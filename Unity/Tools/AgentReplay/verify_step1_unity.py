"""실제 Main UDP replay를 실행하고 적용된 모든 명령/transform을 검증한다."""
import csv
import argparse
import io
import json
import math
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
BUILD = HERE / "build"
VALIDATION = ROOT / "Validation"
FIELDS = ("base_deg", "shoulder_deg", "elbow_deg", "wrist_pitch_deg", "wrist_roll_deg", "gripper_norm")
ANGLES = ("base_local", "shoulder_local", "elbow_local", "wrist_pitch_local", "wrist_roll_local")


def read_shared(path):
    for attempt in range(100):
        try:
            return path.read_text(encoding="utf-8-sig")
        except OSError:
            if attempt == 99:
                raise
            time.sleep(0.02)


def status():
    return json.loads(read_shared(VALIDATION / "step1_status.json"))


def rows(path):
    return list(csv.DictReader(io.StringIO(read_shared(path))))


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def angle_error(a, b):
    return abs((a - b + 180) % 360 - 180)


def main():
    global VALIDATION
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dual", action="store_true", help="DualRobotDemo 오른팔과 왼팔 독립성 검증")
    args = parser.parse_args()
    label = "Dual RobotArm_R" if args.dual else "Main"
    prefix = "dual_step1" if args.dual else "step1"
    if args.dual:
        VALIDATION = VALIDATION / "DualStep1"
    s = status()
    require(time.time() * 1000 - s["utcMs"] < 5000, "Unity status가 오래됨")
    require(s["playing"] and s["listening"] and s["manualRegression"], "Main 검증 메뉴를 먼저 실행하세요")
    require(s["received"] == 0 and s["applied"] == 0, "새 Main 검증 실행이 필요합니다")
    trace = rows(BUILD / "joint_command_trace.csv")
    physical = rows(BUILD / "physical_command_pwm_trace.csv")
    by_id = {int(row["frame_id"]): row for row in trace}
    require(len(trace) == len(physical), "양쪽 trace 길이")
    for unity, servo in zip(trace, physical):
        require(all(unity[k] == servo[k] for k in unity), "양쪽 final output 불일치")

    with (BUILD / (prefix + "_udp_sender.log")).open("w", encoding="utf-8") as log:
        subprocess.run([sys.executable, str(HERE / "send_joint_trace_udp.py"),
                        "--csv", str(BUILD / "joint_command_trace.csv")],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        s = status()
        if s["frame"] == int(trace[-1]["frame_id"]) and s["captured"] == s["applied"]:
            break
        time.sleep(0.1)
    require(s["frame"] == int(trace[-1]["frame_id"]), "마지막 output 적용")
    require(s["received"] == len(trace), "UDP 전체 수신")
    for key in ("stale", "malformed", "invalid", "missedCapture"):
        require(s[key] == 0, key)
    require(not s["error"], s["error"])
    applied = rows(VALIDATION / "step1_unity_applied.csv")
    require(len(applied) == s["captured"] == s["applied"] > 0, "모든 적용 tick 캡처")
    previous = 0
    max_command_error = 0.0
    max_angle_error = 0.0
    for row in applied:
        frame = int(row["frame_id"])
        require(frame > previous, "적용 순서")
        previous = frame
        expected = by_id[frame]
        require(row["valid"] == expected["valid"] == "1", "valid")
        for key in FIELDS:
            value = float(row[key])
            error = abs(value - float(expected[key]))
            require(math.isfinite(value) and error < 0.0001, f"tick {frame}: {key}")
            max_command_error = max(max_command_error, error)
        for field, angle in zip(FIELDS, ANGLES):
            error = angle_error(float(row[angle]), float(expected[field]) - 90)
            require(error < 0.001, f"tick {frame}: {angle} transform")
            max_angle_error = max(max_angle_error, error)
        error = angle_error(float(row["gripper_gear_local"]), -28 + 40 * float(expected["gripper_norm"]))
        require(error < 0.001, f"tick {frame}: linkage gripper")
        max_angle_error = max(max_angle_error, error)
    ranges = {key: [min(float(row[key]) for row in applied), max(float(row[key]) for row in applied)] for key in FIELDS}
    require(all(high > low for low, high in ranges.values()), "5개 joint 및 gripper 실제 변화")
    result = {"result": "PASS", "udp_sent": len(trace), "physical_equal_ticks": len(trace),
              "unity": s, "compared_applied_ticks": len(applied), "max_command_error": max_command_error,
              "max_transform_error_deg": max_angle_error, "command_ranges": ranges,
              "not_rendered_intermediate_ticks": len(trace) - len(applied)}
    if args.dual:
        ownership = json.loads(read_shared(VALIDATION / "ownership.json"))
        require(time.time()*1000-ownership["utcMs"] < 5000, "ownership 감시 최신성")
        require(ownership["checkedRightFrames"] >= len(applied) and ownership["startResetGuard"], "전체 playback ownership 감시")
        for key in ("leftChangedFrames", "overwriteFrames", "listenerErrors"):
            require(ownership[key] == 0, key)
        require(ownership["armCount"] == 2 and ownership["receiverCount"] == ownership["listenerCount"] == 1 and ownership["framePresent"], "Dual 구조")
        result["ownership"] = ownership
    (BUILD / (prefix + "_unity_results.json")).write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
    report = f"""# STEP 1 {label} 실제 UDP 검증

- 결과: PASS
- 전송 / 수신: {len(trace)} / {s['received']}
- 적용 / 캡처 / 같은 tick 명령·transform 대조: {s['applied']} / {s['captured']} / {len(applied)}
- stale / malformed / invalid / 캡처 누락: {s['stale']} / {s['malformed']} / {s['invalid']} / {s['missedCapture']}
- final output → Agent3 입력과 UDP 입력 동일성: {len(trace)}개 전체 PASS
- 명령 최대 오차: {max_command_error:.9g}; transform 최대 오차: {max_angle_error:.9g}도
- 5개 joint 및 linkage gripper의 실제 변화: PASS
- 사전 검증 및 UDP에서 Manual overwrite 방지: PASS

기존 receiver는 렌더 Update에서 최신 mailbox 한 개를 적용한다. 따라서 수신 tick 중 {len(trace)-len(applied)}개는 렌더 사이에 최신값으로 교체됐으며, stale 패킷이나 UDP 손실이 아니다. 적용된 모든 tick을 같은 frame_id의 물리 경로 명령과 비교했다. 50Hz 모든 tick을 화면에 그린다는 의미는 아니다.

실제 보드 및 서보 운동은 미검증이며 physical 검증은 canonical Agent3/PWM host mock 경로다.
"""
    if args.dual:
        report += "\n- PASS: 두 팔 및 T-frame 존재, receiver/listener 1개, 왼팔 전체 local transform 불변\n- PASS: 매 LateUpdate 오른팔 overwrite 감시, START/Advance 자극, CSV START/RESET guard\n"
    (VALIDATION / "Step1UnityValidation.md").write_text(report, encoding="utf-8")
    print(json.dumps(result, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
