#!/usr/bin/env python3
"""Generate reports from checked logs/URG output and package portable evidence."""
import csv
import hashlib
import json
import re
import shutil
import subprocess
import zipfile
from pathlib import Path
from run_final_regression import EVIDENCE, OUT, ROOT, GOLDEN


def write_csv(path, columns, rows):
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, columns, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def coverage_results():
    report = EVIDENCE / "coverage" / "urg_report"
    groups = []
    for line in (report / "groups.txt").read_text().splitlines():
        match = re.match(r"^\s*(\d+)\s+(\d+)\s+([\d.]+).*\s(cnn_golden_pkg::cnn_golden_coverage::\w+)\s*$", line)
        if match:
            groups.append(dict(group=match[4], covered=int(match[1]), expected=int(match[2]), percent=float(match[3])))
    if len(groups) != 8:
        raise SystemExit("Expected eight Golden covergroups in URG report")
    covered = sum(g["covered"] for g in groups)
    expected = sum(g["expected"] for g in groups)
    bins = []
    for block in re.split(r"^Group : ", (report / "grpinfo.txt").read_text(), flags=re.M)[1:]:
        group = block.splitlines()[0].strip()
        if "cnn_golden_pkg::cnn_golden_coverage::" not in group:
            continue
        variable = None
        bin_table = False
        for line in block.splitlines():
            match = re.match(r"Summary for Variable (\w+)", line)
            if match:
                variable = match[1]
                bin_table = False
            if variable and re.match(r"NAME\s+COUNT\s+AT LEAST", line):
                bin_table = True
                continue
            match = re.match(r"^(\S+)\s+(\d+)\s+(\d+)(?:\s+(\d+))?\s*$", line)
            if variable and bin_table and match:
                bins.append(dict(group=group, coverpoint=variable, bin=match[1], count=int(match[2]),
                                 at_least=int(match[3]), result="HIT" if int(match[2]) >= int(match[3]) else "MISS"))
    if len(bins) != expected:
        raise SystemExit("URG bin extraction incomplete: %d/%d" % (len(bins), expected))
    text = (report / "dashboard.txt").read_text()
    summary = text.split("Total Coverage Summary", 1)[1].split("Total Groups", 1)[0]
    first_row = next(line for line in summary.splitlines() if re.search(r"\d+/\d+", line))
    scores = re.findall(r"([\d.]+|--)\s+(\d+)/(\d+)", first_row)
    if len(scores) != 6 or any(int(total) == 0 for _, _, total in scores[:5]):
        raise SystemExit("Missing actual RTL coverage counts in URG dashboard")
    code = {name: {"percent": float(score), "covered": int(count), "expected": int(total)}
            for name, (score, count, total) in zip(("Line", "Condition", "Toggle", "FSM", "Branch", "All covergroups"), scores)}
    result = dict(functional_percent=100.0 * covered / expected,
                  functional_covered=covered, functional_expected=expected, groups=groups, code=code)
    write_csv(EVIDENCE / "coverage/functional/covergroup_summary.csv", ["group", "covered", "expected", "percent"], groups)
    write_csv(EVIDENCE / "coverage/functional/bin_hit_miss.csv", ["group", "coverpoint", "bin", "count", "at_least", "result"], bins)
    (EVIDENCE / "coverage" / "coverage_metrics.json").write_text(json.dumps(result, indent=2) + "\n")
    for filename in ("dashboard.txt", "hierarchy.txt", "modlist.txt"):
        if (report / filename).exists():
            shutil.copy2(report / filename, EVIDENCE / "coverage/code" / filename)
    lines = ["Functional Coverage (Golden intent only): %.2f%% (%d/%d bins)" % (result["functional_percent"], covered, expected)]
    lines += ["%s: %.2f%% (%d/%d)" % (name, code[name]["percent"], code[name]["covered"], code[name]["expected"])
              for name in ("Line", "Condition", "Branch", "FSM", "Toggle")]
    lines += ["All covergroups including common kind/keep/last: %.2f%%" % code["All covergroups"]["percent"],
              "RTL scope: tb_top_golden.dut and descendants; VCS default coverage semantics.",
              "No manual exclusion file or unreachable-bin waiver was applied.",
              "Golden functional aggregation: sum of covered Golden bins / sum of expected Golden bins."]
    lines += ["%s: %d/%d %.2f%%" % (g["group"], g["covered"], g["expected"], g["percent"]) for g in groups]
    misses = [b for b in bins if b["result"] == "MISS"]
    lines.append("Golden functional MISS: " + (", ".join(b["coverpoint"] + "." + b["bin"] for b in misses) or "NONE"))
    (EVIDENCE / "coverage/coverage_summary.txt").write_text("\n".join(lines) + "\n")
    return result


def documents(coverage, runs, code_runs):
    code = coverage["code"]
    code_line = ", ".join("%s %.2f%%" % (name, code[name]["percent"]) for name in ("Line", "Condition", "Branch", "FSM", "Toggle"))
    g07 = [r for r in runs if r["scenario"] == "G07" and "repeat" not in r["name"]]
    repeat = next(r for r in runs if r["name"] == "g07_seed_1_repeat")
    first = next(r for r in g07 if r["seed"] == 1)
    if not first.get("stimulus_sha256") or first["stimulus_sha256"] != repeat.get("stimulus_sha256"):
        raise SystemExit("seed=1 stimulus reproduction was not bit identical")
    g02 = (EVIDENCE / "reports/g02_bitexact_summary.txt").read_text()
    if "G02 RESULT : 29/29 OP PASS" not in g02 or "5174016/5174016 bit-exact matched" not in g02:
        raise SystemExit("Missing actual full G02 comparison result")
    rows = [
        ("G01", "Final E2E Golden", "실제 RGB 입력의 최종 Joint와 metadata 검증", "5/5 frames; 17/17 joints per frame", "logs/g01/prior_runs"),
        ("G02", "29-OP Checkpoint", "중간 tensor 전체를 Python Integer Golden과 tolerance=0 비교", "29/29 OP; 5174016/5174016 elements bit-exact", "reports/g02_bitexact_summary.txt"),
        ("G03", "Threshold Boundary", "score=98에서 equal-valid / plus-one-invalid 검증", "threshold98=620FE2BF; threshold99=62000000", "logs/g03"),
        ("G04", "Multi-Frame", "reset 없이 연속 frame과 RESULT_SEQ 진행 검증", "frames121/122/123; RESULT_SEQ1/2/3; each17/17", "logs/g04"),
        ("G05", "Atomic Publish", "계산 중 public 결과 유지와 ping-pong publish 검증", "2 publishes; bank0->1->0; seq0->1->2", "logs/g05"),
        ("G06", "Multi-Sample", "여러 실제 입력에 대해 동일 Golden checker regression", "PASS5; FAIL0; frames121~125", "reports/g06_summary.csv"),
        ("G07", "Random Stall", "bounded READY backpressure와 stream 안정성 검증", "4/4 seeds PASS; seed1 repeat identical; stall0~12 cycles", "reports/g07_summary.csv"),
    ]
    matrix = [dict(ID=id_, Scenario=scenario, Purpose=purpose, Result="PASS", **{"Main Metric": metric, "Evidence Path": path})
              for id_, scenario, purpose, metric, path in rows]
    write_csv(EVIDENCE / "verification_matrix.csv", ["ID", "Scenario", "Purpose", "Result", "Main Metric", "Evidence Path"], matrix)
    ppt = "\n".join([
        "- SystemVerilog/UVM 1.2 기반 CNN RTL Golden functional verification G01~G07 완료.",
        "- Python Integer Golden Model과 전체 29개 OP를 비교하여 5,174,016/5,174,016 tensor element bit-exact 일치.",
        "- Multi-Frame·Atomic Publish·5개 실제 RGB sample·4개 random stall seed 검증 PASS, UVM_ERROR/FATAL=0.",
        "- Golden Functional Coverage %.2f%% (%d/%d bins), VCS/URG/Verdi coverage 증빙 확보." % (coverage["functional_percent"], coverage["functional_covered"], coverage["functional_expected"]),
        "- RTL Code Coverage: " + code_line + ".",
    ]) + "\n"
    (EVIDENCE / "ppt_summary.md").write_text(ppt)
    summary = "# G01~G07 Verification Result\n\n"
    summary += "\n".join("- **%s %s: PASS** — %s" % (r[0], r[1], r[3]) for r in rows) + "\n\n"
    summary += "이번 실행: functional regression 16회, 실제 RTL 계측 regression 6회. 모든 실행의 UVM_ERROR/FATAL=0. 기존 로그 11개도 별도 보존.\n\n"
    summary += "Functional Coverage %.2f%%. %s.\n\n" % (coverage["functional_percent"], code_line)
    summary += "G02의 checkpoint hit coverage는 OP 관찰 여부이며, bit-exact 일치 여부는 Python tensor 비교 보고서로 입증한다.\n\n"
    summary += "Golden functional verification 범위의 결과이며 synthesis/timing/board 검증 결과를 포함하지 않는다.\n"
    (EVIDENCE / "reports/g01_g07_summary.md").write_text(summary)
    wave = EVIDENCE / "waveform"
    g05 = (EVIDENCE / "logs/g05/g05_coverage.log").read_text()
    transitions = [line for line in g05.splitlines() if "FRAME START #" in line or "ATOMIC PUBLISH #" in line]
    starts = [int(re.search(r"@ (\d+):", line)[1]) for line in transitions if "FRAME START #" in line]
    pubs = [int(re.search(r"@ (\d+):", line)[1]) for line in transitions if "ATOMIC PUBLISH #" in line]
    if len(starts) != 2 or len(pubs) != 2:
        raise SystemExit("Missing G05 waveform event timestamps")
    waveform_doc = f"""# Waveform Evidence

FSDB는 VCS/Verdi PLI로 실제 simulation에서 생성했다. GUI screenshot은 생성하지 않았다. `fsdbreport`로 추출한 CSV를 함께 제공한다.

## G05 Atomic Publish

`g05_atomic_publish.fsdb`의 scope는 `tb_top_golden.g05_if`이다.
두 번째 frame 계산 구간: {starts[1]}~{pubs[1]} ps ({starts[1]/1000:.3f}~{pubs[1]/1000:.3f} ns).
이 구간에서 published_bank=1, RESULT_SEQ=1, RESULT_FRAME_ID=121이며 public Joint/flags가 유지된다.
두 번째 publish: {pubs[1]} ps. bank 1→0, RESULT_SEQ 1→2, RESULT_FRAME_ID 121→122, public result가 새 bank 값으로 전환된다.
첫 publish: {pubs[0]} ps. bank 0→1, RESULT_SEQ 0→1, RESULT_FRAME_ID 0→121.
관찰 권장: 두 번째 publish 전후 ±200ns. `g05_publish_window.csv`는 이 구간을 FSDB에서 추출한 값이다.

필수 신호: busy, done_pending, published_bank, result_seq, result_frame_id, bank0_joints, bank1_joints, joint_words, bank0_flags, bank1_flags, joint_flags.
Joint0는 각 joints bus의 [31:0]을 표시한다.

## G07 Random Stall

`g07_random_stall.fsdb`는 seed=1, frame121의 실제 실행이다. scope: `tb_top_golden.g07_if`.
valid/data는 8385ns부터 유지되고 ready는 8400ns에서 0→1로 바뀐다. 8405ns posedge에서 transfer가 accept된다.
`g07_stall_window.csv`: 8200~8550ns. data=0x3f0d005700670009, keep=0xff, last=0이 stall 동안 유지된다.
로그의 첫 관찰 stall: 8395ns. 이 시각은 clocking block의 pre-edge sampling에 해당한다.
신호: clk, rst_n, valid, ready, data, keep, last. 전체 simulation의 stability checker는 VALID/data/keep/last를 검사한다.

## Open from repository root

```bash
verdi -ssf CNN/uvm/golden/output/final_evidence/waveform/g05_atomic_publish.fsdb
verdi -ssf CNN/uvm/golden/output/final_evidence/waveform/g07_random_stall.fsdb
```

설치된 `verdi -help`의 `-ssf` 옵션을 확인했다. GUI는 DISPLAY가 있는 Linux 또는 원격 GUI 세션에서 연다.
"""
    (wave / "README.md").write_text(waveform_doc)
    (wave / "signal_list.txt").write_text("\n".join(
        ["tb_top_golden.g05_if." + name for name in ("busy", "done_pending", "published_bank", "result_seq", "result_frame_id", "bank0_joints", "bank1_joints", "joint_words", "bank0_flags", "bank1_flags", "joint_flags")]
        + ["tb_top_golden.g07_if." + name for name in ("clk", "rst_n", "valid", "ready", "data", "keep", "last")]) + "\n")
    (EVIDENCE / "reports/g05_events.txt").write_text("\n".join(transitions) + "\n")
    stats = [dict(seed=r["seed"], stalls=int(r["stall_stats"][1]), accepted=int(r["stall_stats"][2]),
                  stability_checks=int(r["stall_stats"][3]), schedules=int(r["stall_stats"][4]),
                  stimulus_hash=r["stall_stats"][5], stimulus_sha256=r["stimulus_sha256"]) for r in g07]
    write_csv(EVIDENCE / "reports/g07_stall_metrics.csv", list(stats[0]), stats)
    (EVIDENCE / "reports/g07_reproducibility.txt").write_text(
        "seed=1 original and repeat stimulus CSV byte-for-byte identical: PASS\nSHA256=" + first["stimulus_sha256"] + "\n")
    notion = "# CNN RTL UVM Golden Verification\n\n## Verification Environment\n\n"
    notion += "SSH Linux local repository, branch val/cnn/golden. SystemVerilog, UVM 1.2, VCS W-2024.09-SP2, Verdi X-2025.06-1. Golden Model: CNN/golden_py_model/cnn_model_v4.py. DUT RTL 기능 변경 없이 검증 환경만 확장.\n\n"
    notion += "## G01~G07 Scenario\n\n" + "\n".join("- %s %s: PASS — %s" % (r[0], r[1], r[3]) for r in rows) + "\n\n"
    notion += "## Golden Bit-Exact Result\n\n29/29 OP PASS. 5,174,016/5,174,016 Tensor Element bit-exact matched, tolerance=0, mismatch=0. G06 실제 RGB frame121~125에서 각각 17/17 Joint 및 metadata 검증 PASS.\n\n"
    notion += "## Functional Coverage\n\nGolden intent covergroup: %.2f%% (%d/%d bins). G01~G07, OP0~28, threshold boundary, seq1~3, bank 양방향 publish, 5개 frame, stall 길이, valid/ready 네 상태를 실제 sampling으로 측정. 상세 HIT/MISS: coverage/functional/bin_hit_miss.csv.\n\n" % (coverage["functional_percent"], coverage["functional_covered"], coverage["functional_expected"])
    notion += "## RTL Code Coverage\n\n" + code_line + ". DUT 계층만 계측. -cm_libs yv로 라이브러리 RTL 포함. URG 보고서와 standalone merged VDB 재개방 검증 완료.\n\n"
    notion += "## G05 Atomic Publish Waveform\n\n" + f"Frame122 계산 중 {starts[1]/1000:.3f}~{pubs[1]/1000:.3f}ns에서 Frame121 public result 유지. {pubs[1]/1000:.3f}ns publish에서 bank1→0, seq1→2, frame_id121→122. FSDB와 정확한 time window 제공.\n\n"
    notion += "## G07 Random Stall Waveform\n\nSeed1의 8385~8405ns에서 valid=1, ready=0 동안 data/keep/last 유지. 8400ns ready 상승 후 8405ns transfer accept. 네 seed PASS, seed1 동일 stimulus 재현 PASS.\n\n"
    notion += "## Final Result\n\nCNN RTL Golden Verification G01~G07 완료. UVM_ERROR/FATAL=0. Functional/code coverage, URG HTML, Verdi VDB, FSDB, PPT 요약을 ZIP 하나에 정리. Golden functional verification은 timing/synthesis/board 검증과 별도 범위다.\n\n"
    notion += "## PPT Summary\n\n" + ppt
    (EVIDENCE / "notion/final_project_uvm.md").write_text(notion)
    readme = f"""# CNN RTL UVM Golden Verification Evidence

G01~G07 실제 PASS. Golden tensor: 29/29 OP, 5,174,016/5,174,016 elements bit-exact.
Golden Functional Coverage: {coverage['functional_percent']:.2f}% ({coverage['functional_covered']}/{coverage['functional_expected']} bins).
RTL coverage: {code_line}.

## Windows에서 확인

ZIP을 압축 해제한 뒤 `final_evidence/coverage/urg_report/dashboard.html`을 브라우저로 연다.
`css/`, `js/`, 나머지 HTML을 함께 유지한다. 로컬 링크로 report 세부 페이지가 연결된다.
빠른 확인: `ppt_summary.md`, `verification_matrix.csv`, `reports/g01_g07_summary.md`, `coverage/coverage_summary.txt`.
숫자 원본: `coverage/urg_report/dashboard.txt`, `groups.txt`, `grpinfo.txt`.
G06/G07: `reports/g06_summary.csv`, `reports/g07_summary.csv`, `reports/g07_stall_metrics.csv`.

## Coverage DB / Verdi

standalone merged database: `coverage/merged/golden_merged.vdb`.
이 DB 하나를 URG로 다시 열어 RTL/functional report가 생성되는 것을 확인했다.
서버 repository root에서:

```bash
verdi -cov -covdir CNN/uvm/golden/output/final_evidence/coverage/merged/golden_merged.vdb
verdi -ssf CNN/uvm/golden/output/final_evidence/waveform/g05_atomic_publish.fsdb
verdi -ssf CNN/uvm/golden/output/final_evidence/waveform/g07_random_stall.fsdb
```

설치된 `verdi -help`에서 `-cov`, `-covdir`, `-ssf`를 확인했다. GUI 실행에는 DISPLAY가 있는 세션을 사용한다.
Coverage mode에서 Design 페이지로 DUT 계층과 Line/Condition/Branch/FSM/Toggle을 본다.
Groups 페이지에서 `cnn_golden_pkg::cnn_golden_coverage::*`를 선택해 coverpoint/bin HIT/MISS를 본다.
Summary에는 common `cnn_coverage::cg`도 포함된다. Golden intent 수치는 Golden 8개 그룹의 bin을 합산한 비율이며 common kind/keep/last 수치와 구분한다.

## 실제 측정 범위

Functional regression 16회 + RTL 계측 regression 6회. 원본 실행 command/seed/DB/로그 경로는 `reports/regression_runs.json`, `reports/regression_code_runs.json`에 있다.
기존 G01~G05 로그는 `logs/g01`~`logs/g05`의 `prior_runs/`에 보존했다.
G02는 기존 checkpoint 및 이번 새 checkpoint를 Python Integer Golden tensor와 tolerance=0으로 재비교했다.
RTL scope는 `tb_top_golden.dut` 전체 하위 계층. 수동 coverage waiver/exclusion은 적용하지 않았다.
초기 빌드는 -y library RTL이 VCS 기본 설정상 계측되지 않았다. 이를 -cm_libs yv로 보완한 빌드의 실제 code data를 사용했다.
초기 실행의 functional data는 group metric만 가져오고, RTL 계측 실행 data와 union merge했다. 최종 DB의 standalone 재개방 로그는 `logs/toolchain/urg_merged_reload_console.log`.
FSM은 VCS가 추출한 FSM의 coverage이며 모든 RTL 상태기의 완전성 지표로 확대 해석하지 않는다.

## Waveform

`waveform/README.md`의 신호와 정확한 time window를 사용한다.
G05 probe와 G07 stream probe만 dump하여 기존 4.3GB FSDB보다 작은 파일을 만들었다.
GUI screenshot은 생성하지 않았으며 실제 FSDB 및 FSDB에서 추출한 CSV를 제공한다.

## Reproduce

```bash
bash CNN/uvm/golden/golden_io/build_evidence.sh _rtl
/usr/bin/python3.11 CNN/uvm/golden/golden_io/run_code_coverage.py
/usr/bin/python3.11 CNN/uvm/golden/golden_io/merge_evidence_coverage.py
```

이미 제공된 산출물 경로로 재실행하면 같은 이름의 새 산출물을 기록하므로 별도 output 사본에서 실행하는 것을 권장한다.
모든 simulator 실행 cwd는 golden/output이다. DUT RTL, common UVM source, 기존 로컬 수정은 유지했고 commit/push/merge는 수행하지 않았다.
Notion용 완성 문서: `notion/final_project_uvm.md`. Notion 직접 반영 결과는 `reports/notion_publish_status.txt`에 기록한다.
검증 범위: CNN RTL Golden functional verification. Synthesis, timing, board, 전체 SoC sign-off는 별도다.
"""
    (EVIDENCE / "README.md").write_text(readme)


def package():
    required = ["README.md", "ppt_summary.md", "verification_matrix.csv", "reports/g06_summary.csv",
                "reports/g07_summary.csv", "reports/g02_bitexact_summary.txt", "coverage/coverage_summary.txt",
                "coverage/urg_report/dashboard.html", "coverage/urg_report/dashboard.txt",
                "waveform/g05_atomic_publish.fsdb", "waveform/g07_random_stall.fsdb", "waveform/README.md",
                "notion/final_project_uvm.md"]
    for name in required:
        if not (EVIDENCE / name).is_file() or not (EVIDENCE / name).stat().st_size:
            raise SystemExit("Missing/empty evidence: " + name)
    broken = [str(p) for p in EVIDENCE.rglob("*") if p.is_symlink() and not p.exists()]
    if broken:
        raise SystemExit("Broken links: " + str(broken))
    hashes = []
    for path in sorted(EVIDENCE.rglob("*")):
        if path.is_file() and path.name != "artifact_sha256.txt":
            hashes.append(hashlib.sha256(path.read_bytes()).hexdigest() + "  " + str(path.relative_to(EVIDENCE)))
    (EVIDENCE / "artifact_sha256.txt").write_text("\n".join(hashes) + "\n")
    target = OUT / "uvm_golden_final_evidence.zip"
    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED, compresslevel=6, allowZip64=True) as archive:
        for path in sorted(EVIDENCE.rglob("*")):
            if path.is_file():
                archive.write(path, str(Path("final_evidence") / path.relative_to(EVIDENCE)))
    with zipfile.ZipFile(target) as archive:
        if archive.testzip() is not None:
            raise SystemExit("ZIP CRC check failed")
        names = archive.namelist()
        for name in required:
            if "final_evidence/" + name not in names:
                raise SystemExit("ZIP entry missing: " + name)
        (OUT / "uvm_golden_final_evidence.zip.list.txt").write_text("\n".join(names) + "\n")
    digest = hashlib.sha256(target.read_bytes()).hexdigest()
    target.with_suffix(".zip.sha256").write_text(digest + "  " + target.name + "\n")
    print("ZIP:", target)
    print("Bytes:", target.stat().st_size)
    print("SHA256:", digest)


def main():
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--zip-only", action="store_true")
    args = parser.parse_args()
    if args.zip_only:
        package()
        return
    runs = json.loads((EVIDENCE / "reports/regression_runs.json").read_text())
    code_runs = json.loads((EVIDENCE / "reports/regression_code_runs.json").read_text())
    if len(runs) != 16 or len(code_runs) != 6 or any(r["result"] != "PASS" for r in runs + code_runs):
        raise SystemExit("Regressions incomplete or failing")
    coverage = coverage_results()
    documents(coverage, runs, code_runs)
    merged = EVIDENCE / "coverage/merged/golden_merged.vdb"
    shutil.copytree(OUT / "coverage/merged/golden_merged.vdb", merged, dirs_exist_ok=True)
    toolchain = EVIDENCE / "logs/toolchain"
    for pattern in ("urg_*console.log", "evidence_compile_rtl*log", "g*_fsdb*txt", "*fsdb*console.log"):
        for path in (OUT / "logs").glob(pattern):
            shutil.copy2(path, toolchain / path.name)
    (EVIDENCE / "reports/final_working_tree.txt").write_text(subprocess.check_output(["git", "status", "--short"], cwd=ROOT, text=True))
    print((EVIDENCE / "coverage/coverage_summary.txt").read_text())
    print("Documents ready; publish Notion and then run --zip-only")


if __name__ == "__main__":
    main()
