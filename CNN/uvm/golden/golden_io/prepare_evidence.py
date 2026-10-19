#!/usr/bin/env python3
"""Record existing evidence and exact inputs without changing DUT or old runs."""
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

from run_final_regression import EVIDENCE, GOLDEN, OUT, ROOT, inspect_log


def main():
    selections = {
        "G01": ["g01_frame_%05d.log" % frame for frame in range(121, 126)],
        "G02": ["g02_checkpoint_dump.log"],
        "G03": ["g03_threshold_equal.log", "g03_threshold_plus1.log"],
        "G04": ["g04_multiframe_121_123.log"],
        "G05": ["g05_atomic_publish.log", "g05_atomic_publish_fsdb.log"],
    }
    audit = []
    for scenario, files in selections.items():
        target = EVIDENCE / "logs" / scenario.lower() / "prior_runs"
        target.mkdir(parents=True, exist_ok=True)
        for filename in files:
            original = OUT / "logs" / filename
            shutil.copy2(original, target / filename)
            expected = 3 if scenario == "G04" else 2 if scenario == "G05" else 1
            record = inspect_log(original, expected, scenario == "G05")
            record.update(scenario=scenario, log=str((target / filename).relative_to(EVIDENCE)),
                          sha256=hashlib.sha256(original.read_bytes()).hexdigest(),
                          modified_unix=original.stat().st_mtime)
            audit.append(record)
    (EVIDENCE / "reports" / "prior_runs_audit.json").write_text(json.dumps(audit, indent=2) + "\n")
    provenance = []
    paths = list((ROOT / "CNN" / "rtl").glob("*.v"))
    paths += [ROOT / "CNN" / "golden_py_model" / "cnn_model_v4.py"]
    paths += list(GOLDEN.rglob("*.sv"))
    paths += [GOLDEN / "files_golden.f", GOLDEN / "coverage" / "rtl_coverage.hier"]
    paths += list((OUT / "weight_stream").glob("*.hex"))
    paths += list((OUT / "image_hex").glob("*.hex"))
    paths += list((OUT / "python_golden").glob("frame_*/final_expected.hex"))
    paths += list((OUT / "python_golden" / "frame_00121" / "g03").glob("*.hex"))
    for path in sorted(set(paths)):
        provenance.append({"path": str(path.relative_to(ROOT)), "bytes": path.stat().st_size,
                           "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
    (EVIDENCE / "reports" / "input_source_sha256.json").write_text(json.dumps(provenance, indent=2) + "\n")
    build = EVIDENCE / "logs" / "toolchain"
    build.mkdir(parents=True, exist_ok=True)
    for name in ("evidence_compile.log", "evidence_compile_console.log", "vcs_help.txt", "urg_help.txt", "verdi_help.txt", "weight_stream_conversion.txt"):
        shutil.copy2(OUT / "logs" / name, build / name)
    (EVIDENCE / "reports" / "working_tree_at_audit.txt").write_text(
        subprocess.check_output(["git", "status", "--short"], cwd=ROOT, text=True))
    print("Prior logs audited:", len(audit))
    print("Source/input hashes:", len(provenance))
    if any(r["result"] != "PASS" for r in audit):
        raise SystemExit("A prior log failed evidence checks")


if __name__ == "__main__":
    main()
