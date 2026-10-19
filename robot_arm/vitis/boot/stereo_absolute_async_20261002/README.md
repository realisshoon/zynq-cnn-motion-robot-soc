# Stereo absolute-coordinate BOOT release — 2026-10-02

## Images and setup

- `left/BOOT.BIN`: left-camera board, CNN coordinate sender.
- `right/BOOT.BIN`: right-camera board, UART receiver, calibrated stereo depth and fixed-rig A1 angles.
- Both images are 4,493,904 bytes and contain FSBL, the dual-arm UART0 bitstream, and their role-specific application.
- Hardware: `../../xsa/cnn_rgby_pack77_dual_arm_uart0.xsa`. Only the existing right-arm PWM is controlled; dual-arm operation is not implemented.
- Vitis/Vivado 2020.2; `ROBOT_TRACE`, `SERVO_PWM_DRIVER_USE_XILINX`, role-specific `ROBOT_STEREO_LEFT`/`ROBOT_STEREO_RIGHT`, `ROBOT_STEREO_UART_BAUD=115200`. No boot-time `ROBOT_ARM_PWM_ENABLE`.
- USB console UART1: 921600 8N1. Board link UART0: 115200 8N1.
- Power off the board before installing its role's image as SD-root `BOOT.BIN`. Retain a backup and the existing matching `WGT_V4.BIN`; weights are not included in this release.
- Expected existing weights SHA256: `77D4E3BB0E747AB5DF62AAA41A85784BE3215489941C6D271F401A50218E7DE3` (1,287,680 bytes).
- Calibration constants are in `src/stereo_vision/stereo_calibration.c`. Calibration photos, local capture/monitor tools, generated workspaces and test logs are intentionally not included.

## Behavior and limitations

PWM and asynchronous trial mode start OFF. LEFT cannot enable robot PWM or trial control.
The default strict stereo path still requires verified exposure metadata. There is no common camera exposure synchronization in this release.
RIGHT supports explicit `E` then `A` for experimental asynchronous binocular tracking; `V`/`T` report status, `S` stops new trial inputs, and `X` releases PWM torque.
These are UART single-character keys. Do not activate control until mechanical support, home/last-command position and workspace clearance are checked.
`S` lets the last approved trajectory finish and hold; it is not an emergency stop. `X` can let the arm fall.

Stereo angles use measured elbow/wrist mm coordinates in the fixed left-camera rig, not shoulder-derived BodyFrame or shoulder-width normalization.
This is not robot-base/world extrinsic registration. The single-camera path and Agent2 calibration remain unchanged.
See [absolute angle definitions](../../../docs/stereo_absolute_angles.md), [trial commands and gates](../../../docs/stereo_async_trial.md), and [UART reference](../../../docs/uart_reference.md).

## Validation

- Forced ARM rebuild and link of LEFT and RIGHT applications: PASS. XSCT produced applications but did not return promptly; the final binaries were independently rebuilt through generated makefiles with successful exit codes.
- Bootgen generation and header inspection: PASS, three images per BOOT.
- Host C runner: 25 suites, 20 PASS; the five pre-existing failures remain `test_integration_smoke`, `test_trace`, `test_axis_replay`, `test_forearm_calibration`, `test_forearm_replay`.
- New fixed-rig A1 test: seven groups PASS; asynchronous stereo and PWM command tests PASS.
- Local dual-console Python tool tests: 76 PASS; those tools are intentionally not distributed in this commit.
- No physical servo actuation or synchronized camera exposure validation was performed for this release.

## SHA256

| Artifact | SHA256 |
|---|---|
| LEFT BOOT.BIN | `8B806942A27010271B87E44500E6BFA0D3B216C1C5D5B0B2287350197DA6F058` |
| RIGHT BOOT.BIN | `01133204E0EBB83F0D7D9CE71DFDB9F2DB55A10CFA7D3892063C89C61E24CAF6` |
| LEFT application ELF (local build artifact) | `13D6CFE253CE1056A6FB70E5DB346CCA85FCA5936706930ED274405142BDD9AA` |
| RIGHT application ELF (local build artifact) | `BC00A8F0AA8A4CF2758B7B53CBD197D4F09CEFD910795BE410F0157E1AA82220` |
| Shared FSBL ELF (local build artifact) | `E9FB2FEB7AB3333763C99F8AEDE29F1547C858D068CB057584605E072BFD143B` |
| Shared bitstream (local build artifact) | `FB09DB29E04A947532F5982753006D623CDB4299A328AF08D06CCBB247834490` |
