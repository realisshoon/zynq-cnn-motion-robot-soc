#!/usr/bin/env python3
import argparse
import csv
import json
import math
import socket
import time

FIELDS = (
    "base_deg",
    "shoulder_deg",
    "elbow_deg",
    "wrist_pitch_deg",
    "wrist_roll_deg",
    "gripper_norm",
)


def parse_bool(value):
    return str(value).strip().lower() in {"1", "true", "t", "yes", "y"}


def load_trace(path):
    rows = []
    with open(path, "r", encoding="utf-8-sig", newline="") as f:
        reader = csv.DictReader(f)
        required = {"time_sec", "frame_id", "valid", *FIELDS}
        missing = required.difference(reader.fieldnames or [])
        if missing:
            raise SystemExit(f"Missing columns: {sorted(missing)}")

        previous_time = -1.0
        for line_no, row in enumerate(reader, start=2):
            try:
                t = float(row["time_sec"])
                frame_id = int(row["frame_id"])
                command = {name: float(row[name]) for name in FIELDS}
            except Exception as exc:
                raise SystemExit(f"Bad row {line_no}: {exc}") from exc

            if not math.isfinite(t) or t < 0 or t < previous_time:
                raise SystemExit(f"Invalid/non-monotonic time_sec at row {line_no}")
            if frame_id < 0:
                raise SystemExit(f"Invalid frame_id at row {line_no}")
            if not all(math.isfinite(v) for v in command.values()):
                raise SystemExit(f"Non-finite command at row {line_no}")

            rows.append({
                "time_sec": t,
                "frame_id": frame_id,
                "valid": parse_bool(row["valid"]),
                **command,
            })
            previous_time = t

    if not rows:
        raise SystemExit("Trace is empty")
    return rows


def main():
    ap = argparse.ArgumentParser(description="Replay JointCommand CSV to Unity UDP receiver")
    ap.add_argument("--csv", required=True, help="joint_command_trace.csv")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=5005)
    ap.add_argument("--speed", type=float, default=1.0, help="1.0=real time, 2.0=2x")
    ap.add_argument("--loop", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    if args.speed <= 0:
        raise SystemExit("--speed must be > 0")

    rows = load_trace(args.csv)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    loop_index = 0
    id_span = max(row["frame_id"] for row in rows) + 1

    print(f"[LOAD] {len(rows)} commands, duration={rows[-1]['time_sec']:.3f}s")
    print(f"[TARGET] {args.host}:{args.port}, speed={args.speed}x, loop={args.loop}")

    try:
        while True:
            start = time.perf_counter()
            for i, row in enumerate(rows):
                deadline = start + row["time_sec"] / args.speed
                delay = deadline - time.perf_counter()
                if delay > 0:
                    time.sleep(delay)

                # Unity receiver requires a strictly increasing transport frame id.
                packet = {
                    "frame_id": int(row["frame_id"] + loop_index * id_span) & 0xFFFFFFFF,
                    "valid": bool(row["valid"]),
                    "base_deg": row["base_deg"],
                    "shoulder_deg": row["shoulder_deg"],
                    "elbow_deg": row["elbow_deg"],
                    "wrist_pitch_deg": row["wrist_pitch_deg"],
                    "wrist_roll_deg": row["wrist_roll_deg"],
                    "gripper_norm": row["gripper_norm"],
                }

                if not args.dry_run:
                    sock.sendto(json.dumps(packet, separators=(",", ":")).encode("utf-8"),
                                (args.host, args.port))

                if i == 0 or (i + 1) % 50 == 0 or i + 1 == len(rows):
                    print(
                        f"[TX] {i+1}/{len(rows)} frame={packet['frame_id']} "
                        f"B={packet['base_deg']:.1f} S={packet['shoulder_deg']:.1f} "
                        f"E={packet['elbow_deg']:.1f} WP={packet['wrist_pitch_deg']:.1f} "
                        f"WR={packet['wrist_roll_deg']:.1f} G={packet['gripper_norm']:.2f}"
                    )

            if not args.loop:
                break
            loop_index += 1
    except KeyboardInterrupt:
        print("\n[STOP]")
    finally:
        sock.close()

    print("[DONE]")


if __name__ == "__main__":
    main()
