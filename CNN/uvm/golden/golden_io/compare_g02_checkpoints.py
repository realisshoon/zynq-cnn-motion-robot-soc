#!/usr/bin/env python3

import argparse
from pathlib import Path

import numpy as np


OP_COUNT = 29


def signed_value(value, bits):
    sign = 1 << (bits - 1)
    mask = (1 << bits) - 1

    value &= mask

    if value & sign:
        value -= 1 << bits

    return value


def tag_fields(tag):
    col = tag & 0xFF
    row = (tag >> 8) & 0xFF
    batch = (tag >> 16) & 0xF
    group = (tag >> 20) & 0x7F
    op_id = (tag >> 33) & 0x1F

    return row, col, batch, group, op_id


def find_golden_file(feature_dir, op_id):
    matches = sorted(feature_dir.glob(f"{op_id:02d}_*.npy"))

    if len(matches) != 1:
        raise RuntimeError(
            f"OP{op_id:02d}: expected one Golden NPY, found {len(matches)}"
        )

    return matches[0]


def compare_op(op_id, dump_path, golden_path, max_examples):
    golden = np.load(golden_path, allow_pickle=False)

    if golden.ndim != 3:
        raise RuntimeError(
            f"OP{op_id:02d}: expected HWC tensor, shape={golden.shape}"
        )

    height, width, channels = golden.shape

    seen = np.zeros(golden.shape, dtype=np.bool_)

    compared = 0
    matched = 0
    mismatched = 0
    duplicate = 0
    bad_tag = 0
    bad_coord = 0

    examples = []

    with dump_path.open("r") as f:
        for line_no, line in enumerate(f, 1):
            line = line.strip()

            if not line:
                continue

            fields = line.split()

            if len(fields) != 3:
                raise RuntimeError(
                    f"{dump_path}:{line_no}: expected TAG MASK DATA"
                )

            tag = int(fields[0], 16)
            mask = int(fields[1], 16)
            data = int(fields[2], 16)

            row, col, batch, group, tag_op_id = tag_fields(tag)

            if tag_op_id != op_id:
                bad_tag += 1

                if len(examples) < max_examples:
                    examples.append(
                        f"bad tag: line={line_no} "
                        f"tag_op={tag_op_id} expected_op={op_id}"
                    )

                continue

            if row >= height or col >= width:
                bad_coord += 1

                if len(examples) < max_examples:
                    examples.append(
                        f"bad coord: line={line_no} "
                        f"row={row} col={col} "
                        f"shape={golden.shape}"
                    )

                continue

            if op_id == 0:
                lane_count = 4
                channel_base = group * 4

                for lane in range(lane_count):
                    if ((mask >> lane) & 1) == 0:
                        continue

                    channel = channel_base + lane

                    if channel >= channels:
                        bad_coord += 1
                        continue

                    raw = (data >> (lane * 16)) & 0xFFFF

                    rtl_value = raw & 0xFF
                    golden_value = int(golden[row, col, channel])

                    compared += 1

                    if seen[row, col, channel]:
                        duplicate += 1
                    else:
                        seen[row, col, channel] = True

                    if rtl_value == golden_value:
                        matched += 1
                    else:
                        mismatched += 1

                        if len(examples) < max_examples:
                            examples.append(
                                f"[{row},{col},{channel}] "
                                f"rtl={rtl_value} golden={golden_value} "
                                f"raw=0x{raw:04x}"
                            )

            elif op_id & 1 and op_id <= 25:
                lane_count = 32
                channel_base = batch * 32

                for lane in range(lane_count):
                    if ((mask >> lane) & 1) == 0:
                        continue

                    channel = channel_base + lane

                    if channel >= channels:
                        bad_coord += 1
                        continue

                    rtl_value = (data >> (lane * 8)) & 0xFF
                    golden_value = int(golden[row, col, channel])

                    compared += 1

                    if seen[row, col, channel]:
                        duplicate += 1
                    else:
                        seen[row, col, channel] = True

                    if rtl_value == golden_value:
                        matched += 1
                    else:
                        mismatched += 1

                        if len(examples) < max_examples:
                            examples.append(
                                f"[{row},{col},{channel}] "
                                f"rtl={rtl_value} golden={golden_value}"
                            )

            else:
                lane_count = 4
                channel_base = group * 4

                for lane in range(lane_count):
                    if ((mask >> lane) & 1) == 0:
                        continue

                    channel = channel_base + lane

                    if channel >= channels:
                        bad_coord += 1
                        continue

                    raw = (data >> (lane * 16)) & 0xFFFF

                    if op_id == 27:
                        rtl_value = signed_value(raw, 16)
                        golden_value = int(golden[row, col, channel])

                    elif op_id == 28:
                        rtl_value = signed_value(raw, 16)
                        golden_value = int(golden[row, col, channel])

                    else:
                        rtl_value = raw & 0xFF
                        golden_value = int(golden[row, col, channel])

                    compared += 1

                    if seen[row, col, channel]:
                        duplicate += 1
                    else:
                        seen[row, col, channel] = True

                    if rtl_value == golden_value:
                        matched += 1
                    else:
                        mismatched += 1

                        if len(examples) < max_examples:
                            examples.append(
                                f"[{row},{col},{channel}] "
                                f"rtl={rtl_value} golden={golden_value} "
                                f"raw=0x{raw:04x}"
                            )

    expected = int(golden.size)
    missing = int(expected - np.count_nonzero(seen))

    passed = (
        compared == expected
        and matched == expected
        and mismatched == 0
        and missing == 0
        and duplicate == 0
        and bad_tag == 0
        and bad_coord == 0
    )

    return {
        "passed": passed,
        "shape": golden.shape,
        "dtype": str(golden.dtype),
        "expected": expected,
        "compared": compared,
        "matched": matched,
        "mismatched": mismatched,
        "missing": missing,
        "duplicate": duplicate,
        "bad_tag": bad_tag,
        "bad_coord": bad_coord,
        "examples": examples,
    }


def main():
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--rtl-dir",
        type=Path,
        required=True,
    )

    parser.add_argument(
        "--golden-dir",
        type=Path,
        required=True,
    )

    parser.add_argument(
        "--max-examples",
        type=int,
        default=8,
    )

    args = parser.parse_args()

    total_expected = 0
    total_matched = 0
    pass_count = 0

    print("============================================================")
    print("G02 FULL 29-OP BIT-EXACT CHECK")
    print("============================================================")

    for op_id in range(OP_COUNT):
        dump_path = args.rtl_dir / f"op_{op_id:02d}.hex"
        golden_path = find_golden_file(args.golden_dir, op_id)

        if not dump_path.exists():
            raise FileNotFoundError(
                f"Missing RTL checkpoint: {dump_path}"
            )

        result = compare_op(
            op_id,
            dump_path,
            golden_path,
            args.max_examples,
        )

        total_expected += result["expected"]
        total_matched += result["matched"]

        if result["passed"]:
            pass_count += 1

            print(
                f"OP{op_id:02d} PASS "
                f"{result['matched']}/{result['expected']} "
                f"shape={result['shape']} "
                f"dtype={result['dtype']}"
            )

        else:
            print(
                f"OP{op_id:02d} FAIL "
                f"matched={result['matched']}/{result['expected']} "
                f"mismatch={result['mismatched']} "
                f"missing={result['missing']} "
                f"duplicate={result['duplicate']} "
                f"bad_tag={result['bad_tag']} "
                f"bad_coord={result['bad_coord']}"
            )

            for example in result["examples"]:
                print(f"    {example}")

    print("============================================================")
    print(f"G02 RESULT : {pass_count}/29 OP PASS")
    print(
        f"ELEMENTS   : "
        f"{total_matched}/{total_expected} bit-exact matched"
    )
    print("============================================================")

    if pass_count != OP_COUNT or total_matched != total_expected:
        raise SystemExit(1)


if __name__ == "__main__":
    main()