# Stereo person-axis release (2026-10-02)

The camera faces the person; the robot faces the same direction as the person.
Stereo A1 now uses fixed person-right/up/forward axes `(-camera X, +camera Y,
-camera Z)` instead of camera-right/up/forward. Forward-pointing forearms no longer
appear near +/-180 degrees merely because they point toward the camera.

Camera-origin measured positions and positive optical depth remain unchanged.
Only the A1 angle basis changes; wrist reference uses the same basis. No shoulder
BodyFrame, virtual depth or robot-base translation is introduced. This fixed
180-degree orientation convention does not claim measured six-axis camera/robot
extrinsic registration. Mono A1 and A2/A3 servo calibration/directions, limits,
home, trajectories and PWM mapping are unchanged.

The preceding release's pixel EMA, 15 px reprojection threshold, bounded async
FIFO, 150 mm last-accepted displacement gate and green-margin default 30 remain.
Exposure synchronization and two-arm actuation are still unimplemented. PWM and
async control boot OFF; installation does not send activation or motion commands.

- `left/BOOT.BIN`: LEFT coordinate sender.
- `right/BOOT.BIN`: RIGHT stereo A1/A2 processing and existing right-arm controller.
- `SHA256SUMS.json`: verified image and shared FSBL/bitstream hashes.
- Both Cortex-A9 builds and three-partition Bootgen headers are verified. Host
  regressions retain the five previously known failures; all stereo regressions
  pass, including eight absolute-angle groups and the recorded +13.248 degree yaw.
- See `docs/stereo_absolute_angles.md` for the coordinate and angle contract.

Power off the board before SD exchange. Back up its root `BOOT.BIN`, install only
the matching role's image and verify SHA256. Preserve weights and photographs.
Supported-arm physical direction verification is separate from host/build tests.
Local capture/calibration scripts, photos, logs and Vitis workspaces are excluded
from the shared Git release. Earlier dated releases remain historical artifacts.
