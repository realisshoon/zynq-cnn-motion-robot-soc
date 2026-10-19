# CNN DMA / AXI-Stream verification

Verification source changes are confined to this directory. The root `.gitignore`
also contains explicitly authorized tool-artifact rules. RTL, common UVM,
tb_top and the current branch remain unchanged.
No commit, push, pull, merge or branch switch is part of this flow.

From the repository root:

```bash
bash CNN/uvm/dma_stream/scripts/compile.sh --fresh
python3 CNN/uvm/dma_stream/scripts/run_dma_stream_regression.py
bash CNN/uvm/dma_stream/scripts/report_coverage.sh
bash CNN/uvm/dma_stream/scripts/open_verdi_coverage.sh
```

`--fresh` preserves the development VDB by renaming it under output/coverage
and starts a clean final database. Each simulation uses its own `-cm_name`.
The regression has 18 runs: S01/S02, three S03 boundary lengths, four stage0
S04 seeds plus the same four full-flow seeds, zero/delayed S05, S06/S07/S08.
The script checks simulation exit, explicit scenario result, UVM counts and
VCS assertion summary. Missing results, wall timeout and nonzero assertion
failures are FAIL. Output includes `logs/regression_summary.txt` and JSON.
URG reads all named tests in the single cumulative VDB; this is not a guess
about merged scores. Use the generated text/HTML groups report to verify
scenario bin hits and every required group. `report_coverage.sh` also verifies
all eleven required groups, eight actual scenario bins and eighteen matching
VDB/URG/regression test records; it writes `functional_coverage_summary.txt`.

| Scenario | Stimulus | Independent checks |
|---|---|---|
| S01 | No disturbance, legal zero Conv0 parameters/black image | 120/69120/49152 accepted beats, image done/halt, SG and S2MM registers |
| S02 | Directed input VALID gaps 1/2/3 | Same anchors and six weight/image gap combinations |
| S03 | Original normal stalls 1/2/3 plus pre-accept final beat stall | Passive normal/last episodes, exactly one LAST acceptance, output count, stable payload assertions |
| S04 | Reproducible xorshift seeds 1/7/42/12345, gaps/stalls 1..3 | Stage0 anchors; full-flow feature MM2S gaps; overlap/recovery, every packet KEEP/LAST/count, all stages/mapping |
| S05 | Independently gate AW/W, stall AR, zero/three extra response cycles | Observed AW-first/W-first/same cycles, exact response latency, B/R reconstruction/OKAY, hold assertions exercised |
| S06 | Delay weight command response | ROM-derived source/length for all 29 operations, SR pending W1C/RUN/source/LENGTH order, stream after B commit, packet count/KEEP/LAST; directed Conv0 summary |
| S07 | One-cycle emulated DMA completion | Actual Idle read timestamp before output final accept; fully programmed S2MM before source; stage0 drain before transition |
| S08 | Independent command-driven weight/image/feature workers | 16 actual stages, 29 weight/15 input/14 output packets, body FM_A/B ping-pong, both heads reuse FM_B, final done/no error |

The original S03 sequence and its normal stall requests are retained. A helper
changes READY at negedge after acceptance 49151, before beat 49152 can be
accepted. It counts actual `VALID && LAST && !READY` cycles before release.
Three boundary variants fill the normal/last × 1/2/3 cross without ignored
bins. The passive observer samples completed, accepting recovery episodes.

Coverage `scenario_cg` has eight identity bins: a single test normally reports
12.5%; only the cumulative final VDB should report 100%. Identity is not proof
of correctness. Input gap mailboxes record completed intentional intervals;
output stalls and AXI-Lite order come from actual sampled handshakes. Feature
mapping samples actual B-committed addresses, and completion order samples
observed status/stream timestamps. Body arm coverage is separate because the
S07 directed run is stage0, while S04 full flow and S08 exercise body MM2S.
The existing common stream coverage is preserved; it includes a partial-KEEP
bin that full-byte CNN tensor lengths need not reach. Do not equate overall
code/common coverage with the required scenario-specific functional coverage.

`cnn_dma_command_checker` is separate from the common accepted-count path.
A second analysis connection checks each accepted stream item without double
counting it. Commands are reconstructed by the DMA master monitor at B/R
handshakes. A bind checker publishes the read-only debug stage to a package
variable for this single-DUT testbench; package code does not reach into RTL
hierarchy or change DUT state. Additional bind assertions check all three
input stream holds, output holds, AW/W/AR/B/R holds and Stage0 drain.

The responder models control/status only. The feeder emulates stream data;
there is no simulated Xilinx DMA/DDR data movement. Zero weight/bias/multiplier
parameters meet the RTL sign-extension and reserved-bit requirements. Image
traffic remains 144 rows × 480 beats, with TLAST per row. Generic packet lengths
are taken from actual committed LENGTH commands and final KEEP is calculated
from valid bytes. Numerical CNN accuracy and distinguishability of individual
identical zero payloads are not claimed: acceptance count, packet boundaries,
protocol stability, completion and DMA command/address correctness are checked.

Timeouts are bounded: a stalled feeder fails after 2M nonaccepting cycles,
full flow after 30M cycles, and each process after 300 wall seconds. The RTL's
existing progress watchdog remains enabled. Actual full-flow development runs
complete in approximately 100 CPU seconds; none of these limits disables or
hides a DUT fault.

Generated files under `output/` are not source artifacts and are not committed.

## Generated artifact containment

Scripts resolve their own repository path and accept invocation from any working
directory using an absolute script path. Compilation runs in `output/build`,
simulation in `output/gui/runs/<run>`, URG in `output/coverage`, and the Verdi
wrapper in `output/gui`. They preserve HOME and existing license/tool overrides.
Source file-list paths are resolved before changing the working directory.

| Directory | Contents |
|---|---|
| `output/bin` | Executable entry wrapper; preserved legacy executable |
| `output/build` | Actual executable, daidir, csrc, resolved source list |
| `output/logs` | Compile/run/report logs, summaries, cleanup audit |
| `output/coverage` | VDB databases and URG reports |
| `output/waves` | Run waveforms |
| `output/gui` | Verdi settings, sessions, auxiliary GUI artifacts |

`root_artifacts.py --relocate` moves only explicitly recognized untracked root
artifacts into timestamped `legacy_root` directories and records a manifest.
It never deletes files, replaces destinations, or moves tracked files. Every
simulation and the regression finish with a root audit. Unexpected untracked
root entries fail the audit; protected tracked entries produce a warning and
`ROOT CLEAN = NO` without invalidating simulation results. The existing tracked
`.vscode/settings.json` is such an exception and remains untouched.

The whole `output/` directory is ignored. Root fallback ignore rules list known
tool artifacts; arbitrary source `*.log` and `*.conf` files remain visible to Git.
