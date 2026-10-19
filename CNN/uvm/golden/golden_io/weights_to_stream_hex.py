import argparse
import hashlib
import json
from pathlib import Path


BEAT_BYTES = 8


def convert(
    weights_path: Path,
    manifest_path: Path,
    output_dir: Path
):
    blob = weights_path.read_bytes()

    manifest = json.loads(
        manifest_path.read_text()
    )

    # ---------------------------------------------------------
    # Packed weights contract check
    # ---------------------------------------------------------
    expected_bytes = manifest["packed_bytes"]

    if len(blob) != expected_bytes:
        raise ValueError(
            f"packed size mismatch: "
            f"{len(blob)} != {expected_bytes}"
        )

    actual_sha256 = hashlib.sha256(blob).hexdigest()
    expected_sha256 = manifest["packed_sha256"]

    if actual_sha256 != expected_sha256:
        raise ValueError(
            "packed SHA256 mismatch\n"
            f"expected: {expected_sha256}\n"
            f"actual  : {actual_sha256}"
        )

    ops = manifest["ops"]

    if len(ops) != 29:
        raise ValueError(
            f"expected 29 ops, got {len(ops)}"
        )

    output_dir.mkdir(
        parents=True,
        exist_ok=True
    )

    total_beats = 0
    cursor = 0

    print("========================================")
    print("WEIGHT stream conversion")
    print("========================================")

    for op in ops:

        op_id = op["op_id"]
        name = op["name"]

        offset = op["weight_offset"]
        dma_bytes = op["dma_bytes"]

        # weights_v4.bin should be packed sequentially.
        if offset != cursor:
            raise ValueError(
                f"op {op_id}: non-contiguous offset "
                f"{offset} != {cursor}"
            )

        if dma_bytes % BEAT_BYTES != 0:
            raise ValueError(
                f"op {op_id}: dma_bytes "
                f"{dma_bytes} is not 8-byte aligned"
            )

        end = offset + dma_bytes

        payload = blob[offset:end]

        if len(payload) != dma_bytes:
            raise ValueError(
                f"op {op_id}: payload truncated"
            )

        beat_count = dma_bytes // BEAT_BYTES

        output_path = (
            output_dir /
            f"op_{op_id:02d}.hex"
        )

        with output_path.open("w") as f:

            for beat_idx in range(beat_count):

                start = beat_idx * BEAT_BYTES

                chunk = payload[
                    start:start + BEAT_BYTES
                ]

                # First DMA byte -> data[7:0]
                data64 = int.from_bytes(
                    chunk,
                    byteorder="little",
                    signed=False
                )

                # TLAST only on final beat of this OP load.
                last = (
                    beat_idx == beat_count - 1
                )

                f.write(
                    f"{data64:016X} {int(last)}\n"
                )

        print(
            f"OP {op_id:02d} | "
            f"{name:<30} | "
            f"bytes={dma_bytes:6d} | "
            f"beats={beat_count:6d}"
        )

        total_beats += beat_count
        cursor = end

    if cursor != len(blob):
        raise ValueError(
            f"final cursor mismatch: "
            f"{cursor} != {len(blob)}"
        )

    print("========================================")
    print("WEIGHT stream conversion PASS")
    print(f"packed bytes : {len(blob)}")
    print(f"total beats  : {total_beats}")
    print(f"ops          : {len(ops)}")
    print(f"output dir   : {output_dir}")
    print("========================================")


def main():

    parser = argparse.ArgumentParser()

    parser.add_argument(
        "weights",
        type=Path,
        help="weights_v4.bin"
    )

    parser.add_argument(
        "manifest",
        type=Path,
        help="manifest.json"
    )

    parser.add_argument(
        "output_dir",
        type=Path,
        help="output directory"
    )

    args = parser.parse_args()

    convert(
        args.weights,
        args.manifest,
        args.output_dir
    )


if __name__ == "__main__":
    main()