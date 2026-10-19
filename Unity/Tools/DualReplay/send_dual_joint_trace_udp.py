#!/usr/bin/env python3
import argparse, csv, json, socket, time
from pathlib import Path

def f(row, key):
    return float(row[key])

def b(row, key):
    return bool(int(row[key]))

def cmd(row, prefix, frame_id):
    return {
        "frame_id": frame_id,
        "elbowRoll": f(row, f"{prefix}_elbow_roll_deg"),
        "elbowPitch": f(row, f"{prefix}_elbow_pitch_deg"),
        "wristPitch": f(row, f"{prefix}_wrist_pitch_deg"),
        "wristRoll": f(row, f"{prefix}_wrist_roll_deg"),
        "gripper": f(row, f"{prefix}_gripper_norm"),
        "valid": b(row, f"{prefix}_valid"),
    }

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trace", required=True)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=5005)
    ap.add_argument("--speed", type=float, default=1.0,
                    help="1.0 = real-time 50Hz, 0 = as fast as possible")
    args = ap.parse_args()

    rows = list(csv.DictReader(Path(args.trace).open(newline="", encoding="utf-8")))
    if not rows:
        raise SystemExit("empty trace")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    start = time.perf_counter()
    sent = 0

    for i, row in enumerate(rows):
        fid = int(row["tick_id"])
        packet = {
            "frame_id": fid,
            "source_frame_id": int(row["source_frame_id"]),
            "left": cmd(row, "left", fid),
            "right": cmd(row, "right", fid),
        }
        payload = json.dumps(packet, separators=(",", ":")).encode("utf-8")
        sock.sendto(payload, (args.host, args.port))
        sent += 1

        if args.speed > 0 and i + 1 < len(rows):
            target = (float(rows[i+1]["time_sec"]) - float(rows[0]["time_sec"])) / args.speed
            delay = target - (time.perf_counter() - start)
            if delay > 0:
                time.sleep(delay)

    print(json.dumps({
        "result": "DONE",
        "sent": sent,
        "host": args.host,
        "port": args.port,
        "speed": args.speed,
    }, indent=2))

if __name__ == "__main__":
    main()
