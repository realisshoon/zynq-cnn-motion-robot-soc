#!/usr/bin/env python3
"""Simulate missing finger landmarks in a HumanPose2D CSV.

Every frame has finger1/finger2 coordinates set to zero and validity set to 0.
Frame timing and all shoulder, elbow, and wrist fields are preserved.
"""

import argparse
import csv
from pathlib import Path


FINGER_FIELDS = (
    "finger1_x", "finger1_y", "finger1_valid",
    "finger2_x", "finger2_y", "finger2_valid",
)
REQUIRED_FIELDS = (
    "frame_id", "time_sec", "frame_valid",
    "shoulder_l_x", "shoulder_l_y", "shoulder_l_valid",
    "shoulder_r_x", "shoulder_r_y", "shoulder_r_valid",
    "elbow_x", "elbow_y", "elbow_valid",
    "wrist_x", "wrist_y", "wrist_valid",
) + FINGER_FIELDS


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-csv", required=True, type=Path)
    parser.add_argument("--output-csv", required=True, type=Path)
    parser.add_argument("--overwrite", action="store_true",
                        help="replace an existing generated output CSV")
    args = parser.parse_args()

    source = args.input_csv.resolve()
    target = args.output_csv.resolve()
    if source == target:
        parser.error("input and output must be different files")
    if not source.is_file():
        parser.error(f"input CSV not found: {source}")
    if not target.parent.is_dir():
        parser.error(f"output directory not found: {target.parent}")
    if target.exists() and not args.overwrite:
        parser.error(f"output already exists; refusing to overwrite: {target}")

    with source.open(newline="", encoding="utf-8-sig") as input_file:
        reader = csv.DictReader(input_file)
        fields = reader.fieldnames
        if fields is None or any(field not in fields for field in REQUIRED_FIELDS):
            parser.error("input is not a HumanPose2D CSV with finger1/finger2 fields")
        if len(fields) != len(set(fields)):
            parser.error("input CSV contains duplicate column names")
        with target.open("w" if args.overwrite else "x", newline="", encoding="utf-8") as output_file:
            writer = csv.DictWriter(output_file, fieldnames=fields)
            writer.writeheader()
            count = 0
            for row in reader:
                row["finger1_x"] = row["finger1_y"] = "0.0"
                row["finger2_x"] = row["finger2_y"] = "0.0"
                row["finger1_valid"] = row["finger2_valid"] = "0"
                writer.writerow(row)
                count += 1

    print(f"[OK] {target}: {count} frames, finger1/finger2 invalid in every frame")


if __name__ == "__main__":
    main()
