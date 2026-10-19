#!/usr/bin/env bash
set -euo pipefail
golden_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd -- "$golden_dir/../../.." && pwd)"
output_dir="$golden_dir/output"
build_suffix="${1:-}"
mkdir -p "$output_dir"/{bin,build,logs,coverage/runs}
# Convert repository-relative file-list entries so compilation also runs inside
# output and places VCS FSM schedules and helper files below output.
/usr/bin/python3.11 - "$repo_dir" "$golden_dir/files_golden.f" "$output_dir/build/files_evidence_absolute.f" <<'PY'
import sys
from pathlib import Path
root, source, target = map(Path, sys.argv[1:])
lines = []
for line in source.read_text().splitlines():
    for prefix in ("+incdir+", "-y ", ""):
        if line.startswith(prefix + "CNN/"):
            line = prefix + str(root / line[len(prefix):])
            break
    lines.append(line)
target.write_text("\n".join(lines) + "\n")
PY
cd -- "$output_dir"
vcs -full64 -sverilog -timescale=1ns/1ps -ntb_opts uvm-1.2 \
    +define+FSDB -debug_access+all -kdb \
    -cm line+cond+fsm+tgl+branch -cm_libs yv \
    -cm_hier "$golden_dir/coverage/rtl_coverage.hier" \
    -cm_dir "coverage/runs/build${build_suffix}.vdb" -f build/files_evidence_absolute.f \
    -top tb_top_golden -o "bin/simv_evidence${build_suffix}" -Mdir="build/csrc_evidence${build_suffix}" \
    -l "logs/evidence_compile${build_suffix}.log"
