#!/usr/bin/env python3
"""Run the actual VCS evidence regressions from output, preserving old artifacts."""
import argparse
import concurrent.futures
import csv
import hashlib
import json
import re
import shutil
import subprocess
import time
from pathlib import Path

GOLDEN = Path(__file__).resolve().parents[1]
OUT = GOLDEN / "output"
EVIDENCE = OUT / "final_evidence"
ROOT = GOLDEN.parents[2]
METRICS = "line+cond+fsm+tgl+branch"
SIM_BINARY = OUT / "bin" / "simv_evidence"


def inspect_log(path, expected_frames=1, atomic=False, stall=False):
    text = path.read_text(errors="replace")
    errors = re.findall(r"^UVM_ERROR\s*:\s*(\d+)", text, re.M)
    fatals = re.findall(r"^UVM_FATAL\s*:\s*(\d+)", text, re.M)
    passed = text.count("G01 GOLDEN CHECK PASS: 17/17 joints bit-exact matched")
    metadata = text.count("JOINT_FLAGS / RESULT_SEQ / ERROR_STATUS / RESULT_FRAME_ID PASS")
    ok = (errors == ["0"] and fatals == ["0"] and passed == expected_frames
          and metadata == expected_frames and "$finish at simulation time" in text)
    if atomic:
        ok &= "G05 ATOMIC PUBLISH CHECK PASS" in text
    stats = re.search(r"seed=(\d+) stalls=(\d+) accepted=(\d+) stability_checks=(\d+) schedules=(\d+) stimulus_hash=([0-9a-f]+)", text)
    if stall:
        ok &= bool(stats and int(stats[2]) > 0 and int(stats[4]) > 0)
    return {"result": "PASS" if ok else "FAIL", "joint_frames": passed,
            "uvm_errors": int(errors[-1]) if errors else None,
            "uvm_fatals": int(fatals[-1]) if fatals else None,
            "stall_stats": stats.groups() if stats else None}


def run_case(case):
    name, scenario, test, frame, seed, extra, count = case
    log_dir = EVIDENCE / "logs" / scenario.lower()
    log_dir.mkdir(parents=True, exist_ok=True)
    log = log_dir / (name + ".log")
    features = OUT / "feature_stream" / name
    features.mkdir(parents=True, exist_ok=True)
    trace = log_dir / (name + "_stimulus.csv")
    db = OUT / "coverage" / "runs" / (name + ".vdb")
    cmd = [str(SIM_BINARY), "-no_save",
           "-cm", METRICS, "-cm_dir", str(db), "-cm_name", name,
           "+UVM_TESTNAME=" + test, "+UVM_NO_RELNOTES", "+UVM_VERBOSITY=UVM_LOW",
           "+UVM_TIMEOUT=500000000000,YES", "+FRAME_ID=" + str(frame),
           "+IMAGE_HEX=image_hex/frame_%05d_image.hex" % frame,
           "+GOLDEN_FINAL_HEX=python_golden/frame_%05d/final_expected.hex" % frame,
           "+WEIGHT_DIR=weight_stream", "+FEATURE_DIR=" + str(features),
           "+ntb_random_seed=" + str(seed), "+STALL_SEED=" + str(seed),
           "+STALL_TRACE=" + str(trace)] + extra
    # VCS takes the first matching plusarg; remove overridden defaults.
    for override in extra:
        if "=" in override:
            key = override.split("=", 1)[0] + "="
            cmd = [arg for arg in cmd if not arg.startswith(key) or arg == override]
    started = time.time()
    print("START " + name, flush=True)
    with log.open("w") as handle:
        try:
            rc = subprocess.run(cmd, cwd=OUT, stdout=handle, stderr=subprocess.STDOUT,
                                timeout=3600).returncode
        except subprocess.TimeoutExpired:
            rc = 124
    result = inspect_log(log, count, scenario == "G05", scenario == "G07")
    result.update(name=name, scenario=scenario, frame=frame, seed=seed,
                  exit_code=rc, seconds=round(time.time() - started, 2),
                  log=str(log.relative_to(EVIDENCE)), db=str(db), command=cmd)
    if rc:
        result["result"] = "FAIL"
    if trace.exists():
        result["stimulus_sha256"] = hashlib.sha256(trace.read_bytes()).hexdigest()
    print("END %s %s %.1fs" % (name, result["result"], result["seconds"]), flush=True)
    return result


def summarize(results, scenario):
    rows = [r for r in results if r["scenario"] == scenario and "repeat" not in r["name"]]
    rows.sort(key=lambda r: r["frame"] if scenario == "G06" else r["seed"])
    columns = ["name", "frame", "seed", "result", "joint_frames", "uvm_errors", "uvm_fatals", "seconds", "log"]
    path = EVIDENCE / "reports" / (scenario.lower() + "_summary.csv")
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, columns, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)
    title = "MULTI-SAMPLE REGRESSION" if scenario == "G06" else "RANDOM STALL / DELAY REGRESSION"
    lines = [scenario + " " + title]
    lines += [("frame%d" % r["frame"] if scenario == "G06" else "seed=%d" % r["seed"]) + " " + r["result"] for r in rows]
    passed = sum(r["result"] == "PASS" for r in rows)
    lines += ["PASS=%d" % passed, "FAIL=%d" % (len(rows) - passed), "TOTAL=%d/%d" % (passed, len(rows))]
    if scenario == "G07":
        original = next(r for r in rows if r["seed"] == 1)
        repeat = next(r for r in results if r["name"] == "g07_seed_1_repeat")
        reproduced = (original.get("stimulus_sha256") == repeat.get("stimulus_sha256")
                      and original["result"] == repeat["result"] == "PASS")
        lines += ["seed=1 stimulus SHA256 reproducibility=" + ("PASS" if reproduced else "FAIL")]
    summary = "\n".join(lines) + "\n"
    path.with_suffix(".txt").write_text(summary)
    (OUT / (scenario.lower() + "_summary.txt")).write_text(summary)
    shutil.copy2(path, OUT / path.name)
    target = "g06_regression_summary.txt" if scenario == "G06" else "g07_seed_summary.txt"
    (EVIDENCE / "reports" / target).write_text(summary)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    results = []
    manifest = EVIDENCE / "reports" / "regression_runs.json"
    groups = []
    groups.append([("g06_frame_%05d" % frame, "G06", "cnn_g06_multisample_test", frame, 1, [], 1)
                   for frame in range(121, 126)])
    g07 = [("g07_seed_%d" % seed, "G07", "cnn_g07_random_stall_test", 121, seed,
            (["+FSDB_FILE=" + str(EVIDENCE / "waveform" / "g07_random_stall.fsdb")] if seed == 1 else []), 1)
           for seed in (1, 11, 101, 1001)]
    g07.append(("g07_seed_1_repeat", "G07", "cnn_g07_random_stall_test", 121, 1, [], 1))
    groups.append(g07)
    groups.append([
        ("g01_coverage", "G01", "cnn_g01_e2e_test", 121, 1, [], 1),
        ("g03_equal_coverage", "G03", "cnn_g03_threshold_test", 121, 1,
         ["+JOINT_THRESHOLD=62", "+GOLDEN_FINAL_HEX=python_golden/frame_00121/g03/g03_threshold_equal.hex"], 1),
        ("g03_plus1_coverage", "G03", "cnn_g03_threshold_test", 121, 1,
         ["+JOINT_THRESHOLD=63", "+GOLDEN_FINAL_HEX=python_golden/frame_00121/g03/g03_threshold_plus1.hex"], 1),
        ("g04_coverage", "G04", "cnn_g04_multiframe_test", 121, 1, [], 3),
        ("g05_coverage", "G05", "cnn_g05_atomic_publish_test", 121, 1,
         ["+FSDB_G05", "+FSDB_FILE=" + str(EVIDENCE / "waveform" / "g05_atomic_publish.fsdb")], 2),
    ])
    checkpoints = OUT / "rtl_checkpoints_evidence"
    checkpoints.mkdir(exist_ok=True)
    groups[-1].append(("g02_coverage", "G02", "cnn_g02_checkpoint_test", 121, 1,
                       ["+G02_DUMP_DIR=" + str(checkpoints)], 1))
    for index, group in enumerate(groups):
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            futures = [pool.submit(run_case, c) for c in group]
            for future in concurrent.futures.as_completed(futures):
                results.append(future.result())
                manifest.write_text(json.dumps(results, indent=2) + "\n")
        results.sort(key=lambda r: r["name"])
        if index < 2:
            summarize(results, ("G06", "G07")[index])
    # Keep accidental fixed-name simulator artifacts beneath output.
    for name, target in (("tr_db.log", OUT / "logs"), ("ucli.key", OUT / "build"), ("novas_dump.log", OUT / "logs")):
        if (OUT / name).exists():
            shutil.move(str(OUT / name), str(target / ("evidence_" + name)))
    failed = [r["name"] for r in results if r["result"] != "PASS"]
    print("REGRESSION COMPLETE: %d runs, failures=%s" % (len(results), failed), flush=True)
    return bool(failed)


if __name__ == "__main__":
    raise SystemExit(main())
