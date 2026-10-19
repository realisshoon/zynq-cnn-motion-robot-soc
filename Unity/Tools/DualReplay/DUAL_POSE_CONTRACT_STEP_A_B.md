# Dual Pose Contract — STEP A/B

## Purpose
This file defines the mock-CNN dual-arm input used before live FPGA CNN integration.

## Camera contract
- Resolution: 1280x720
- Origin: top-left
- +x: right
- +y: down
- Source cadence for this fixture: 20 Hz

## Body landmarks
The body CNN golden model already contains:
- joint 5: left shoulder
- joint 6: right shoulder
- joint 7: left elbow
- joint 8: right elbow
- joint 9: left wrist
- joint 10: right wrist

## Robot-side mapping
Because the camera faces the person:
- Human RIGHT arm -> viewer LEFT -> Unity RobotArm_L
- Human LEFT arm  -> viewer RIGHT -> Unity RobotArm_R

## Tracking-ready policy for this fixture
- Source video before 4.0 s: ACQUIRING / robot HOLD
- Source video 4.0..16.0 s: TRACKING READY
- The CSV starts at source-video 4.0 s, so pre-motion head/body settling is not emitted as robot commands.

This 4.0 s threshold is ONLY for this fixture.
The live system should replace it with a tracking-ready gate based on consecutive valid/stable frames.

## Independent arm validity
- frame_valid means the camera/CNN frame itself exists.
- shoulder_l/r validity is shared body-frame support.
- elbow_l/wrist_l and elbow_r/wrist_r are independently valid.
- If one arm is occluded, hold only that arm. The other arm continues updating.

## Fingers
The current body CNN golden model outputs 17 body joints, not finger landmarks.
Finger fields are therefore invalid in this fixture. Do not invent finger coordinates.
Wrist/gripper should use the existing HOLD/neutral policy until marker/hand data is available.
