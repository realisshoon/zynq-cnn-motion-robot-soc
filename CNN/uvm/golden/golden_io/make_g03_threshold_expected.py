#!/usr/bin/env python3

import argparse
import importlib.util
import json
from pathlib import Path

import numpy as np


def load_model(path):
    spec = importlib.util.spec_from_file_location("cnn_model_v4", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def write_expected(path, joints):
    flags = 0

    with path.open("w") as f:
        for j in joints:
            f.write(f"{j['word'] & 0xffffffff:08X}\n")

            if j["valid"]:
                flags |= 1 << j["joint"]

        f.write(f"{flags:08X}\n")

    return flags


def main():
    parser = argparse.ArgumentParser()

    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--heatmap", type=Path, required=True)
    parser.add_argument("--offset", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)

    args = parser.parse_args()

    model = load_model(args.model)

    heat = np.load(args.heatmap, allow_pickle=False)
    offset = np.load(args.offset, allow_pickle=False)

    # threshold=-128이면 score 조건은 항상 만족하므로
    # 여기서 valid인 joint는 좌표 bounds 안에 있는 joint다.
    bounds_only = model.decode(
        heat,
        offset,
        threshold=-128,
    )

    selected = None

    for j in bounds_only:
        score = int(j["score_raw"])

        if j["valid"] and score < 127:
            selected = j
            break

    if selected is None:
        raise RuntimeError(
            "No in-bounds joint with score < 127 found"
        )

    joint_id = int(selected["joint"])
    score = int(selected["score_raw"])

    threshold_equal = score
    threshold_plus1 = score + 1

    equal_results = model.decode(
        heat,
        offset,
        threshold=threshold_equal,
    )

    plus1_results = model.decode(
        heat,
        offset,
        threshold=threshold_plus1,
    )

    args.out_dir.mkdir(parents=True, exist_ok=True)

    equal_path = args.out_dir / "g03_threshold_equal.hex"
    plus1_path = args.out_dir / "g03_threshold_plus1.hex"

    equal_flags = write_expected(equal_path, equal_results)
    plus1_flags = write_expected(plus1_path, plus1_results)

    eq = equal_results[joint_id]
    p1 = plus1_results[joint_id]

    info = {
        "joint": joint_id,
        "score_signed": score,

        "threshold_equal_signed": threshold_equal,
        "threshold_equal_hex": f"{threshold_equal & 0xff:02X}",

        "threshold_plus1_signed": threshold_plus1,
        "threshold_plus1_hex": f"{threshold_plus1 & 0xff:02X}",

        "equal_valid": bool(eq["valid"]),
        "equal_word": f"{eq['word'] & 0xffffffff:08X}",

        "plus1_valid": bool(p1["valid"]),
        "plus1_word": f"{p1['word'] & 0xffffffff:08X}",

        "equal_flags": f"{equal_flags:08X}",
        "plus1_flags": f"{plus1_flags:08X}",
    }

    (args.out_dir / "g03_case.json").write_text(
        json.dumps(info, indent=2)
    )

    print("==========================================")
    print("G03 THRESHOLD BOUNDARY CASE")
    print("==========================================")
    print(f"joint              : {joint_id}")
    print(f"score               : {score}")
    print(
        f"threshold == score  : "
        f"{threshold_equal} / 0x{threshold_equal & 0xff:02X}"
    )
    print(
        f"threshold == score+1: "
        f"{threshold_plus1} / 0x{threshold_plus1 & 0xff:02X}"
    )
    print("------------------------------------------")
    print(
        f"equal case valid    : {eq['valid']} "
        f"word=0x{eq['word'] & 0xffffffff:08X}"
    )
    print(
        f"plus1 case valid    : {p1['valid']} "
        f"word=0x{p1['word'] & 0xffffffff:08X}"
    )
    print("==========================================")


if __name__ == "__main__":
    main()