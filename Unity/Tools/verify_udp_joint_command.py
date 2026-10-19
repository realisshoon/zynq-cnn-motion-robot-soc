#!/usr/bin/env python3
"""실제 Python 송신기와 Unity Play Mode 캡처를 대조하는 localhost 통합 검증."""
import argparse
import json
import math
from pathlib import Path
import socket
import subprocess
import sys
import time

from udp_joint_command_sender import encode, neutral, poses

ROOT = Path(__file__).resolve().parents[1]
FIELDS = ("base_deg", "shoulder_deg", "elbow_deg", "wrist_pitch_deg", "wrist_roll_deg")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def near(actual, expected, label):
    require(math.isclose(actual, expected, abs_tol=0.002), f"{label}: {actual} != {expected}")


def check_pose(status):
    if not status["applied"]:
        return
    command = status["command"]
    for actual, field in zip(status["localAngles"], FIELDS):
        expected = command[field] - 90
        near((actual - expected + 180) % 360 - 180, 0, field)
    opening = min(1, max(0, command["gripper_norm"]))
    near(status["left"]["x"], -0.04 - opening * 0.16, "Finger_L")
    near(status["right"]["x"], 0.04 + opening * 0.16, "Finger_R")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=5005)
    parser.add_argument("--telemetry", type=Path, default=ROOT / "Validation/udp_status.json")
    args = parser.parse_args()
    report = ROOT / "Validation/UdpJointCommandValidation.md"
    results = []

    def passed(label):
        results.append(f"- {label}: PASS")
        print(f"{label}: PASS", flush=True)

    def read():
        try:
            status = json.loads(args.telemetry.read_text(encoding="utf-8-sig"))
            require(time.time() * 1000 - status["utc_ms"] < 5000, "Unity 캡처가 5초 이상 갱신되지 않음")
            return status
        except (FileNotFoundError, PermissionError, json.JSONDecodeError):
            return None

    def wait_for(predicate, message, timeout=6):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            status = read()
            if status is not None and predicate(status):
                return status
            time.sleep(0.02)
        raise AssertionError(message)

    child = None
    try:
        initial = wait_for(lambda s: s["playing"] and s["listening"] and s["mode"] == 1,
                           "Play Mode + UDP Mode + Start UDP Validation Capture가 필요함")
        require(initial["received"] == 0, "새 Play 세션 또는 Restart Listener로 frame sequence를 초기화해야 함")
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
            try:
                probe.bind(("127.0.0.1", args.port))
            except OSError:
                pass
            else:
                raise AssertionError("Unity가 지정 포트를 소유하지 않음")

        child = subprocess.Popen([sys.executable, str(ROOT / "Tools/udp_joint_command_sender.py"),
                                  "--port", str(args.port)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        seen = set()
        previous_frame = None
        frame_samples = 0
        deadline = time.monotonic() + 30
        expected_poses = list(poses())
        while child.poll() is None:
            require(time.monotonic() < deadline, "PC sender 시간 초과")
            status = read()
            if status and status["applied"]:
                check_pose(status)
                frame = status["lastAppliedFrameId"]
                if frame != previous_frame:
                    require(previous_frame is None or frame > previous_frame, "frame_id 역행")
                    previous_frame = frame
                    frame_samples += 1
                for label, command in expected_poses:
                    if all(math.isclose(status["command"][field], value, abs_tol=0.001)
                           for field, value in command.items()):
                        seen.add(label)
                if status["command"]["shoulder_deg"] == 120 and status["command"]["elbow_deg"] == 60:
                    near((status["elbowWorldX"] + 180) % 360 - 180, 0, "Shoulder/Elbow world")
            time.sleep(0.02)
        output = child.communicate()[0]
        require(child.returncode == 0, output)
        (ROOT / "Validation/udp_sender.log").write_text(output, encoding="utf-8")
        require(all(label in seen for label, _ in expected_poses), "관찰하지 못한 자세: " + str(set(label for label, _ in expected_poses) - seen))
        for label in ("Neutral", "Base", "Shoulder/Elbow", "Wrist", "Gripper"):
            passed(label + " (실제 Python sender 20 Hz → Unity Transform)")
        require(frame_samples > 20, "frame 진행 샘플 부족")
        passed(f"frame_id progression ({frame_samples}개 관찰)")
        passed("수신 명령 즉시 적용 / Unity smoothing 없음")

        destination = ("127.0.0.1", args.port)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            status = wait_for(lambda s: s["lastAppliedFrameId"] == 240, "기본 송신기의 최종 frame=240 미수신")
            malformed_before = status["malformed"]
            baseline_applied = status["applied"]
            valid_packet = encode(neutral(), 241).decode()
            bad_packets = [b"not-json", b"{", b"", b"{}", b"[]", b"null", b"\xff\xfe",
                           b"x" * 5000, (valid_packet + " garbage").encode(),
                           valid_packet.replace('"valid":true', '"valid":"true"').encode(),
                           valid_packet.replace('"base_deg":90.0', '"base_deg":null').encode(),
                           valid_packet.replace('"base_deg":90.0', '"base_deg":NaN').encode(),
                           valid_packet.replace('"base_deg":90.0', '"base_deg":1e999').encode(),
                           valid_packet.replace('"frame_id":241', '"frame_id":-1').encode(),
                           valid_packet.replace('"frame_id":241', '"frame_id":241,"frame_id":242').encode()]
            for packet in bad_packets:
                sock.sendto(packet, destination)
                time.sleep(0.02)
            status = wait_for(lambda s: s["malformed"] == malformed_before + len(bad_packets), "malformed 패킷 처리 실패")
            require(status["lastReceivedFrameId"] == 240 and status["applied"] == baseline_applied, "malformed가 sequence/pose를 변경함")
            passed(f"Malformed ({len(bad_packets)}종) / 누락·중복·타입·NaN/Infinity 처리")

            sock.sendto(encode({**neutral(), "base_deg": 120}, 241), destination)
            status = wait_for(lambda s: s["lastAppliedFrameId"] == 241, "malformed 이후 복구 실패")
            check_pose(status)
            baseline_applied = status["applied"]
            invalid_before = status["invalid"]
            sock.sendto(encode({**neutral(), "valid": False, "base_deg": 60, "gripper_norm": 0}, 242), destination)
            status = wait_for(lambda s: s["invalid"] == invalid_before + 1 and s["lastReceivedFrameId"] == 242, "invalid 미수신")
            require(status["applied"] == baseline_applied and status["lastAppliedFrameId"] == 241, "valid=false가 pose를 변경함")
            check_pose(status)
            passed("valid=false HOLD / malformed 이후 정상 복구")

            stale_before = status["stale"]
            for frame in (240, 241, 242):
                sock.sendto(encode(neutral(), frame), destination)
            status = wait_for(lambda s: s["stale"] == stale_before + 3, "역순/중복 frame 미거부")
            require(status["applied"] == baseline_applied, "stale 패킷이 적용됨")
            check_pose(status)
            passed("오래된/중복 frame 무시 (invalid sequence 포함)")

            for frame in range(243, 643):
                sock.sendto(encode({**neutral(), "base_deg": float(60 + frame % 61)}, frame), destination)
            sock.sendto(encode({**neutral(), "base_deg": 60}, 643), destination)
            status = wait_for(lambda s: s["lastAppliedFrameId"] == 643, "burst 최종 명령 미적용")
            check_pose(status)
            require(status["applied"] - baseline_applied < 401, "모든 패킷을 backlog로 순차 적용함")
            passed("burst 최신 명령만 적용 (bounded mailbox)")
            sock.sendto(encode(neutral(), 644), destination)
            status = wait_for(lambda s: s["lastAppliedFrameId"] == 644, "Neutral 복원 실패")
            check_pose(status)

        report.write_text("# STEP 2-A UDP 통합 검증\n\n" + "\n".join(results) + "\n\nPlay 종료 검증 대기 중.\n", encoding="utf-8")
        print("PLAY STOP REQUIRED: Unity Play를 종료하세요. 소켓/스레드 정리를 검증합니다.", flush=True)
        status = wait_for(lambda s: not s["playing"] and s["cleanupObserved"] and s["lastStopJoined"],
                          "Play 종료 후 thread Join 확인 실패", timeout=180)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
            probe.bind(destination)
        passed("Play stop socket cleanup / thread Join / 동일 port 재bind")
        results.append("\n전체 결과: PASS")
    except Exception as exception:
        results.append("\n전체 결과: FAIL — " + str(exception))
        raise
    finally:
        if child is not None and child.poll() is None:
            child.terminate()
            child.wait(timeout=5)
        report.write_text("# STEP 2-A UDP 통합 검증\n\n실행 UTC: " + time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()) +
                          "\n\n" + "\n".join(results) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
