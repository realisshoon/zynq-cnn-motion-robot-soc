#!/usr/bin/env python3
"""Convert a legacy pose CSV (finger1=index) to finger1=thumb, finger2=index.

This only swaps the two finger coordinate/valid triples. It leaves the input
unchanged so historical replays and logs keep their original meaning.
"""

import argparse
import csv
from pathlib import Path


FINGER_FIELDS = tuple(
    f"finger{number}_{field}"
    for number in (1, 2)
    for field in ("x", "y", "valid")
)


def convert(source: Path, destination: Path) -> int:
    if source.resolve() == destination.resolve():
        raise ValueError("input and output must be different files")
    if destination.exists():
        raise FileExistsError(f"output already exists: {destination}")

    with source.open(newline="", encoding="utf-8-sig") as stream:
        reader = csv.DictReader(stream)
        fields = reader.fieldnames
        if fields is None or any(field not in fields for field in FINGER_FIELDS):
            raise ValueError(f"missing finger coordinate/valid columns: {source}")
        rows = list(reader)

    for row in rows:
        for field in ("x", "y", "valid"):
            left = f"finger1_{field}"
            right = f"finger2_{field}"
            row[left], row[right] = row[right], row[left]

    with destination.open("x", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    return len(rows)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    print(f"[OK] {convert(args.input, args.output)} rows: {args.output}")


if __name__ == "__main__":
    main()
