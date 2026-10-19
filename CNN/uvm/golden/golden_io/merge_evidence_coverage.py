#!/usr/bin/env python3
"""Merge measured functional and RTL coverage, rejecting URG load errors."""
import json
import subprocess
from pathlib import Path
from run_final_regression import OUT, EVIDENCE, ROOT


def urg(label, args):
    command = ["urg", "-full64"] + args
    log = OUT / "logs" / (label + "_console.log")
    with log.open("w") as handle:
        rc = subprocess.run(command, cwd=OUT, stdout=handle, stderr=subprocess.STDOUT).returncode
    text = log.read_text(errors="replace")
    if rc or "Error-[" in text or "Shape Not Found" in text or "Design Not Loaded" in text or "Directory not found" in text:
        raise SystemExit("URG error: inspect " + str(log))
    (EVIDENCE / "reports" / (label + "_command.json")).write_text(json.dumps(command, indent=2) + "\n")
    print(label + " PASS", flush=True)


def main():
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--functional-only", action="store_true")
    parser.add_argument("--skip-functional", action="store_true")
    args = parser.parse_args()
    original = json.loads((EVIDENCE / "reports" / "regression_runs.json").read_text())
    if len(original) != 16 or any(r["result"] != "PASS" for r in original):
        raise SystemExit("Incomplete or failing regression evidence")
    merged = OUT / "coverage" / "merged"
    # Original runs have measured covergroups but no RTL library instrumentation.
    # Import only their group metric. The code runs supply actual RTL metrics.
    if not args.skip_functional:
        urg("urg_functional_merge", ["-dir", str(OUT / "coverage/runs/build.vdb")] +
             [r["db"] for r in original] + ["-metric", "group", "-dbname", str(merged / "functional.vdb"),
             "-format", "both", "-report", str(EVIDENCE / "coverage" / "functional" / "urg_report"),
             "-show", "ratios", "-group", "ratio", "-group", "instcov_for_score"])
    if args.functional_only:
        return
    code = json.loads((EVIDENCE / "reports" / "regression_code_runs.json").read_text())
    if len(code) != 6 or any(r["result"] != "PASS" for r in code):
        raise SystemExit("Incomplete or failing code regression evidence")
    urg("urg_final_merge", ["-dir", str(OUT / "coverage/runs/build_rtl.vdb")] +
        [r["db"] for r in code] + [str(merged / "functional.vdb"),
         "-flex_merge", "union", "-dbname", str(merged / "golden_merged.vdb"),
         "-format", "both", "-report", str(EVIDENCE / "coverage" / "urg_report"),
         "-show", "ratios", "-group", "ratio", "-group", "instcov_for_score"])
    # Reopen the merged DB on its own; a bundle must not require build.vdb.
    urg("urg_merged_reload", ["-dir", str(merged / "golden_merged.vdb"),
         "-format", "both", "-report", str(OUT / "coverage" / "merged_reload_report"),
         "-show", "ratios", "-group", "ratio", "-group", "instcov_for_score"])
    print((EVIDENCE / "coverage" / "urg_report" / "dashboard.txt").read_text())


if __name__ == "__main__":
    main()
