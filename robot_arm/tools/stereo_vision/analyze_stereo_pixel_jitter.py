#!/usr/bin/env python3
"""Compare RAW and processed PIX coordinates on explicitly identified PAIR rows."""

import argparse
from collections import Counter
import csv
import json
import math
from pathlib import Path
import statistics
import sys


POINTS = {
    "lshoulder": "sl", "rshoulder": "sr", "elbow": "e",
    "wrist": "w", "red": "red", "green": "green",
}
SIDES = ("left", "right")
SCHEMAS = {
    "RAW": "t_ms,sid,seq,fid,slx,sly,sls,slv,srx,sry,srs,srv,ex,ey,es,ev,wx,wy,ws,wv,redx,redy,redv,greenx,greeny,greenv",
    "PAIR": "t_ms,rsid,pair,lsid,lseq,lfid,rseq,rfid,sync,gap_us,control,async,es,ws,red_s,green_s",
    "PIX": "rsid,pair,side,sid,seq,fid,slx,sly,slv,srx,sry,srv,ex,ey,ev,wx,wy,wv,redx,redy,redv,greenx,greeny,greenv",
    "PG": "rsid,pair,accepted,reason",
}
SCHEMAS = {tag: fields.split(",") for tag, fields in SCHEMAS.items()}
COORDINATES = {prefix + axis for prefix in POINTS.values() for axis in ("x", "y")}
FLAGS = {prefix + "v" for prefix in POINTS.values()} | {"sync", "accepted"}
UNSIGNED = {"t_ms", "sid", "rsid", "lsid", "seq", "lseq", "rseq",
            "fid", "lfid", "rfid", "pair", "gap_us"}
STATUS_FIELDS = ("control", "async", "es", "ws", "red_s", "green_s")


def parse_record(tag, fields):
    if len(fields) != len(SCHEMAS[tag]) + 1:
        raise ValueError("Wrong field count")
    record = {}
    for name, value in zip(SCHEMAS[tag], fields[1:]):
        value = value.strip()
        if name == "side":
            value = {"L": "left", "R": "right", "LEFT": "left", "RIGHT": "right"}[value.upper()]
        elif name == "reason":
            if not value:
                raise ValueError("Empty admission reason")
        elif name in COORDINATES:
            value = float(value)
        else:
            value = int(value)
            if name in FLAGS and value not in (0, 1):
                raise ValueError("Invalid flag")
            if name in UNSIGNED and value < 0:
                raise ValueError("Negative identity or time")
        record[name] = value
    return record


def load_log(path, side):
    records = {tag: {} for tag in SCHEMAS}
    counts = {"lines": 0, "ignored": 0, "misplaced": 0,
              "seen": Counter(), "malformed": Counter(),
              "duplicates": Counter(), "conflicts": Counter()}
    with path.open(encoding="utf-8-sig", errors="replace") as stream:
        for line in stream:
            counts["lines"] += 1
            line = line.strip()
            tag = line.split(",", 1)[0]
            if tag not in SCHEMAS:
                counts["ignored"] += 1
                continue
            counts["seen"][tag] += 1
            if side == "left" and tag != "RAW":
                counts["misplaced"] += 1
                continue
            try:
                record = parse_record(tag, next(csv.reader([line], strict=True)))
            except (ValueError, KeyError, csv.Error):
                counts["malformed"][tag] += 1
                continue
            if tag == "RAW":
                key = (side, record["sid"], record["seq"], record["fid"])
            else:
                key = (record["rsid"], record["pair"])
                if tag == "PIX":
                    key += (record["side"],)
            table = records[tag]
            if key not in table:
                table[key] = record
            else:
                counts["duplicates"][tag] += 1
                if table[key] is not None and table[key] != record:
                    table[key] = None
                    counts["conflicts"][tag] += 1
    return records, counts


def percentile95(values):
    if not values:
        return None
    ordered = sorted(values)
    position = 0.95 * (len(ordered) - 1)
    lower = math.floor(position)
    upper = math.ceil(position)
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def point_statistics(samples):
    """Population spread; radial steps require adjacent valid pairs in one boot."""
    valid = [point for _, point in samples if point not in (None, "invalid")]
    steps = []
    previous = None
    for identity, point in samples:
        if point in (None, "invalid"):
            previous = None
            continue
        if previous is not None:
            old_identity, old_point = previous
            if (identity[0] == old_identity[0] and identity[2] == old_identity[2]
                    and identity[1] == old_identity[1] + 1):
                steps.append(math.hypot(point[0] - old_point[0], point[1] - old_point[1]))
        previous = (identity, point)
    result = {
        "n": len(valid), "invalid": sum(point == "invalid" for _, point in samples),
        "missing": sum(point is None for _, point in samples),
        "std_x": None, "std_y": None, "span_x": None, "span_y": None,
        "step_n": len(steps), "step_p95": percentile95(steps),
        "step_max": max(steps, default=None),
    }
    if valid:
        for axis, index in (("x", 0), ("y", 1)):
            values = [point[index] for point in valid]
            result["std_" + axis] = statistics.pstdev(values)
            result["span_" + axis] = max(values) - min(values)
    return result


def source_windows(pairs):
    windows = []
    for rsid in dict.fromkeys(key[0] for key, _ in pairs):
        rows = [row for key, row in pairs if key[0] == rsid]
        window = {
            "right_session": rsid, "first_pair": min(row["pair"] for row in rows),
            "last_pair": max(row["pair"] for row in rows), "pairs": len(rows),
            "t_ms_min": min(row["t_ms"] for row in rows),
            "t_ms_max": max(row["t_ms"] for row in rows),
        }
        for side in SIDES:
            session_field, sequence_field, frame_field = (
                ("lsid", "lseq", "lfid") if side == "left" else ("rsid", "rseq", "rfid"))
            window[side] = []
            for sid in dict.fromkeys(row[session_field] for row in rows):
                group = [row for row in rows if row[session_field] == sid]
                window[side].append({
                    "session": sid,
                    "seq_min": min(row[sequence_field] for row in group),
                    "seq_max": max(row[sequence_field] for row in group),
                    "fid_min": min(row[frame_field] for row in group),
                    "fid_max": max(row[frame_field] for row in group),
                })
        windows.append(window)
    return windows


def analyze(session, right_session=None, start_pair=None, end_pair=None):
    if any(value is not None and value < 0 for value in (right_session, start_pair, end_pair)):
        raise ValueError("Session and pair bounds must be nonnegative")
    if start_pair is not None and end_pair is not None and start_pair > end_pair:
        raise ValueError("start-pair must not exceed end-pair")
    session = Path(session).resolve()
    left, left_counts = load_log(session / "left_uart.log", "left")
    right, right_counts = load_log(session / "right_uart.log", "right")
    raw = {**left["RAW"], **right["RAW"]}
    pairs = right["PAIR"]
    selected = [(key, row) for key, row in pairs.items()
                if (right_session is None or key[0] == right_session)
                and (start_pair is None or key[1] >= start_pair)
                and (end_pair is None or key[1] <= end_pair)]
    session_order = {sid: index for index, sid in enumerate(dict.fromkeys(key[0] for key in pairs))}
    selected.sort(key=lambda entry: (session_order[entry[0][0]], entry[0][1]))
    identified = [(key, row) for key, row in selected if row is not None]
    series = {source: {side: {name: [] for name in POINTS} for side in SIDES}
              for source in ("RAW", "PIX")}
    links = Counter({"complete_pairs": 0})
    for side in SIDES:
        for source in ("RAW", "PIX"):
            links["missing_" + source + "_" + side] = 0
        links["PIX_identity_mismatch_" + side] = 0
    used_raw = set()
    statuses = {name: Counter() for name in STATUS_FIELDS}
    sync = {"verified_by_flag": 0, "not_verified": 0}
    admission = {"accepted": 0, "rejected": 0, "missing": 0,
                 "accepted_reasons": Counter(), "rejected_reasons": Counter()}
    for key, row in selected:
        gate = right["PG"].get(key)
        if gate is None:
            admission["missing"] += 1
        else:
            outcome = "accepted" if gate["accepted"] else "rejected"
            admission[outcome] += 1
            admission[outcome + "_reasons"][gate["reason"]] += 1
        if row is not None:
            for name in STATUS_FIELDS:
                statuses[name][str(row[name])] += 1
            sync["verified_by_flag" if row["sync"] else "not_verified"] += 1
        complete = row is not None
        for side in SIDES:
            identity = None
            raw_record = pix_record = None
            if row is not None:
                identity = ((row["lsid"], row["lseq"], row["lfid"]) if side == "left"
                            else (row["rsid"], row["rseq"], row["rfid"]))
                raw_key = (side,) + identity
                used_raw.add(raw_key)
                raw_record = raw.get(raw_key)
                pix_record = right["PIX"].get(key + (side,))
                if pix_record is not None and tuple(pix_record[name] for name in ("sid", "seq", "fid")) != identity:
                    links["PIX_identity_mismatch_" + side] += 1
                    pix_record = None
            for source, record in (("RAW", raw_record), ("PIX", pix_record)):
                if record is None:
                    links["missing_" + source + "_" + side] += 1
                    complete = False
                for name, prefix in POINTS.items():
                    point = None
                    if record is not None:
                        point = (record[prefix + "x"], record[prefix + "y"])
                        if not record[prefix + "v"] or not all(math.isfinite(value) for value in point):
                            point = "invalid"
                    sessions = (row["lsid"], row["rsid"]) if row is not None else None
                    series[source][side][name].append(((key[0], key[1], sessions), point))
        links["complete_pairs"] += int(complete)
    links["RAW_outside_selected_pairs"] = len(set(raw) - used_raw)
    links["orphan_PIX"] = sum(key[:2] not in pairs for key in right["PIX"])
    links["orphan_PG"] = sum(key not in pairs for key in right["PG"])
    stats = {source: {side: {name: point_statistics(samples)
                            for name, samples in points.items()}
                      for side, points in sides.items()}
             for source, sides in series.items()}
    observed = any(point["n"] for sides in stats.values()
                   for points in sides.values() for point in points.values())
    return {
        "status": "ok" if observed else ("no_observations" if identified else "no_pairs"),
        "sources": {side: str(session / (side + "_uart.log")) for side in SIDES},
        "window": {"right_session": right_session, "start_pair": start_pair,
                   "end_pair": end_pair, "inclusive": True, "observed": source_windows(identified)},
        "pairs": {"logged": len(pairs), "selected": len(selected),
                  "identified": len(identified), "ambiguous": len(selected) - len(identified)},
        "parse": {"left": left_counts, "right": right_counts},
        "links": dict(links), "pair_status": statuses, "sync": sync,
        "admission": admission, "stats": stats,
        "notes": [
            "Points observed only; physical static state is not verified.",
            "RAW and PIX independently exclude invalid/missing points on the same selected PAIR window; no pairs are inferred.",
            "Population standard deviations and spans are in pixels; radial step p95 uses linear interpolation.",
            "Steps require adjacent pair IDs, valid records, and unchanged LEFT/RIGHT sessions; gaps and resets break steps.",
            "Synchronization is not verified unless PAIR sync=1; this is the firmware flag, not an independent exposure check.",
            "PG is input admission, not A2/PWM success; accepted and rejected pairs both contribute observations.",
        ],
    }


def print_report(report):
    print("Paired pixel jitter:", report["status"])
    if report["status"] == "no_pairs":
        print("No identified pairs in the selected window; no jitter measurements are available.")
        if not report["pairs"]["logged"]:
            print("Pre-schema boot logs need a new monitor session after booting firmware that emits RAW/PAIR/PIX/PG.")
    elif report["status"] == "no_observations":
        print("Identified pairs have no valid matched point observations; no jitter measurements are available.")
    for side, path in report["sources"].items():
        print(f"Source {side}: {path}")
    window = report["window"]
    print(f"Requested inclusive bounds: right-session={window['right_session']} "
          f"start-pair={window['start_pair']} end-pair={window['end_pair']} (None=unbounded)")
    for observed in window["observed"]:
        print("Observed source window:", json.dumps(observed, sort_keys=True))
    print("Pairs:", json.dumps(report["pairs"], sort_keys=True))
    print("Link counts:", json.dumps(report["links"], sort_keys=True))
    print("PAIR status counts:", json.dumps(report["pair_status"], sort_keys=True))
    print("Sync:", json.dumps(report["sync"], sort_keys=True))
    print("PG input admission:", json.dumps(report["admission"], sort_keys=True))
    print("Parse diagnostics:", json.dumps(report["parse"], sort_keys=True))
    print("Pixels: source side  point          n invalid missing    std_x    std_y   span_x   span_y steps step_p95 step_max")
    for side in SIDES:
        for name in POINTS:
            for source in ("RAW", "PIX"):
                point = report["stats"][source][side][name]
                values = ["     N/A" if point[field] is None else f"{point[field]:8.3f}"
                          for field in ("std_x", "std_y", "span_x", "span_y", "step_p95", "step_max")]
                print(f"{source:6} {side:5} {name:11} {point['n']:5} {point['invalid']:7} {point['missing']:7} "
                      + " ".join(values[:4]) + f" {point['step_n']:5} " + " ".join(values[4:]))
    for note in report["notes"]:
        print(note)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", required=True, type=Path, help="Directory containing left_uart.log and right_uart.log")
    parser.add_argument("--right-session", type=int, help="Select a RIGHT boot session ID")
    parser.add_argument("--start-pair", type=int, help="Inclusive first pair ID (within each selected RIGHT session)")
    parser.add_argument("--end-pair", type=int, help="Inclusive last pair ID")
    parser.add_argument("--json", type=Path, help="Write a generated JSON report")
    args = parser.parse_args(argv)
    try:
        if args.json is not None and args.json.resolve() in {
                (args.session / (side + "_uart.log")).resolve() for side in SIDES}:
            raise ValueError("JSON output must not overwrite a source log")
        report = analyze(args.session, args.right_session, args.start_pair, args.end_pair)
        print_report(report)
        if args.json is not None:
            args.json.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
        return 0 if report["status"] == "ok" else 1
    except (OSError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
