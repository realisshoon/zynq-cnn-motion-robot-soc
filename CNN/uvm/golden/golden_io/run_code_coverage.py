#!/usr/bin/env python3
"""Collect code coverage after verifying nonempty RTL instrumentation shapes."""
import concurrent.futures
import gzip
import json
from pathlib import Path
import xml.etree.ElementTree as ET
import run_final_regression as regression


def main():
    regression.SIM_BINARY = regression.OUT / "bin" / "simv_evidence_rtl"
    build = regression.OUT / "coverage" / "runs" / "build_rtl.vdb"
    for metric in ("line", "cond", "branch", "fsm", "tgl"):
        path = build / "snps" / "coverage" / "db" / "shape" / (metric + ".verilog.shape.xml")
        data = path.read_bytes()
        if data.startswith(b'\x1f\x8b'):
            data = gzip.decompress(data)
        root = ET.fromstring(data)
        if len(root) <= 1:
            raise SystemExit("Empty RTL instrumentation for " + metric)
    cases = [("g06_frame_00125_code", "G06", "cnn_g06_multisample_test", 125, 1, [], 1)]
    cases += [("g07_seed_%d_code" % seed, "G07", "cnn_g07_random_stall_test", 121, seed, [], 1)
              for seed in (1, 11, 101, 1001)]
    cases += [("g05_atomic_code", "G05", "cnn_g05_atomic_publish_test", 121, 1,
               ["+FSDB_G05", "+FSDB_FILE=" + str(regression.EVIDENCE / "waveform" / "g05_atomic_publish_code.fsdb")], 2)]
    results = []
    manifest = regression.EVIDENCE / "reports" / "regression_code_runs.json"
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        futures = [pool.submit(regression.run_case, c) for c in cases]
        for future in concurrent.futures.as_completed(futures):
            results.append(future.result())
            manifest.write_text(json.dumps(results, indent=2) + "\n")
    print("CODE COVERAGE RUNS COMPLETE: %d/%d PASS" %
          (sum(r["result"] == "PASS" for r in results), len(results)), flush=True)
    return any(r["result"] != "PASS" for r in results)


if __name__ == "__main__":
    raise SystemExit(main())
