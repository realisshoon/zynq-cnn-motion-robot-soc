#!/usr/bin/env python3
"""Add reproducible pixel jitter to a HumanPose2D CSV without changing its timing.

The input is the CSV produced by extract_real_person_pose_v3.py. Only valid
landmarks in valid frames are perturbed; frame IDs, timestamps, validity flags,
and any extra columns are preserved. Test the result offline before driving a
physical robot with it.
"""

import argparse
import csv
import math
import random
from pathlib import Path


POINTS = ("shoulder_l", "shoulder_r", "elbow", "wrist", "finger1", "finger2")
REQUIRED = ("frame_id", "time_sec", "frame_valid") + tuple(
    f"{point}_{suffix}" for point in POINTS for suffix in ("x", "y", "valid")
)


def probability(value: str) -> float:
    result = float(value)
    if not math.isfinite(result) or not 0.0 <= result <= 1.0:
        raise argparse.ArgumentTypeError("probability must be between 0 and 1")
    return result


def nonnegative_float(value: str) -> float:
    result = float(value)
    if not math.isfinite(result) or result < 0.0:
        raise argparse.ArgumentTypeError("value must be finite and nonnegative")
    return result


def positive_int(value: str) -> int:
    result = int(value)
    if result <= 0:
        raise argparse.ArgumentTypeError("value must be positive")
    return result


def make_noisy_rows(rows, points, rng, sigma, spike_prob, spike_sigma, width, height):
    changed_points = 0
    spikes = 0
    clamped = 0
    for row_number, row in enumerate(rows, start=2):
        if row["frame_valid"] != "1":
            continue
        for point in points:
            if row[f"{point}_valid"] != "1":
                continue
            try:
                x = float(row[f"{point}_x"])
                y = float(row[f"{point}_y"])
            except ValueError as exc:
                raise ValueError(f"row {row_number}: invalid {point} coordinate") from exc
            if not math.isfinite(x) or not math.isfinite(y):
                raise ValueError(f"row {row_number}: non-finite {point} coordinate")

            dx = rng.gauss(0.0, sigma)
            dy = rng.gauss(0.0, sigma)
            if rng.random() < spike_prob:
                dx += rng.gauss(0.0, spike_sigma)
                dy += rng.gauss(0.0, spike_sigma)
                spikes += 1

            noisy_x = min(max(x + dx, 0.0), float(width - 1))
            noisy_y = min(max(y + dy, 0.0), float(height - 1))
            clamped += int(noisy_x != x + dx) + int(noisy_y != y + dy)
            row[f"{point}_x"] = f"{noisy_x:.3f}"
            row[f"{point}_y"] = f"{noisy_y:.3f}"
            changed_points += 1
    return changed_points, spikes, clamped


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-csv", required=True, type=Path)
    parser.add_argument("--output-csv", required=True, type=Path)
    parser.add_argument("--sigma-px", type=nonnegative_float, default=3.0,
                        help="Gaussian jitter standard deviation in pixels (default: 3)")
    parser.add_argument("--seed", type=int, default=26,
                        help="fixed random seed for repeatable experiments (default: 26)")
    parser.add_argument("--points", nargs="+", choices=POINTS, default=POINTS,
                        help="landmarks to perturb (default: all six)")
    parser.add_argument("--spike-prob", type=probability, default=0.0,
                        help="chance of an extra large jump per valid landmark/frame (default: 0)")
    parser.add_argument("--spike-sigma-px", type=nonnegative_float, default=30.0,
                        help="extra Gaussian deviation when a spike occurs (default: 30 px)")
    parser.add_argument("--width", type=positive_int, default=1280)
    parser.add_argument("--height", type=positive_int, default=720)
    parser.add_argument("--overwrite", action="store_true",
                        help="replace an existing generated output CSV")
    args = parser.parse_args()

    source = args.input_csv.resolve()
    target = args.output_csv.resolve()
    if source == target:
        parser.error("input and output must be different files")
    if target.exists() and not args.overwrite:
        parser.error(f"output already exists; refusing to overwrite: {target}")
    if not source.is_file():
        parser.error(f"input CSV not found: {source}")
    if not target.parent.is_dir():
        parser.error(f"output directory not found: {target.parent}")

    with source.open(newline="", encoding="utf-8-sig") as handle:
        reader = csv.DictReader(handle)
        fields = reader.fieldnames
        if fields is None or any(field not in fields for field in REQUIRED):
            parser.error("input is not an extract_real_person_pose_v3 HumanPose2D CSV")
        rows = list(reader)
    if not rows:
        parser.error("input CSV has no frames")

    changed, spikes, clamped = make_noisy_rows(
        rows, tuple(dict.fromkeys(args.points)), random.Random(args.seed),
        args.sigma_px, args.spike_prob, args.spike_sigma_px,
        args.width, args.height,
    )
    with target.open("w" if args.overwrite else "x", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)

    print(f"[OK] {target}: {len(rows)} frames, {changed} landmarks perturbed, "
          f"{spikes} spikes, {clamped} coordinates clamped "
          f"(sigma={args.sigma_px:g}px, seed={args.seed})")


if __name__ == "__main__":
    main()
