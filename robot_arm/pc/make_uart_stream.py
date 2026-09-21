#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
CSV -> 실제 UART와 동일한 binary byte stream 생성기

XSA가 없는 PC 테스트에서는:

CSV
 -> 이 Python 파일
 -> uart_pose_stream.bin
 -> C parser가 1 byte씩 읽음
 -> HumanPose2D
 -> Agent1

즉 실제 UART 전기 신호만 제외하고 packet/byte/parser 경로를 그대로 테스트한다.
"""

import argparse
import csv
import math
import struct
from dataclasses import dataclass
from typing import Dict, List

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
    valid: bool


@dataclass
class Pose:
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


def as_bool(s, default=True):
    if s is None or s == "":
        return default
    try:
        return bool(int(float(s)))
    except Exception:
        return str(s).strip().lower() in ("1", "true", "yes", "y")


def pixel(v, maximum):
    if not math.isfinite(v):
        return 0, False

    iv = int(round(v))
    if iv < 0 or iv >= maximum:
        return max(0, min(maximum - 1, iv)), False

    return iv, True


def parse_positional(row: List[str]) -> Pose:
    """
    extract_real_person_pose_v3.py 형식:
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
    if len(row) < 21:
        raise ValueError(f"need >= 21 columns, got {len(row)}")

    return Pose(
        valid=as_bool(row[2]),
        points={
            "shoulder_l": Point(float(row[3]), float(row[4]), as_bool(row[5])),
            "shoulder_r": Point(float(row[6]), float(row[7]), as_bool(row[8])),
            "elbow":      Point(float(row[9]), float(row[10]), as_bool(row[11])),
            "wrist":      Point(float(row[12]), float(row[13]), as_bool(row[14])),
            "finger1":    Point(float(row[15]), float(row[16]), as_bool(row[17])),
            "finger2":    Point(float(row[18]), float(row[19]), as_bool(row[20])),
        },
    )


def get_float(row, *names):
    for n in names:
        if n in row and row[n] not in ("", None):
            return float(row[n])
    raise KeyError(f"missing one of {names}")


def parse_header(row: Dict[str, str]) -> Pose:
    frame_valid = as_bool(row.get("frame_valid", row.get("valid", "1")))

    points = {}
    for name in POINT_ORDER:
        points[name] = Point(
            get_float(row, f"{name}_x", f"x_{name}"),
            get_float(row, f"{name}_y", f"y_{name}"),
            as_bool(row.get(f"{name}_valid", row.get(f"valid_{name}", "1"))),
        )

    return Pose(frame_valid, points)


def load_csv(path: str) -> List[Pose]:
    with open(path, newline="", encoding="utf-8-sig") as f:
        rows = [
            [c.strip() for c in r]
            for r in csv.reader(f)
            if r and not r[0].lstrip().startswith("#")
        ]

    if not rows:
        return []

    try:
        float(rows[0][0])
        header = False
    except Exception:
        header = True

    if not header:
        return [parse_positional(r) for r in rows]

    with open(path, newline="", encoding="utf-8-sig") as f:
        out = []
        for r in csv.DictReader(f):
            if not r:
                continue
            norm = {
                str(k).strip(): (v.strip() if isinstance(v, str) else v)
                for k, v in r.items()
                if k is not None
            }
            out.append(parse_header(norm))
        return out


def make_packet(pose: Pose, frame_id: int) -> bytes:
    mask = 0
    coords = []

    for name in POINT_ORDER:
        p = pose.points[name]
        x, x_ok = pixel(p.x, WIDTH)
        y, y_ok = pixel(p.y, HEIGHT)

        valid = pose.valid and p.valid and x_ok and y_ok
        if valid:
            mask |= VALID_BITS[name]
        else:
            x = 0
            y = 0

        coords.extend([x, y])

    payload = struct.pack(
        "<IBB" + ("HH" * 6),
        frame_id & 0xFFFFFFFF,
        1 if pose.valid else 0,
        mask,
        *coords
    )

    packet = bytearray([SYNC0, SYNC1, VERSION, PAYLOAD_SIZE])
    packet.extend(payload)
    packet.extend(struct.pack("<H", crc16_ccitt(packet[2:])))

    assert len(packet) == PACKET_SIZE
    return bytes(packet)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", required=True)
    ap.add_argument("--output", required=True)
    ap.add_argument("--start-frame-id", type=int, default=1)
    args = ap.parse_args()

    poses = load_csv(args.csv)
    if not poses:
        raise SystemExit("no pose rows")

    with open(args.output, "wb") as f:
        frame_id = args.start_frame_id
        for pose in poses:
            f.write(make_packet(pose, frame_id))
            frame_id = (frame_id + 1) & 0xFFFFFFFF

    print(f"[OK] poses       : {len(poses)}")
    print(f"[OK] packet size : {PACKET_SIZE} bytes")
    print(f"[OK] stream bytes: {len(poses) * PACKET_SIZE}")
    print(f"[OK] output      : {args.output}")


if __name__ == "__main__":
    main()
