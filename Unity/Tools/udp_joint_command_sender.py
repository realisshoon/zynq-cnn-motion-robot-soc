#!/usr/bin/env python3
"""최종 JointCommand JSON을 보내는 PC 테스트 송신기. 보정/보간/스무딩 없음."""
import argparse
import json
import math
import socket
import time


def neutral():
    return dict(valid=True, base_deg=90.0, shoulder_deg=90.0, elbow_deg=90.0,
                wrist_pitch_deg=90.0, wrist_roll_deg=90.0, gripper_norm=1.0)


def poses(case="all"):
    """각 항목은 유지할 고정 명령이며 항목 사이를 보간하지 않는다."""
    groups = {
        "neutral": [("Neutral", {})],
        "base": [(f"Base {angle}", dict(base_deg=float(angle))) for angle in (90, 120, 60, 90)],
        "shoulder-elbow": [("Shoulder/Elbow neutral", {}),
                           ("Shoulder/Elbow", dict(shoulder_deg=120.0, elbow_deg=60.0))],
        "wrist": [("Wrist neutral", {}), ("Wrist", dict(wrist_pitch_deg=120.0, wrist_roll_deg=140.0))],
        "gripper": [(f"Gripper {opening}", dict(gripper_norm=float(opening))) for opening in (1, 0, 1)],
    }
    for name, stages in groups.items():
        if case not in ("all", name):
            continue
        for label, changes in stages:
            yield label, {**neutral(), **changes}


def encode(command, frame_id):
    return json.dumps({"frame_id": frame_id, **command}, allow_nan=False,
                      separators=(",", ":")).encode("utf-8")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5005)
    parser.add_argument("--hz", type=float, default=20.0)
    parser.add_argument("--seconds-per-pose", type=float, default=1.0)
    parser.add_argument("--case", choices=("all", "neutral", "base", "shoulder-elbow", "wrist", "gripper"), default="all")
    parser.add_argument("--start-frame", type=int, default=1)
    parser.add_argument("--repeat", type=int, default=1)
    args = parser.parse_args(argv)
    if not 1 <= args.port <= 65535:
        parser.error("port는 1~65535 범위여야 합니다.")
    if not math.isfinite(args.hz) or args.hz <= 0 or not math.isfinite(args.seconds_per_pose) or args.seconds_per_pose <= 0:
        parser.error("hz와 seconds-per-pose는 양의 유한한 값이어야 합니다.")
    if not 0 <= args.start_frame <= 0xFFFFFFFF or args.repeat < 1:
        parser.error("start-frame은 uint32, repeat는 1 이상이어야 합니다.")
    destination = (socket.gethostbyname(args.host), args.port)
    frame = args.start_frame
    sent = 0
    period = 1.0 / args.hz
    count = max(1, round(args.seconds_per_pose * args.hz))
    deadline = time.monotonic()
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            for _ in range(args.repeat):
                for label, command in poses(args.case):
                    print(f"{label}: frame_id={frame}, {destination[0]}:{destination[1]}, {args.hz:g} Hz", flush=True)
                    for _ in range(count):
                        sock.sendto(encode(command, frame), destination)
                        sent += 1
                        frame = (frame + 1) & 0xFFFFFFFF
                        deadline += period
                        delay = deadline - time.monotonic()
                        if delay > 0:
                            time.sleep(delay)
                        else:
                            deadline = time.monotonic()  # 지연을 burst로 따라잡지 않는다.
    except KeyboardInterrupt:
        print("송신 중지", flush=True)
    print(f"sent={sent}, next_frame_id={frame}", flush=True)


if __name__ == "__main__":
    main()
