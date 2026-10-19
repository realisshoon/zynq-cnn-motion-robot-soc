#!/usr/bin/env python3

import argparse
from pathlib import Path

import numpy as np


FRAME_H = 720
FRAME_W = 1280
CHANNELS = 3

ROW_STEP = 5
ROW_BYTES = FRAME_W * CHANNELS       # 3840 bytes
BEAT_BYTES = 8                       # AXI-stream 64-bit
BEATS_PER_ROW = ROW_BYTES // BEAT_BYTES   # 480
NUM_DMA_ROWS = FRAME_H // ROW_STEP        # 144
TOTAL_BEATS = NUM_DMA_ROWS * BEATS_PER_ROW  # 69120
TOTAL_BYTES = NUM_DMA_ROWS * ROW_BYTES      # 552960


def convert(input_path: Path, output_path: Path) -> None:
    frame = np.load(input_path, allow_pickle=False)

    # ------------------------------------------------------------
    # Input contract check
    # ------------------------------------------------------------
    if frame.shape != (FRAME_H, FRAME_W, CHANNELS):
        raise ValueError(
            f"Invalid frame shape: {frame.shape}, "
            f"expected {(FRAME_H, FRAME_W, CHANNELS)}"
        )

    if frame.dtype != np.uint8:
        raise ValueError(
            f"Invalid dtype: {frame.dtype}, expected uint8"
        )

    output_path.parent.mkdir(parents=True, exist_ok=True)

    beat_count = 0
    row_count = 0

    with output_path.open("w") as f:
        # IMAGE SG DMA sends source rows:
        # 0, 5, 10, ..., 715
        for src_row in range(0, FRAME_H, ROW_STEP):
            row = frame[src_row]

            # HWC RGB:
            # [R0,G0,B0,R1,G1,B1,...]
            row_bytes = row.reshape(-1).tobytes()

            if len(row_bytes) != ROW_BYTES:
                raise RuntimeError(
                    f"Row {src_row}: got {len(row_bytes)} bytes, "
                    f"expected {ROW_BYTES}"
                )

            for beat_idx in range(BEATS_PER_ROW):
                start = beat_idx * BEAT_BYTES
                chunk = row_bytes[start:start + BEAT_BYTES]

                # AXI byte lane mapping:
                # first stream byte -> data[7:0]
                # second byte       -> data[15:8]
                # ...
                data64 = int.from_bytes(chunk, byteorder="little")

                # One 64-bit word per line.
                f.write(f"{data64:016X}\n")

                beat_count += 1

            row_count += 1

    # ------------------------------------------------------------
    # Self checks
    # ------------------------------------------------------------
    if row_count != NUM_DMA_ROWS:
        raise RuntimeError(
            f"DMA row count mismatch: {row_count} != {NUM_DMA_ROWS}"
        )

    if beat_count != TOTAL_BEATS:
        raise RuntimeError(
            f"Beat count mismatch: {beat_count} != {TOTAL_BEATS}"
        )

    print("IMAGE stream conversion PASS")
    print(f"input       : {input_path}")
    print(f"output      : {output_path}")
    print(f"frame shape : {frame.shape}")
    print(f"dtype       : {frame.dtype}")
    print(f"DMA rows    : {row_count}")
    print(f"bytes/row   : {ROW_BYTES}")
    print(f"beats/row   : {BEATS_PER_ROW}")
    print(f"total bytes : {TOTAL_BYTES}")
    print(f"total beats : {beat_count}")
    print("keep        : 0xFF for every beat")
    print("last        : asserted on beat 479 of every DMA row")


def main():
    parser = argparse.ArgumentParser(
        description=(
            "Convert 720x1280 RGB uint8 .npy frame "
            "to CNN IMAGE DMA 64-bit stream hex."
        )
    )

    parser.add_argument(
        "input",
        type=Path,
        help="Input frame .npy"
    )

    parser.add_argument(
        "output",
        type=Path,
        help="Output 64-bit hex stream file"
    )

    args = parser.parse_args()

    convert(args.input, args.output)


if __name__ == "__main__":
    main()