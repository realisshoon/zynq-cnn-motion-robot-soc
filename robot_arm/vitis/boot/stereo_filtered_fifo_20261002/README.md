# Filtered stereo FIFO release (2026-10-02)

Install only the matching role's `BOOT.BIN`: LEFT sends CNN coordinates; RIGHT
triangulates and controls the existing right arm. Two-arm actuation and camera
exposure synchronization are not implemented. PWM and experimental async control
start OFF. Never enable motion without supporting the arm and checking its space.

## Changes

- Per-camera, per-landmark pixel EMA before triangulation: 100 ms time constant.
  Verified exposure timestamps supply filter intervals when available; otherwise
  RIGHT-local receipt/CNN-completion intervals are used, not exposure timing.
- Invalid points are not filled from history. Reappearance, session/mode changes,
  non-increasing timestamps and intervals over 500 ms restart the filter. Pixel
  innovations over 30 px pass through unchanged and restart that landmark so a
  large movement is not clamped into a plausible small measurement.
- Default maximum reprojection error is 15 px on each camera. Existing angle/
  hand EMA, geometry checks and the 150 mm last-accepted 3D displacement guard remain.
- Experimental async `A` uses bounded eight-entry FIFO queues on each side without
  unverified receipt-age/gap limits. Full queues drop the oldest frame; `qdrop`
  records overflow. `qL`, `qR` and `time_gate=OFF_FIFO` explain this in `[ST]` logs.
  Verified exposure epoch/skew checks and default strict expiry are not bypassed.
- Boot color margins R/G/B are 50/30/50. Other settings and calibration are unchanged.

Smoothing and relaxed rejection do not fix mismatched exposures, calibration bias,
or physical camera movement. FIFO may process old measurements. `fresh=0` is a
diagnostic in async mode, not permission to claim fresh/synchronized acquisition.
Use stationary or slow-motion trials until real common exposure is implemented.

## Build and installation

Both Cortex-A9 applications use the existing dual-arm UART0 XSA and unchanged FSBL/
bitstream. Bootgen 2020.2 verifies three partitions per image. `SHA256SUMS.json`
contains package image hashes. Host regressions retain the five known failures;
stereo, buffering, filtering and UART tests pass. See repository stereo docs for
commands and coordinate conventions. Local capture/calibration scripts and photos
are intentionally excluded from the shared repository release.

Power off the board, back up its current SD-root `BOOT.BIN`, then copy the correct
role's image to that root and verify SHA256. Keep `WGT_V4.BIN` and all photographs.
No reset, PWM-enable or motion command is part of installation. SD deployment and
physical runtime verification are separate from building this package.
