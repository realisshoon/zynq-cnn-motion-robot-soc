# Native record/library boundary regressions

Run from PowerShell with MSVC C/C++ installed:

```powershell
./run.ps1
./run.ps1 -SourceRoot ../../.. -Case prepare_hal_settle_and_seed
```

The runner finds `src/record_replay/motion_library.c` in ancestor firmware roots,
an ancestor `source/`, or an ancestor `robot_arm/`. `-SourceRoot` overrides
discovery; `-VisualStudio` overrides `vswhere` compiler discovery. The example
explicit root is appropriate after copying this directory to
`robot_arm/tests/record_replay/boundaries/`. Source paths are never embedded in
the checked-in tests. Copy the authoring files below together when publishing.

| Authoring file | Purpose |
| --- | --- |
| `test_record_boundaries.c` | Fourteen named regression cases and independent clip audits |
| `run.ps1` | MSVC LEFT/RIGHT builds, input hashes, logs, failure exit codes |
| `harness.h` | Shared platform and board test-double declarations |
| `mocks/platform_cnn_stereo.c` | In-process platform/CNN/board boundaries |
| `mocks/fatfs_mock.c` | In-memory FatFS with injectable storage failures |
| `mocks/ff.h` | FatFS mock API |
| `mocks/xil_types.h` | Native Xilinx integer aliases |
| `mocks/xil_printf.h` | Captured firmware diagnostic output |
| `README.md` | Commands, scope, and interpretation |

The mock files originate from the split-board integration harness. The FatFS
capacity is enlarged to the production 2048-sample MRP1 payload, and the legacy
`0:/MOTION.BIN` path is accepted. All FatFS data stays in process memory. The
actual firmware driver uses its native register mock; no serial port, OBS,
physical PWM, or SD device is opened.

Each role compiles the actual pipeline, motion, safety, HAL, record/replay,
library, and SD implementation with `/std:c11 /W3 /WX /O2`. A generated include
under the run directory selects the actual `main_integration.c`. Its entry
point is renamed for testing, its event dispatcher is exercised directly, and
three bounded foreground-loop cases observe the actual target/gripper gates.
Library internals are not included or modified. Public query/report APIs and
the captured `REC_STATE` schema provide UI observations.

Coverage includes motion-to-START_RECORD preparation, stale/software-only
settling, HAL failure, three fresh settled ticks, two resting seed samples,
preparation cancellation, preserved existing follow flags, rejected new A,
and automatic tail reservation. Full-speed clips are independently audited
for range, speed, delta, acceleration, terminal stopping, gripper delta,
geometry safety, sample-by-sample HAL provenance, and byte-exact native replay.
An actual main-loop capacity case injects fresh targets and gripper inputs
throughout recording and proves their consumers stop during the reserved tail.

Rejected A preserves existing gripper, elbow, and wrist tracking state in
every pending UI phase through the actual main dispatcher. Accepted IDLE A
still calls the existing reset API.

NAME checks cover V refresh, S, P/UI_BUSY, rejected A, X, unsaved RAM retention,
and saving after PWM-off. Naming checks include one- and 24-character ASCII
names, malformed/non-ASCII names, duplicates, and cancel transitions. Eight
storage fault phases preserve the new RAM clip and existing binary file, clean
temporary files, and allow retry. Legacy MRP1 loading and saved-payload CRCs
are checked. Lower-level mid-motion recording remains rejected for replay;
delta, acceleration, terminal acceleration, gripper, finite, range, and safety
preflight rejection fixtures remain active. Register audits reject any write
to the other robot except disabling its CONTROL register and reject CH5 writes.

`runs/<timestamp>/` contains per-role `compile.rsp`, `build.cmd`, `build.log`,
`test.log`, and executables, plus before/after SHA-256 manifests and
`summary.json`. `latest_run.txt` points to the latest run. These generated files
are evidence, not authoring files to publish. Exit 0 means both roles pass;
exit 1 means build/test failure; exit 2 means inputs changed during a parallel
run. An unknown `-Case` also exits the test binary with code 2, recorded as a
test failure by the runner. `CHECK` remains active in optimized builds and
each case reports its own failures without hiding later cases.
