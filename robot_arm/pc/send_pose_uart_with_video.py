#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
PC -> Zybo HumanPose2D UART sender + synchronized video viewer.

Place this file in the same pc/ directory as send_pose_uart.py.

Install:
    pip install pyserial opencv-python

PowerShell example:
    python pc/send_pose_uart_with_video.py --port COM5 --csv example_pose2d_1280x720_20hz.csv --video "sample\\예시 동영상.mp4" --hz 20

Keys:
    Q / ESC : stop
"""

import argparse
import csv
import time
from pathlib import Path
from typing import List, Optional

try:
    import cv2
except ImportError:
    cv2 = None

try:
    import serial
except ImportError:
    serial = None

try:
    import send_pose_uart as uart_sender
except ImportError as exc:
    raise SystemExit(
        "send_pose_uart.py를 찾을 수 없습니다.\n"
        "이 파일을 pc/send_pose_uart.py와 같은 폴더에 두세요."
    ) from exc


def _to_float(value: object) -> Optional[float]:
    try:
        return float(str(value).strip())
    except (TypeError, ValueError):
        return None


def load_csv_times(path: str, row_count: int, hz: float) -> List[float]:
    """Use CSV time_sec when available, otherwise row_index / hz."""
    fallback = [i / hz for i in range(row_count)]

    with open(path, "r", newline="", encoding="utf-8-sig") as f:
        rows = list(csv.reader(f))

    rows = [r for r in rows if r and not r[0].lstrip().startswith("#")]
    if not rows:
        return fallback

    first = [c.strip() for c in rows[0]]
    try:
        float(first[0])
        has_header = False
    except Exception:
        has_header = True

    times: List[float] = []

    if has_header:
        with open(path, "r", newline="", encoding="utf-8-sig") as f:
            reader = csv.DictReader(f)
            for row in reader:
                if not row:
                    continue
                normalized = {
                    str(k).strip(): (v.strip() if isinstance(v, str) else v)
                    for k, v in row.items()
                    if k is not None
                }
                t = None
                for key in ("time_sec", "time", "timestamp_sec", "timestamp"):
                    if key in normalized:
                        t = _to_float(normalized[key])
                        if t is not None:
                            break
                if t is None:
                    return fallback
                times.append(t)
    else:
        # extract_real_person_pose_v3.py positional layout:
        # col 0 = frame_id, col 1 = time_sec
        for row in rows:
            if len(row) < 2:
                return fallback
            t = _to_float(row[1])
            if t is None:
                return fallback
            times.append(t)

    if len(times) != row_count:
        return fallback

    t0 = times[0]
    normalized = [max(0.0, t - t0) for t in times]

    for i in range(1, len(normalized)):
        if normalized[i] + 1e-9 < normalized[i - 1]:
            return fallback

    return normalized


class VideoReader:
    def __init__(self, path: str):
        if cv2 is None:
            raise SystemExit("opencv-python이 없습니다. 설치: pip install opencv-python")

        self.cap = cv2.VideoCapture(path)
        if not self.cap.isOpened():
            raise SystemExit(f"동영상을 열 수 없습니다: {path}")

        self.fps = float(self.cap.get(cv2.CAP_PROP_FPS))
        if self.fps <= 0.0:
            self.fps = 30.0

        self.frame_count = int(self.cap.get(cv2.CAP_PROP_FRAME_COUNT))
        self.width = int(self.cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        self.height = int(self.cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        self.current_index = -1
        self.current_frame = None

    def reset(self):
        self.cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
        self.current_index = -1
        self.current_frame = None

    def frame_at_time(self, time_sec: float):
        target = max(0, int(round(time_sec * self.fps)))
        if self.frame_count > 0:
            target = min(target, self.frame_count - 1)

        if target < self.current_index:
            self.reset()

        while self.current_index < target:
            ok, frame = self.cap.read()
            if not ok:
                return self.current_frame
            self.current_index += 1
            self.current_frame = frame

        if self.current_frame is None:
            ok, frame = self.cap.read()
            if not ok:
                return None
            self.current_index = 0
            self.current_frame = frame

        return self.current_frame

    def close(self):
        self.cap.release()


def draw_status(frame, video_time, row_index, row_count, tx_frame_id, csv_frame_id):
    out = frame.copy()
    cv2.rectangle(out, (8, 8), (470, 92), (0, 0, 0), -1)
    cv2.putText(out, f"time={video_time:6.2f}s   UART=TX", (18, 34),
                cv2.FONT_HERSHEY_SIMPLEX, 0.65, (255, 255, 255), 2, cv2.LINE_AA)
    cv2.putText(out, f"row={row_index + 1}/{row_count}  tx_frame={tx_frame_id}", (18, 60),
                cv2.FONT_HERSHEY_SIMPLEX, 0.58, (255, 255, 255), 1, cv2.LINE_AA)
    cv2.putText(out, f"csv_frame={csv_frame_id}   Q/ESC: stop", (18, 84),
                cv2.FONT_HERSHEY_SIMPLEX, 0.58, (255, 255, 255), 1, cv2.LINE_AA)
    return out


def wait_until(deadline: float):
    while True:
        remain = deadline - time.perf_counter()
        if remain <= 0:
            return
        if remain > 0.003:
            time.sleep(remain - 0.002)
        else:
            time.sleep(0.0005)


def main():
    ap = argparse.ArgumentParser(description="UART pose sender + synchronized video viewer")
    ap.add_argument("--port", required=True, help="Windows: COM5, Linux: /dev/ttyUSB0")
    ap.add_argument("--csv", required=True, help="HumanPose2D CSV file")
    ap.add_argument("--video", required=True, help="Video shown while pose rows are sent")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--hz", type=float, default=20.0)
    ap.add_argument("--loop", action="store_true")
    ap.add_argument("--no-board-text", action="store_true")
    ap.add_argument("--start-frame-id", type=int, default=1)
    ap.add_argument("--window-width", type=int, default=960,
                    help="Display width. 0 keeps original size")
    args = ap.parse_args()

    if serial is None:
        raise SystemExit("pyserial이 없습니다. 설치: pip install pyserial")
    if cv2 is None:
        raise SystemExit("opencv-python이 없습니다. 설치: pip install opencv-python")
    if args.hz <= 0:
        raise SystemExit("--hz must be > 0")
    if not Path(args.csv).is_file():
        raise SystemExit(f"CSV를 찾을 수 없습니다: {args.csv}")
    if not Path(args.video).is_file():
        raise SystemExit(f"동영상을 찾을 수 없습니다: {args.video}")

    poses = uart_sender.load_csv(args.csv)
    if not poses:
        raise SystemExit("CSV에 pose row가 없습니다.")

    csv_times = load_csv_times(args.csv, len(poses), args.hz)
    video = VideoReader(args.video)

    print(f"[INFO] rows={len(poses)}, port={args.port}, baud={args.baud}, rate={args.hz:.3f}Hz")
    print(f"[INFO] packet={uart_sender.PACKET_SIZE}B, video={video.width}x{video.height} @ {video.fps:.3f}fps")
    print("[INFO] video + UART start together. Q or ESC = stop")

    tx_frame_id = args.start_frame_id & 0xFFFFFFFF
    period = 1.0 / args.hz
    stop_requested = False

    try:
        with serial.Serial(args.port, args.baud, timeout=0, write_timeout=1.0) as ser_obj:
            time.sleep(0.2)

            while not stop_requested:
                video.reset()
                loop_start = time.perf_counter()

                for row_index, pose in enumerate(poses):
                    # Keep UART transmission at exactly --hz.
                    deadline = loop_start + row_index * period
                    wait_until(deadline)

                    # Use CSV time_sec to select the matching video frame.
                    video_time = csv_times[row_index]
                    frame = video.frame_at_time(video_time)
                    if frame is None:
                        print("[WARN] video ended before CSV")
                        stop_requested = True
                        break

                    packet = uart_sender.make_packet(pose, tx_frame_id)
                    ser_obj.write(packet)

                    if not args.no_board_text:
                        uart_sender.read_board_text(ser_obj)

                    display = draw_status(
                        frame, video_time, row_index, len(poses),
                        tx_frame_id, pose.frame_id_from_csv
                    )

                    if args.window_width > 0 and display.shape[1] != args.window_width:
                        scale = args.window_width / float(display.shape[1])
                        new_h = max(1, int(round(display.shape[0] * scale)))
                        display = cv2.resize(display, (args.window_width, new_h),
                                             interpolation=cv2.INTER_AREA)

                    cv2.imshow("Pose Video + UART TX", display)
                    key = cv2.waitKey(1) & 0xFF
                    if key in (27, ord("q"), ord("Q")):
                        stop_requested = True
                        break

                    if (tx_frame_id % 20) == 0:
                        print(f"[TX] frame={tx_frame_id} csv_frame={pose.frame_id_from_csv} time={video_time:.2f}s")

                    tx_frame_id = (tx_frame_id + 1) & 0xFFFFFFFF

                if stop_requested or not args.loop:
                    break

            time.sleep(0.1)
            if not args.no_board_text:
                uart_sender.read_board_text(ser_obj)

    except serial.SerialException as exc:
        raise SystemExit(f"Serial port error: {exc}") from exc
    except KeyboardInterrupt:
        print("\n[INFO] stopped by Ctrl+C")
    finally:
        video.close()
        cv2.destroyAllWindows()

    print("[OK] transmission finished")


if __name__ == "__main__":
    main()
