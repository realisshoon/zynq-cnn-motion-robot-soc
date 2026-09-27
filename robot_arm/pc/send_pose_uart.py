#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
PC -> Zybo HumanPose2D UART sender

- 115200 baud
- default 20 Hz
- 1280x720 coordinates
- binary packet + CRC16-CCITT
- existing MediaPipe/PoseNet-style CSV replay supported

Install:
    pip install pyserial

Example:
    python send_pose_uart.py \
        --port COM5 \
        --csv example_pose2d_1280x720.csv \
        --hz 20

Linux / WSL example:
    python send_pose_uart.py \
        --port /dev/ttyUSB0 \
        --csv example_pose2d_1280x720.csv \
        --hz 20

Packet:
    36 bytes/frame
    36 * 10 UART bits/byte * 20Hz = 7200 bit/s
    115200 baud usage ~= 6.25%
"""

import argparse
import csv
import math
import struct
import sys
import time
from dataclasses import dataclass
from typing import Dict, Iterable, List, Optional

try:
    import serial
except ImportError:
    serial = None


SYNC0 = 0xA5
SYNC1 = 0x5A
VERSION = 1
PAYLOAD_SIZE = 30
PACKET_SIZE = 36

WIDTH = 1280
HEIGHT = 720

POINT_ORDER = [
    "finger1",
    "finger2",
    "elbow",
    "wrist",
    "shoulder_l",
    "shoulder_r",
]

VALID_BITS = {
    "finger1": 1 << 0,
    "finger2": 1 << 1,
    "elbow": 1 << 2,
    "wrist": 1 << 3,
    "shoulder_l": 1 << 4,
    "shoulder_r": 1 << 5,
}


@dataclass
class Point:
    x: float
    y: float
    valid: bool = True


@dataclass
class Pose:
    frame_id_from_csv: int
    valid: bool
    points: Dict[str, Point]


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def safe_pixel(value: float, maximum: int):
    if not math.isfinite(value):
        return 0, False
    iv = int(round(value))
    if iv < 0 or iv >= maximum:
        return max(0, min(maximum - 1, iv)), False
    return iv, True


def make_packet(pose: Pose, tx_frame_id: int) -> bytes:
    mask = 0
    coords: List[int] = []

    for name in POINT_ORDER:
        p = pose.points[name]
        x, x_ok = safe_pixel(p.x, WIDTH)
        y, y_ok = safe_pixel(p.y, HEIGHT)

        valid = bool(pose.valid and p.valid and x_ok and y_ok)
        if valid:
            mask |= VALID_BITS[name]
        else:
            # invalid point의 좌표값은 프로토콜상 의미가 없으므로 0으로 보냄
            x = 0
            y = 0

        coords.extend([x, y])

    payload = struct.pack(
        "<IBB" + "HH" * 6,
        tx_frame_id & 0xFFFFFFFF,
        1 if pose.valid else 0,
        mask,
        *coords,
    )
    assert len(payload) == PAYLOAD_SIZE

    packet = bytearray([SYNC0, SYNC1, VERSION, PAYLOAD_SIZE])
    packet.extend(payload)

    crc = crc16_ccitt(bytes(packet[2:]))
    packet.extend(struct.pack("<H", crc))

    assert len(packet) == PACKET_SIZE
    return bytes(packet)


def to_bool(s, default=True):
    if s is None or s == "":
        return default
    try:
        return bool(int(float(s)))
    except Exception:
        return str(s).strip().lower() in ("true", "t", "yes", "y")


def get_float(row: Dict[str, str], *names, default=0.0):
    for n in names:
        if n in row and row[n] not in (None, ""):
            return float(row[n])
    return default


def get_valid(row: Dict[str, str], base: str, default=True):
    candidates = [
        f"{base}_valid",
        f"valid_{base}",
    ]
    for n in candidates:
        if n in row:
            return to_bool(row[n], default)
    return default


def parse_dict_row(row: Dict[str, str], fallback_id: int) -> Pose:
    # common header aliases
    frame_id = int(float(
        row.get("frame_id", row.get("frame", fallback_id))
    ))
    pose_valid = to_bool(
        row.get("frame_valid", row.get("valid", "1")),
        True
    )

    points = {}
    for name in POINT_ORDER:
        points[name] = Point(
            x=get_float(row, f"{name}_x", f"x_{name}"),
            y=get_float(row, f"{name}_y", f"y_{name}"),
            valid=get_valid(row, name, True),
        )

    return Pose(frame_id, pose_valid, points)


def parse_positional_row(cols: List[str], fallback_id: int) -> Pose:
    """
    Existing extract_real_person_pose_v3.py positional layout:
      0 frame_id
      1 time_sec
      2 frame_valid
      3 shoulder_l_x
      4 shoulder_l_y
      5 shoulder_l_valid
      6 shoulder_r_x
      7 shoulder_r_y
      8 shoulder_r_valid
      9 elbow_x
     10 elbow_y
     11 elbow_valid
     12 wrist_x
     13 wrist_y
     14 wrist_valid
     15 finger1_x
     16 finger1_y
     17 finger1_valid
     18 finger2_x
     19 finger2_y
     20 finger2_valid
    """
    if len(cols) < 21:
        raise ValueError(
            f"CSV positional row needs >=21 columns, got {len(cols)}"
        )

    frame_id = int(float(cols[0]))
    pose_valid = to_bool(cols[2], True)

    points = {
        "shoulder_l": Point(float(cols[3]), float(cols[4]), to_bool(cols[5])),
        "shoulder_r": Point(float(cols[6]), float(cols[7]), to_bool(cols[8])),
        "elbow":      Point(float(cols[9]), float(cols[10]), to_bool(cols[11])),
        "wrist":      Point(float(cols[12]), float(cols[13]), to_bool(cols[14])),
        "finger1":    Point(float(cols[15]), float(cols[16]), to_bool(cols[17])),
        "finger2":    Point(float(cols[18]), float(cols[19]), to_bool(cols[20])),
    }

    return Pose(frame_id, pose_valid, points)


def load_csv(path: str) -> List[Pose]:
    with open(path, "r", newline="", encoding="utf-8-sig") as f:
        rows = list(csv.reader(f))

    rows = [r for r in rows if r and not r[0].lstrip().startswith("#")]
    if not rows:
        return []

    first = [c.strip() for c in rows[0]]

    # header 여부: 첫 cell이 숫자로 변환되지 않으면 header로 판단
    has_header = False
    try:
        float(first[0])
    except Exception:
        has_header = True

    poses: List[Pose] = []

    if has_header:
        with open(path, "r", newline="", encoding="utf-8-sig") as f:
            reader = csv.DictReader(f)
            for i, row in enumerate(reader):
                if not row:
                    continue
                normalized = {
                    str(k).strip(): (v.strip() if isinstance(v, str) else v)
                    for k, v in row.items()
                    if k is not None
                }
                poses.append(parse_dict_row(normalized, i))
    else:
        for i, row in enumerate(rows):
            poses.append(
                parse_positional_row([c.strip() for c in row], i)
            )

    return poses


def read_board_text(ser_obj):
    waiting = ser_obj.in_waiting
    if waiting <= 0:
        return

    data = ser_obj.read(waiting)
    if not data:
        return

    text = data.decode("utf-8", errors="replace")
    sys.stdout.write(text)
    sys.stdout.flush()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True,
                    help="Windows: COM5, Linux: /dev/ttyUSB0")
    ap.add_argument("--csv", required=True)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--hz", type=float, default=20.0)
    ap.add_argument("--loop", action="store_true",
                    help="CSV 끝까지 보내고 처음부터 반복")
    ap.add_argument("--no-board-text", action="store_true",
                    help="Zybo -> PC debug text를 표시하지 않음")
    ap.add_argument("--start-frame-id", type=int, default=1,
                    help="UART로 보낼 첫 frame_id")
    args = ap.parse_args()

    if serial is None:
        raise SystemExit(
            "pyserial이 없습니다. 설치: pip install pyserial"
        )

    if args.hz <= 0:
        raise SystemExit("--hz must be > 0")

    poses = load_csv(args.csv)
    if not poses:
        raise SystemExit("CSV에 pose row가 없습니다.")

    period = 1.0 / args.hz
    tx_frame_id = args.start_frame_id

    print(
        f"[INFO] rows={len(poses)}, port={args.port}, "
        f"baud={args.baud}, rate={args.hz:.3f}Hz"
    )
    print(
        f"[INFO] packet={PACKET_SIZE}B, "
        f"UART bandwidth={PACKET_SIZE * 10 * args.hz:.0f} bit/s "
        f"({PACKET_SIZE * 10 * args.hz / args.baud * 100.0:.2f}% of baud)"
    )

    with serial.Serial(
        args.port,
        args.baud,
        timeout=0,
        write_timeout=1.0,
    ) as ser_obj:
        # Zybo reset 직후에도 연결할 시간을 조금 줌
        time.sleep(0.2)

        next_deadline = time.perf_counter()

        while True:
            for row_index, pose in enumerate(poses):
                now = time.perf_counter()
                delay = next_deadline - now
                if delay > 0:
                    time.sleep(delay)

                packet = make_packet(pose, tx_frame_id)
                ser_obj.write(packet)

                if not args.no_board_text:
                    read_board_text(ser_obj)

                if (tx_frame_id % 20) == 0:
                    print(
                        f"[TX] frame={tx_frame_id} "
                        f"csv_frame={pose.frame_id_from_csv}"
                    )

                tx_frame_id = (tx_frame_id + 1) & 0xFFFFFFFF
                next_deadline += period

                # PC가 크게 밀렸으면 과거 deadline을 따라잡으려 폭주하지 않음
                now = time.perf_counter()
                if next_deadline < now - period:
                    next_deadline = now + period

            if not args.loop:
                break

        # 마지막 Zybo debug 문자열 수신
        time.sleep(0.1)
        if not args.no_board_text:
            read_board_text(ser_obj)

    print("[OK] transmission finished")


if __name__ == "__main__":
    main()
