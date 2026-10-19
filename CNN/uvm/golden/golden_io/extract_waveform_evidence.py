#!/usr/bin/env python3
"""Check finished FSDBs and export actual signal values using installed tools."""
import os
import csv
import re
import subprocess
from pathlib import Path
from run_final_regression import OUT, EVIDENCE


def main():
    verdi_bin = Path(os.environ["VERDI_HOME"]) / "bin"
    wave = EVIDENCE / "waveform"
    for id_ in ("g05_atomic_publish", "g07_random_stall"):
        path = wave / (id_ + ".fsdb")
        summary = subprocess.check_output([str(verdi_bin / "fsdb2vcd"), str(path), "-summary"], cwd=OUT, stderr=subprocess.STDOUT, text=True)
        (OUT / "logs" / (id_ + "_fsdb_summary.txt")).write_text(summary)
        if not re.search(r"file status\s*:\s*finished", summary):
            raise SystemExit("FSDB not finished: " + str(path))
    log = (EVIDENCE / "logs/g05/g05_coverage.log").read_text()
    publish = [int(m[1]) for m in re.finditer(r"@ (\d+):.*ATOMIC PUBLISH #", log)]
    if len(publish) != 2:
        raise SystemExit("Expected two actual publish timestamps")
    base = "/tb_top_golden/g05_if/"
    names = ("busy", "done_pending", "published_bank", "result_seq", "result_frame_id", "bank0_joints", "bank1_joints", "joint_words", "bank0_flags", "bank1_flags", "joint_flags")
    command = [str(verdi_bin / "fsdbreport"), str(wave / "g05_atomic_publish.fsdb"),
               "-bt", str(max(0, publish[1] - 200000)) + "ps", "-et", str(publish[1] + 200000) + "ps",
               "-of", "h", "-s"] + [base + name for name in names] + ["-csv", "-o", str(wave / "g05_publish_window.csv"), "-nolog"]
    result = subprocess.run(command, cwd=OUT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (OUT / "logs/g05_fsdb_extract_console.log").write_text(result.stdout)
    if result.returncode or not (wave / "g05_publish_window.csv").is_file():
        raise SystemExit("G05 FSDB extraction failed")
    # Exact trace used to verify bank selector, seq, frame and public result are
    # constant during frame122 and all switch at one publish timestamp.
    command = [str(verdi_bin / "fsdbreport"), str(wave / "g05_atomic_publish.fsdb"), "-of", "h", "-s"]
    command += [base + name for name in ("busy", "done_pending", "published_bank", "result_seq", "result_frame_id", "joint_words", "joint_flags")]
    command += ["-csv", "-o", str(wave / "g05_public_result_trace.csv"), "-nolog"]
    result = subprocess.run(command, cwd=OUT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (OUT / "logs/g05_public_trace_console.log").write_text(result.stdout)
    if result.returncode:
        raise SystemExit("G05 public trace extraction failed")
    with (wave / "g05_public_result_trace.csv").open() as handle:
        rows = list(csv.DictReader(handle))
    previous = None
    observed_publish = []
    public_fields = ("result_frame_id", "joint_words", "joint_flags")
    for row in rows:
        if int(row["Time(1ps)"]) == 0:
            continue
        seq = int(row[base + "result_seq"], 16)
        bank = int(row[base + "published_bank"], 16)
        if previous is not None:
            old_seq = int(previous[base + "result_seq"], 16)
            if seq == old_seq:
                if bank != int(previous[base + "published_bank"], 16) or any(row[base + field] != previous[base + field] for field in public_fields):
                    raise SystemExit("FSDB public result/bank changed outside publish")
            else:
                if seq != old_seq + 1 or bank == int(previous[base + "published_bank"], 16) or row[base + "done_pending"] != "1":
                    raise SystemExit("FSDB non-atomic publish transition")
                frame = int(row[base + "result_frame_id"], 16)
                expected = (OUT / ("python_golden/frame_%05d/final_expected.hex" % frame)).read_text().split()
                packed = int("".join(expected[:17][::-1]), 16)
                if int(row[base + "joint_words"], 16) != packed or int(row[base + "joint_flags"], 16) != int(expected[17], 16):
                    raise SystemExit("FSDB published joints/flags disagree with Golden")
                observed_publish.append(int(row["Time(1ps)"]))
        previous = row
    if observed_publish != publish:
        raise SystemExit("FSDB publish timestamps differ from monitor log")
    (EVIDENCE / "reports/g05_fsdb_atomic_check.txt").write_text(
        "PASS: FSDB public result and bank selector changed only when RESULT_SEQ advanced.\n"
        "PASS: both published 544-bit joint buses and flags match Python Golden.\n"
        "PASS: FSDB publish times match actual UVM monitor log.\n"
        "Publish time_ps=" + ",".join(map(str, observed_publish)) + "\n")
    print("Finished FSDBs and actual G05 signal export: PASS")


if __name__ == "__main__":
    main()
