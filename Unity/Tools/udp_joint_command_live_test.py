import argparse
import json
import math
import socket
import time


def clamp(v, lo, hi):
    return max(lo, min(hi, v))


def main():
    parser = argparse.ArgumentParser(
        description="Continuous JointCommand test sender for HumanMotionDigitalTwin"
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5005)
    parser.add_argument("--hz", type=float, default=50.0)
    parser.add_argument("--duration", type=float, default=20.0,
                        help="seconds; <=0 means run until Ctrl+C")
    parser.add_argument("--start-frame", type=int, default=1)
    args = parser.parse_args()

    dt = 1.0 / args.hz
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    frame_id = args.start_frame
    start = time.perf_counter()
    next_t = start

    print(f"[START] UDP {args.host}:{args.port} @ {args.hz:.1f} Hz")
    print("[INFO] Ctrl+C to stop")

    try:
        while True:
            now = time.perf_counter()
            elapsed = now - start
            if args.duration > 0 and elapsed >= args.duration:
                break

            # Smooth, human-arm-like demo motion.
            # Values stay within the current robot calibration ranges.
            base = 90.0 + 35.0 * math.sin(2.0 * math.pi * 0.10 * elapsed)
            shoulder = 90.0 + 35.0 * math.sin(2.0 * math.pi * 0.13 * elapsed + 0.4)
            elbow = 90.0 + 50.0 * math.sin(2.0 * math.pi * 0.16 * elapsed + 1.0)
            wrist_pitch = 90.0 + 30.0 * math.sin(2.0 * math.pi * 0.20 * elapsed + 1.7)
            wrist_roll = 90.0 + 55.0 * math.sin(2.0 * math.pi * 0.11 * elapsed + 2.2)
            gripper = 0.5 + 0.5 * math.sin(2.0 * math.pi * 0.08 * elapsed)

            packet = {
                "frame_id": frame_id & 0xFFFFFFFF,
                "valid": True,
                "base_deg": clamp(base, 10.0, 170.0),
                "shoulder_deg": clamp(shoulder, 20.0, 160.0),
                "elbow_deg": clamp(elbow, 10.0, 170.0),
                "wrist_pitch_deg": clamp(wrist_pitch, 20.0, 160.0),
                "wrist_roll_deg": clamp(wrist_roll, 0.0, 180.0),
                "gripper_norm": clamp(gripper, 0.0, 1.0),
            }

            payload = json.dumps(packet, separators=(",", ":")).encode("utf-8")
            sock.sendto(payload, (args.host, args.port))

            if frame_id % max(1, int(args.hz)) == 0:
                print(
                    f"frame={packet['frame_id']} "
                    f"B={packet['base_deg']:.1f} "
                    f"S={packet['shoulder_deg']:.1f} "
                    f"E={packet['elbow_deg']:.1f} "
                    f"WP={packet['wrist_pitch_deg']:.1f} "
                    f"WR={packet['wrist_roll_deg']:.1f} "
                    f"G={packet['gripper_norm']:.2f}"
                )

            frame_id = (frame_id + 1) & 0xFFFFFFFF

            next_t += dt
            sleep_sec = next_t - time.perf_counter()
            if sleep_sec > 0:
                time.sleep(sleep_sec)
            else:
                # If the process falls behind, resync instead of bursting old packets.
                next_t = time.perf_counter()

    except KeyboardInterrupt:
        print("\n[STOP] Ctrl+C")
    finally:
        sock.close()

    print(f"[DONE] last_frame={frame_id - 1}")


if __name__ == "__main__":
    main()
