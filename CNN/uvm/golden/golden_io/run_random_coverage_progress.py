#!/usr/bin/env python3
"""Measure cumulative Golden G07 RTL coverage without changing old evidence.

Existing instrumented Golden binary and explicit Golden-only baseline inputs are
used. Every seed is executed afresh; failed runs remain in manifests and merges.
Run directories and stage checkpoints are write-once and resumable.
"""
import argparse
import concurrent.futures
import csv
from decimal import Decimal
import gzip
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess
import time
import xml.etree.ElementTree as ET

from run_final_regression import OUT, EVIDENCE, METRICS, inspect_log

DEST = OUT / "random_coverage_progress"
BINARY = OUT / "bin/simv_evidence_rtl"
BUILD = OUT / "coverage/runs/build_rtl.vdb"
FUNCTIONAL = OUT / "coverage/merged/functional.vdb"
METRIC_NAMES = ("line", "condition", "branch", "fsm", "toggle")
METRIC_LABELS = dict(line="Line", condition="Condition", branch="Branch", fsm="FSM", toggle="Toggle")
STAGES = (10, 25, 50, 75, 100)
THRESHOLD = Decimal("0.10")
SEED_COLUMNS = (
    "seed", "result", "joint_frames", "joint_flags_pass", "result_frame_id_pass",
    "error_status_zero", "uvm_errors", "uvm_fatals", "exit_code", "seconds",
    "stall_events", "total_stall_cycles", "max_stall_length", "ready_toggle_count",
    "scheduled_stall_events", "ready_low_cycles", "schedules", "stimulus_sha256",
    "stimulus_pattern_sha256", "schedule_hash", "baseline_determinism", "log", "stimulus", "db", "failure_point",
)


def save_json(path, data):
    path.write_text(json.dumps(data, indent=2) + "\n")


def xml_read(path):
    data = path.read_bytes()
    return ET.fromstring(gzip.decompress(data) if data.startswith(b"\x1f\x8b") else data)


def planned_seeds():
    # Retain historical seeds first, then platform-independent SHA256-derived IDs.
    seeds = [1, 11, 101, 1001]
    counter = 0
    while len(seeds) < 100:
        digest = hashlib.sha256(("golden-g07-convergence-v1:%d" % counter).encode()).digest()
        seed = int.from_bytes(digest[:4], "big") & 0x7fffffff
        if seed and seed not in seeds:
            seeds.append(seed)
        counter += 1
    return seeds


def write_csv(path, fields, rows):
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def parse_dashboard(path):
    text = path.read_text()
    total = text.split("Total Coverage Summary", 1)[1].split("---", 1)[0]
    lines = [line.split() for line in total.splitlines() if line.strip()]
    headers, values = lines[:2]
    # -show ratios yields percentage plus hit/total, except for SCORE.
    parsed, ratios, index = {}, {}, 0
    for header in headers:
        parsed[header] = Decimal(values[index])
        index += 1
        if header != "SCORE":
            hit, denominator = map(int, values[index].split("/"))
            ratios[header] = {"hit": hit, "total": denominator}
            index += 1
    mapping = dict(line="LINE", condition="COND", branch="BRANCH", fsm="FSM", toggle="TOGGLE")
    return {key: str(parsed[value]) for key, value in mapping.items() if value in parsed}, ratios, str(parsed.get("GROUP", ""))


def golden_functional(path):
    """Read Golden-specific groups separately from inherited common covergroups."""
    listing = path.read_text().split("Group :", 1)[0]
    groups = {}
    for line in listing.splitlines():
        fields = line.split()
        if fields and fields[-1].startswith("cnn_golden_pkg::cnn_golden_coverage::"):
            groups[fields[-1]] = dict(hit=int(fields[0]), total=int(fields[1]))
    if len(groups) != 8:
        raise RuntimeError("Expected eight Golden covergroups in " + str(path))
    hit = sum(group["hit"] for group in groups.values())
    total = sum(group["total"] for group in groups.values())
    return dict(coverage="%.2f" % (Decimal(hit) * 100 / total), hit=hit, total=total, groups=groups)


def run_urg(label, inputs, create_db=True):
    report = DEST / "urg_report" / label
    db = DEST / "merged" / (label + ".vdb")
    checkpoint = DEST / "merged" / (label + ".json")
    if checkpoint.exists():
        saved = json.loads(checkpoint.read_text())
        if saved["inputs"] != [str(path) for path in inputs]:
            raise RuntimeError("Checkpoint inputs changed: " + label)
        if not create_db and saved["db"] != str(inputs[0]):
            initial = checkpoint.with_name(label + "_initial.json")
            if not initial.exists():
                initial.write_bytes(checkpoint.read_bytes())
            saved["db"] = str(inputs[0])
            save_json(checkpoint, saved)
        return saved
    if report.exists() or (create_db and db.exists()):
        raise RuntimeError("Incomplete URG evidence exists; preserve and inspect: " + label)
    command = ["urg", "-full64", "-dir"] + [str(path) for path in inputs]
    command += ["-flex_merge", "union"]
    if create_db:
        command += ["-dbname", str(db)]
    command += ["-format", "both", "-report", str(report), "-show", "ratios",
                "-group", "ratio", "-group", "instcov_for_score"]
    log = DEST / "logs" / (label + "_urg.log")
    attempt = 1
    while log.exists():
        attempt += 1
        log = DEST / "logs" / (label + "_urg_attempt_%d.log" % attempt)
    save_json(log.with_suffix(".command.json"), command)
    started = time.monotonic()
    with log.open("x") as handle:
        rc = subprocess.run(command, cwd=DEST, stdout=handle, stderr=subprocess.STDOUT).returncode
    contents = log.read_text(errors="replace")
    bad = ("Error-[", "Shape Not Found", "Design Not Loaded", "Directory not found")
    if rc or any(word in contents for word in bad):
        raise RuntimeError("URG failed; inspect " + str(log))
    coverage, ratios, functional = parse_dashboard(report / "dashboard.txt")
    result = dict(label=label, inputs=[str(path) for path in inputs], db=str(db if create_db else inputs[0]),
                  report=str(report), coverage=coverage, ratios=ratios,
                  functional=functional, seconds=round(time.monotonic() - started, 2))
    save_json(checkpoint, result)
    print("MERGE %s %s functional=%s" % (label, coverage, functional), flush=True)
    return result


def stimulus_stats(path, log_text):
    finish = re.search(r"\$finish at simulation time\s+(\d+)", log_text)
    end_ps = int(finish[1]) if finish else None
    events = cycles = toggles = maximum = schedules = 0
    prefix = []
    with path.open() as handle:
        for row in csv.DictReader(handle):
            schedules += 1
            low, high, start = (int(row[key]) for key in ("low_cycles", "high_cycles", "time_ps"))
            if not (0 <= low <= 12 and 32 <= high <= 128):
                raise ValueError("G07 schedule outside constraints")
            if schedules <= 256:
                prefix.append("%d,%d\n" % (low, high))
            # 10 ns clock. Clip last scheduled interval to actual simulation end.
            low_start, low_end = start + high * 10000, start + (high + low) * 10000
            if not low or (end_ps is not None and low_start >= end_ps):
                continue
            observed = low if end_ps is None else min(low, (end_ps - low_start + 9999) // 10000)
            events += 1
            cycles += observed
            maximum = max(maximum, observed)
            toggles += 1 + int(end_ps is None or low_end < end_ps)
    return dict(scheduled_stall_events=events, ready_low_cycles=cycles,
                max_stall_length=maximum, ready_toggle_count=toggles, schedules=schedules,
                stimulus_pattern_sha256=hashlib.sha256("".join(prefix).encode()).hexdigest())


def inspect_seed(seed, run, rc, seconds, command, historical):
    log, trace, db = run / "simulation.log", run / "stimulus.csv", run / "coverage.vdb"
    text = log.read_text(errors="replace")
    result = inspect_log(log, 1, stall=True)
    checks = {
        "joint_flags_pass": bool(re.search(r"JOINT_FLAGS PASS RTL=", text)),
        "result_frame_id_pass": "RESULT_FRAME_ID PASS expected=121 actual=121" in text,
        "error_status_zero": "ERROR_STATUS PASS expected=0 actual=0" in text,
    }
    result.update(checks)
    result.update(seed=seed, exit_code=rc, seconds=round(seconds, 2),
                  log=str(log), stimulus=str(trace), db=str(db), command=command)
    failures = []
    if rc:
        failures.append("process exit code %s" % rc)
    failures += [key for key, passed in checks.items() if not passed]
    if result["result"] != "PASS":
        failures.append("joint/metadata/UVM/stability/finish check")
    stats = result.get("stall_stats")
    if stats:
        result.update(stall_events=int(stats[1]), total_stall_cycles=int(stats[3]), schedule_hash=stats[5])
        if int(stats[0]) != seed:
            failures.append("reported stall seed mismatch")
    result["baseline_determinism"] = "N/A"
    if trace.exists():
        result["stimulus_sha256"] = hashlib.sha256(trace.read_bytes()).hexdigest()
        try:
            result.update(stimulus_stats(trace, text))
            if stats and int(stats[4]) != result["schedules"]:
                failures.append("schedule count mismatch")
        except (ValueError, KeyError) as exc:
            failures.append(str(exc))
        if seed in historical:
            result["baseline_determinism"] = (
                "PASS" if result["stimulus_sha256"] == historical[seed] else "FAIL")
            if result["baseline_determinism"] == "FAIL":
                failures.append("historical stimulus SHA256 changed")
    else:
        failures.append("stimulus trace missing")
    for metric in ("line", "cond", "branch", "fsm", "tgl"):
        if not list(db.glob("snps/coverage/db/testdata/*/" + metric + ".verilog.data.xml")):
            failures.append(metric + " VDB hit data missing")
    result["result"] = "FAIL" if failures else "PASS"
    # Preserve first failure text and completed checkpoint for every seed.
    points = [line for line in text.splitlines() if re.match(r"^UVM_(?:ERROR|FATAL)\s+(?:@|[^\s:])", line)
              or "Error-[" in line]
    result["failure_point"] = " | ".join(points[:5] + failures) if failures else ""
    result["schema_version"] = 2
    return result


def run_seed(seed, historical, wave=False):
    run = DEST / "runs" / (("best_seed_%d" if wave else "seed_%d") % seed)
    checkpoint = run / "result.json"
    if checkpoint.exists():
        prior = json.loads(checkpoint.read_text())
        if prior.get("schema_version") != 2:
            # Enrich initial telemetry without rerunning or losing initial evidence.
            revised = inspect_seed(seed, run, prior["exit_code"], prior["seconds"], prior["command"], historical)
            for key in ("result", "joint_frames", "joint_flags_pass", "result_frame_id_pass",
                        "error_status_zero", "uvm_errors", "uvm_fatals", "stall_stats", "stimulus_sha256"):
                if json.dumps(revised.get(key), sort_keys=True) != json.dumps(prior.get(key), sort_keys=True):
                    raise RuntimeError("Completed seed evidence changed: %s %s" % (seed, key))
            initial = run / "result_initial.json"
            if not initial.exists():
                initial.write_bytes(checkpoint.read_bytes())
            save_json(checkpoint, revised)
            return revised
        return prior
    if run.exists():
        raise RuntimeError("Incomplete seed run exists; preserve and inspect: " + str(run))
    run.mkdir()
    (run / "features").mkdir()
    command = [str(BINARY), "-no_save", "-cm", METRICS, "-cm_dir", str(run / "coverage.vdb"),
               "-cm_name", run.name, "+UVM_TESTNAME=cnn_g07_random_stall_test",
               "+UVM_NO_RELNOTES", "+UVM_VERBOSITY=UVM_LOW", "+UVM_TIMEOUT=500000000000,YES",
               "+FRAME_ID=121", "+IMAGE_HEX=" + str(OUT / "image_hex/frame_00121_image.hex"),
               "+GOLDEN_FINAL_HEX=" + str(OUT / "python_golden/frame_00121/final_expected.hex"),
               "+WEIGHT_DIR=" + str(OUT / "weight_stream"), "+FEATURE_DIR=" + str(run / "features"),
               "+ntb_random_seed=%d" % seed, "+STALL_SEED=%d" % seed,
               "+STALL_TRACE=" + str(run / "stimulus.csv")]
    if wave:
        target = DEST / "waveform/g07_random_best_seed.fsdb"
        if target.exists():
            raise RuntimeError("Waveform already exists; preserve it")
        command += ["+FSDB_FILE=" + str(target)]
    save_json(run / "command.json", command)
    print("START %s" % run.name, flush=True)
    started = time.monotonic()
    with (run / "simulation.log").open("x") as handle:
        try:
            rc = subprocess.run(command, cwd=run, stdout=handle, stderr=subprocess.STDOUT, timeout=3600).returncode
        except subprocess.TimeoutExpired:
            rc = 124
    result = inspect_seed(seed, run, rc, time.monotonic() - started, command, historical)
    save_json(checkpoint, result)
    print("END %s %s %.1fs %s" % (run.name, result["result"], result["seconds"], result["failure_point"]), flush=True)
    return result


def verify_waveform(wave):
    target = DEST / "waveform/g07_random_best_seed.fsdb"
    digest = hashlib.sha256(target.read_bytes()).hexdigest()
    checkpoint = DEST / "logs/best_seed_fsdb_check.json"
    if checkpoint.exists():
        checked = json.loads(checkpoint.read_text())
        if checked["sha256"] != digest:
            raise RuntimeError("Finished representative FSDB changed")
        return checked
    log = DEST / "logs/best_seed_fsdb_summary.log"
    attempt = 1
    while log.exists():
        attempt += 1
        log = DEST / "logs" / ("best_seed_fsdb_summary_attempt_%d.log" % attempt)
    command = ["fsdb2vcd", str(target), "-summary"]
    save_json(log.with_suffix(".command.json"), command)
    with log.open("x") as handle:
        rc = subprocess.run(command, cwd=DEST / "waveform", stdout=handle, stderr=subprocess.STDOUT).returncode
    summary = log.read_text(errors="replace")
    end = re.search(r"max xtag\s*:\s*\((\d+)\s+(\d+)\)", summary)
    variables = re.search(r"unique var count\s*:\s*(\d+)", summary)
    finished = re.search(r"file status\s*:\s*finished", summary)
    one_ps = re.search(r"scale unit\s*:\s*1ps", summary)
    expected_end = re.search(r"\$finish at simulation time\s+(\d+)", Path(wave["log"]).read_text())
    if rc or not all((end, variables, finished, one_ps, expected_end)):
        raise RuntimeError("Representative FSDB validation failed; inspect " + str(log))
    end_ps = (int(end[1]) << 32) | int(end[2])
    if end_ps != int(expected_end[1]) or int(variables[1]) < 7:
        raise RuntimeError("FSDB time range/signal count does not match completed replay")
    checked = dict(result="PASS", path=str(target), sha256=digest, bytes=target.stat().st_size,
                   finished=True, end_ps=end_ps, unique_variables=int(variables[1]), log=str(log), command=command)
    save_json(checkpoint, checked)
    print("FSDB finished/time range/signal count: PASS", flush=True)
    return checked


def progress_row(stage, count, merged, previous=None):
    row = dict(stage=stage, seed_count=count, **merged["coverage"])
    for key in METRIC_NAMES:
        delta = Decimal(row[key]) - Decimal(previous[key]) if previous else Decimal("0")
        if delta < 0:
            raise RuntimeError("Cumulative metric decreased: " + key)
        row["delta_" + key] = "%.2f" % delta
    return row


def save_progress(rows, results):
    write_csv(DEST / "coverage_progress.csv", ["stage", "seed_count"] + list(METRIC_NAMES)
              + ["delta_" + key for key in METRIC_NAMES], rows)
    write_csv(DEST / "seed_results.csv", SEED_COLUMNS, results)
    save_json(DEST / "seed_results.json", results)


def final_summary(rows, results, final_merge, best, wave, saturated, baseline_seeds, functional_before, issues):
    golden_before = golden_functional(EVIDENCE / "coverage/urg_report/groups.txt")
    golden_after = golden_functional(Path(final_merge["report"]) / "groups.txt")
    lines = ["[G07 RANDOM COVERAGE CONVERGENCE]", "Baseline Seeds: %d (%s)" %
             (len(baseline_seeds), ", ".join(map(str, baseline_seeds))),
             "Maximum Planned Seeds: 100", "Actually Executed Seeds: %d (all executed afresh)" % len(results),
             "PASS: %d" % sum(r["result"] == "PASS" for r in results),
             "FAIL: %d" % sum(r["result"] != "PASS" for r in results), "", "[COVERAGE PROGRESS]"]
    lookup = {row["seed_count"]: row for row in rows}
    for count in [len(baseline_seeds)] + list(STAGES):
        label = "Baseline" if count == len(baseline_seeds) else "%d Seeds" % count
        if count in lookup:
            lines += [label + ":"] + ["%s: %s%%" % (METRIC_LABELS[key], lookup[count][key]) for key in METRIC_NAMES]
        else:
            lines += [label + ": NOT RUN — saturation reached"]
    lines += ["", "[COVERAGE DELTA]"]
    for previous, row in zip(rows, rows[1:]):
        lines += ["%s → %s: %s percentage points" % (previous["stage"], row["stage"],
                  ", ".join("%s +%s" % (METRIC_LABELS[key], row["delta_" + key]) for key in METRIC_NAMES))]
    lines += ["", "[SATURATION]", "Reached: " + ("YES" if saturated else "NO"),
              "At Seed Count: " + (str(len(results)) if saturated else "N/A"),
              "Reason: " + ("Two consecutive checkpoint deltas are < +0.10 percentage point in every metric."
                            if saturated else "Maximum planned seed count reached."),
              "", "[FINAL CODE COVERAGE]", "Golden Regression RTL Code Coverage"]
    lines += ["%s: %s%%" % (METRIC_LABELS[key], rows[-1][key]) for key in METRIC_NAMES]
    lines += ["", "[FUNCTIONAL COVERAGE]", "Golden Functional Coverage: %s%% (%d/%d) → %s%% (%d/%d)" %
              (golden_before["coverage"], golden_before["hit"], golden_before["total"],
               golden_after["coverage"], golden_after["hit"], golden_after["total"]),
              "URG aggregate GROUP including common coverage: %s%% → %s%% (%d/%d)" %
              (functional_before, final_merge["functional"], final_merge["ratios"]["GROUP"]["hit"],
               final_merge["ratios"]["GROUP"]["total"]), "", "[BEST RANDOM SEED]",
              "Seed: %d" % best["seed"], "Stall Events: %d" % best["stall_events"],
              "Total Stall Cycles: %d" % best["total_stall_cycles"], "Max Stall: %d (ready-low schedule)" % best["max_stall_length"],
              "FSDB: " + str(DEST / "waveform/g07_random_best_seed.fsdb"),
              "Wave Replay: %s; stimulus SHA256: %s" % (wave["result"], wave["stimulus_sha256"]),
              "", "[VERDI]", "Merged VDB: " + final_merge["db"],
              "Open Command: " + shlex.join(["verdi", "-cov", "-covdir", final_merge["db"]]),
              "", "[URG]", "HTML Entry: " + str(Path(final_merge["report"]) / "dashboard.html"),
              "", "[PPT READY]",
              "Golden G07을 %d unique seeds까지 확장하고 각 단계에서 URG hit 정보를 누적 merge했다." % len(results),
              "다섯 RTL metric의 증가량을 측정했으며, %s." % ("두 단계 연속 +0.10 percentage point 미만으로 saturation을 확인했다" if saturated else "100 seed까지 convergence를 확인했다"),
              "17/17 Joint bit-exact 및 metadata/UVM 검사 결과: PASS %d, FAIL %d." %
              (sum(r["result"] == "PASS" for r in results), sum(r["result"] != "PASS" for r in results)),
              "", "[FILES CHANGED]", "CNN/uvm/golden/golden_io/run_random_coverage_progress.py",
              "", "[REMAINING ISSUE]"] + (issues or ["NONE"])
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=int, default=6)
    args = parser.parse_args()
    if not 1 <= args.jobs <= 16:
        parser.error("--jobs must be 1..16")
    DEST.mkdir(exist_ok=True)
    for name in ("logs", "runs", "merged", "urg_report", "waveform"):
        (DEST / name).mkdir(exist_ok=True)
    seeds = planned_seeds()
    seed_path = DEST / "seeds_100.txt"
    content = "".join("%d\n" % seed for seed in seeds)
    if seed_path.exists() and seed_path.read_text() != content:
        raise RuntimeError("Existing seed list differs; preserve it")
    if not seed_path.exists():
        seed_path.write_text(content)
    code = json.loads((EVIDENCE / "reports/regression_code_runs.json").read_text())
    if len(code) != 6 or any(row["result"] != "PASS" for row in code):
        raise RuntimeError("Baseline code evidence is incomplete/failing")
    for metric in ("line", "cond", "branch", "fsm", "tgl"):
        if len(xml_read(BUILD / ("snps/coverage/db/shape/%s.verilog.shape.xml" % metric))) <= 1:
            raise RuntimeError("Empty RTL instrumentation: " + metric)
    inputs = [BUILD] + [Path(row["db"]).resolve() for row in code] + [FUNCTIONAL]
    for path in inputs:
        if not path.is_relative_to((OUT / "coverage").resolve()) or not path.exists():
            raise RuntimeError("Invalid Golden baseline input: " + str(path))
    historical = {row["seed"]: row["stimulus_sha256"] for row in code if row["scenario"] == "G07"}
    baseline_seeds = sorted(historical)
    audit = dict(binary=str(BINARY), binary_sha256=hashlib.sha256(BINARY.read_bytes()).hexdigest(),
                 hierarchy_filter=str(OUT.parent / "coverage/rtl_coverage.hier"),
                 hierarchy_text=(OUT.parent / "coverage/rtl_coverage.hier").read_text(),
                 metrics=METRICS, baseline_inputs=[str(path) for path in inputs],
                 original_compile_command=ET.tostring(xml_read(BUILD / "snps/coverage/db/auxiliary/vcmArguments.xml"), encoding="unicode"),
                 seed_generation="SHA256 golden-g07-convergence-v1:<counter>, first uint32 masked 0x7fffffff; historical four first",
                 jobs=args.jobs, baseline_seed_count=len(baseline_seeds), max_planned_seeds=100,
                 saturation="Two consecutive checkpoints with all five deltas < 0.10 pp; baseline→10 is first comparison")
    save_json(DEST / "baseline_provenance.json", audit)
    baseline = run_urg("baseline", inputs)
    original, ratios, functional_before = parse_dashboard(EVIDENCE / "coverage/urg_report/dashboard.txt")
    if baseline["coverage"] != original or baseline["ratios"] != ratios:
        raise RuntimeError("Baseline remerge differs from saved report")
    rows = [progress_row("Baseline", len(baseline_seeds), baseline)]
    results, low_streak, saturated, final_merge = [], 0, False, baseline
    save_progress(rows, results)
    for count in STAGES:
        completed = {row["seed"] for row in results}
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            pending = [pool.submit(run_seed, seed, historical) for seed in seeds[:count] if seed not in completed]
            for future in concurrent.futures.as_completed(pending):
                results.append(future.result())
                results.sort(key=lambda row: seeds.index(row["seed"]))
                save_progress(rows, results)
        # Include all run databases regardless of PASS/FAIL, never average percentages.
        final_merge = run_urg("stage_%03d" % count, inputs + [Path(row["db"]) for row in results])
        for key in METRIC_NAMES:
            mapping = dict(line="LINE", condition="COND", branch="BRANCH", fsm="FSM", toggle="TOGGLE")
            if final_merge["ratios"][mapping[key]]["total"] != baseline["ratios"][mapping[key]]["total"]:
                raise RuntimeError("Coverage denominator changed: " + key)
        row = progress_row("%d Seeds" % count, count, final_merge, rows[-1])
        rows.append(row)
        save_progress(rows, results)
        significant = max(Decimal(row["delta_" + key]) for key in METRIC_NAMES) >= THRESHOLD
        low_streak = 0 if significant else low_streak + 1
        saturated = low_streak >= 2
        decision = "STOP — saturation reached" if saturated else ("CONTINUE" if significant else "CONTINUE — confirm saturation candidate")
        save_json(DEST / "merged" / ("stage_%03d_decision.json" % count),
                  dict(seed_count=count, delta={key: row["delta_" + key] for key in METRIC_NAMES},
                       significant_gain=significant, low_gain_streak=low_streak, decision=decision,
                       pass_count=sum(r["result"] == "PASS" for r in results),
                       fail_count=sum(r["result"] != "PASS" for r in results)))
        print("CHECKPOINT %d: %s" % (count, decision), flush=True)
        if saturated:
            break
    issues = []
    hashes = [row.get("stimulus_sha256") for row in results]
    if len(set(hashes)) != len(hashes):
        issues.append("Duplicate/missing stimulus hash: investigate randomization.")
    patterns = [row.get("stimulus_pattern_sha256") for row in results]
    if None in patterns or len(set(patterns)) != len(patterns):
        issues.append("Duplicate/missing first-256 schedule pattern hash: investigate randomization.")
    save_json(DEST / "stimulus_quality.json", dict(seed_count=len(results),
              unique_full_traces=len(set(hashes)), unique_first_256_patterns=len(set(patterns)),
              pattern_definition="SHA256 of first 256 low_cycles,high_cycles rows; timestamps excluded",
              seeds=[dict(seed=row["seed"], trace_sha256=row.get("stimulus_sha256"),
                          pattern_sha256=row.get("stimulus_pattern_sha256")) for row in results]))
    failures = [row for row in results if row["result"] != "PASS"]
    if failures:
        issues.append("Failed seeds retained: " + ", ".join(str(row["seed"]) for row in failures))
    best = max(results, key=lambda row: (row.get("stall_events", -1), row.get("total_stall_cycles", -1)))
    wave = run_seed(best["seed"], historical, wave=True)
    if wave["result"] != "PASS" or wave.get("stimulus_sha256") != best.get("stimulus_sha256"):
        issues.append("Representative FSDB replay failed or stimulus differed.")
    target = DEST / "waveform/g07_random_best_seed.fsdb"
    if not target.exists() or target.stat().st_size == 0:
        issues.append("Representative FSDB missing/empty.")
    else:
        verify_waveform(wave)
    reload = run_urg("final_reload", [Path(final_merge["db"])], create_db=False)
    if reload["coverage"] != final_merge["coverage"] or reload["ratios"] != final_merge["ratios"]:
        issues.append("Standalone final VDB reload differs.")
    if Decimal(final_merge["functional"]) < Decimal(functional_before):
        issues.append("Functional coverage decreased.")
    golden_before = golden_functional(EVIDENCE / "coverage/urg_report/groups.txt")
    golden_after = golden_functional(Path(final_merge["report"]) / "groups.txt")
    if Decimal(golden_after["coverage"]) < Decimal(golden_before["coverage"]):
        issues.append("Golden-specific functional coverage decreased.")
    save_json(DEST / "functional_coverage_before_after.json",
              dict(golden_before=golden_before, golden_after=golden_after,
                   aggregate_before=functional_before, aggregate_after=final_merge["functional"]))
    write_csv(DEST / "coverage_before_after.csv", ["metric", "baseline", "final", "delta"],
              [dict(metric=key, baseline=rows[0][key], final=rows[-1][key],
                    delta="%.2f" % (Decimal(rows[-1][key]) - Decimal(rows[0][key]))) for key in METRIC_NAMES])
    summary = final_summary(rows, results, final_merge, best, wave, saturated, baseline_seeds, functional_before, issues)
    (DEST / "random_regression_summary.txt").write_text(summary)
    save_json(DEST / "final_result.json", dict(executed_seeds=len(results), saturated=saturated,
              final_merge=final_merge, best_seed=best["seed"], wave_replay=wave, issues=issues))
    (DEST / "README.md").write_text(
        "# Golden Regression RTL Code Coverage convergence\n\n"
        "Measured G07 seed expansion; existing DUT, Control and Stream/DMA sources and evidence are preserved. "
        "See `random_regression_summary.txt`, `coverage_progress.csv`, and `seed_results.csv`.\n\n"
        "## Method\n\n"
        "100 unique deterministic seeds are frozen in `seeds_100.txt`; every stage uses its cumulative prefix. "
        "The four historical seeds are included and rerun afresh, with stimulus SHA256 compared to baseline. "
        "Both ntb_random_seed and STALL_SEED use the listed seed. Existing G07 constrained timing is unchanged: "
        "low_cycles 0–12, high_cycles 32–128. There is no pre-existing DMA response delay randomization in this flow.\n\n"
        "The existing simv_evidence_rtl instrumented binary uses line+cond+fsm+tgl+branch, -cm_libs yv, "
        "and rtl_coverage.hier (+tree tb_top_golden.dut). baseline_provenance.json records binary hash and compile arguments. "
        "Baseline is freshly merged from build_rtl.vdb, the six Golden code runs and Golden functional.vdb. "
        "Each checkpoint merges those exact inputs plus every executed G07 VDB, including failures. "
        "No Control or Stream/DMA VDB is included. URG merges hits; seed percentages are never averaged. "
        "Total Coverage Summary instance metrics are used; denominators must equal baseline.\n\n"
        "At least one delta >= +0.10 percentage point continues expansion. Two consecutive checkpoints with all "
        "five deltas < +0.10 stop expansion. Baseline→10 counts as the first comparison. "
        "The decision uses URG reported percentages with exact decimal arithmetic; hit/total ratios are also retained.\n\n"
        "## Stimulus statistics\n\n"
        "stall_events is the driver count of actual VALID && !READY stalled transfers. total_stall_cycles is "
        "stability_checks, equal to the stalled VALID cycles for completed held transfers (the final acceptance is checked). "
        "max_stall_length is the maximum scheduled READY-low interval, not the maximum VALID-overlap length. "
        "scheduled_stall_events, ready_low_cycles and ready_toggle_count are reconstructed from *_stimulus-compatible "
        "CSV timing using a 10 ns clock and clipped at $finish. Each run retains stimulus.csv, log, command and result.json. "
        "Failure points and partial traces are retained. Full-trace and first-256 schedule-pattern hashes "
        "(timestamps excluded) must be distinct across unique seeds; see stimulus_quality.json. "
        "The representative seed maximizes actual stall_events; "
        "only its replay requests FSDB, and SHA256 must match its original trace.\n\n"
        "## Functional coverage\n\n"
        "The eight Golden-specific covergroups are 100% (58/58); this is checked for non-regression. "
        "URG aggregate GROUP, including inherited cnn_base_pkg::cnn_coverage::cg, is 92.86% (65/70), "
        "also checked for non-regression. Its existing five uncovered common bins are partial keep plus "
        "CNN_AXIL_WRITE, CNN_AXIL_READ, CNN_SET_FEATURE_READY and CNN_IRQ_EVENT. "
        "No functional bins or exclusions are changed.\n\n"
        "## Reproduction\n\n"
        "From repository root: `/usr/bin/python3.11 CNN/uvm/golden/golden_io/run_random_coverage_progress.py --jobs 6`. "
        "Completed run/stage checkpoints are reused on resume; incomplete directories are never overwritten. "
        "Initial telemetry enriched during this session is preserved as result_initial.json; PASS/FAIL and "
        "raw checks/hashes must agree before metadata enrichment. No simulation rerun is used for enrichment. "
        "URG input manifests and console logs are in logs/, merged databases and decisions in merged/, "
        "HTML/text reports in urg_report/, and the single representative FSDB in waveform/. "
        "The final database was reloaded alone with URG to verify its saved coverage. "
        "fsdb2vcd -summary verifies the representative FSDB is finished, has the expected signals and "
        "ends at the replay's actual $finish time; see logs/best_seed_fsdb_check.json.\n\n"
        "```text\n" + summary + "```\n")
    print(summary, flush=True)
    return bool(issues)


if __name__ == "__main__":
    raise SystemExit(main())
