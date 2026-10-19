"""Unity Run Validation의 CSV 대기 상태에서 canonical trace를 실제 50 Hz UDP로 검증."""
import csv
import json
import math
from pathlib import Path
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]

def status():
    for attempt in range(30):
        try:
            value = json.loads((ROOT/"Validation/dual_status.json").read_text(encoding="utf-8"))
            break
        except (OSError, json.JSONDecodeError):
            if attempt==29: raise
            time.sleep(.02)
    assert time.time()*1000-value["utcMs"] < 3000, "Unity telemetry가 오래되었습니다. Play Mode를 확인하세요."
    return value

def main():
    trace = HERE/"build/joint_command_trace.csv"
    with trace.open(newline="",encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    assert len(rows)>100
    for i,row in enumerate(rows):
        assert int(row["frame_id"])==i+1
        assert abs(float(row["time_sec"])-i*.02)<1e-6
    assert len({r["source_frame_id"] for r in rows}) < len(rows)
    before=status()
    assert before["phase"]==5 and before["listening"] and before["frame"]==0, "Run Validation 완료 후 새 CSV 수신 상태가 필요합니다."
    samples=[]
    log=(HERE/"build/replay_validation.log").open("w",encoding="utf-8")
    proc=subprocess.Popen([sys.executable,str(HERE/"send_joint_trace_udp.py"),"--csv",str(trace)],stdout=log,stderr=subprocess.STDOUT)
    try:
        while proc.poll() is None:
            samples.append(status());time.sleep(.1)
        assert proc.returncode==0
        deadline=time.monotonic()+3
        while status()["frame"]!=len(rows) and time.monotonic()<deadline:time.sleep(.05)
        end=status(); assert end["frame"]==len(rows)
        assert end["stale"]==0 and not end["error"]
        for sample in samples+[end]:
            assert sample["leftGrab"]==before["leftGrab"], "CSV가 왼팔에도 적용되었습니다."
            if sample["frame"]:
                expected=rows[sample["frame"]-1]
                for key in ("base_deg","shoulder_deg","elbow_deg","wrist_pitch_deg","wrist_roll_deg","gripper_norm"):
                    assert math.isclose(sample["command"][key],float(expected[key]),abs_tol=.0002),(key,sample["frame"])
                assert abs(sample["baseAngle"]-(sample["command"]["base_deg"]-90))<.001
        text=("# CSV 실제 UDP replay 검증\n\n"
              f"- PASS: canonical C trace {len(rows)}행 / 20 ms / {rows[-1]['time_sec']}초\n"
              "- PASS: source frame과 transport sequence 분리\n"
              f"- PASS: Unity 수신 {end['received']}, 적용 {end['applied']}, 마지막 frame {end['frame']}, stale 0\n"
              f"- PASS: {len(samples)+1}개 telemetry 표본의 6채널 값이 해당 canonical output 행과 일치\n"
              "- PASS: 오른팔 Base pivot 일치 / 왼팔 GrabPoint 불변\n"
              "- Windows/Python UDP 송신은 실시간 OS 보장이 아닌 20 ms deadline 기반 재생입니다.\n")
        (ROOT/"Validation/CsvReplayValidation.md").write_text(text,encoding="utf-8")
        print(text)
    finally:
        if proc.poll() is None:proc.terminate();proc.wait()
        log.close()

if __name__=="__main__":main()
