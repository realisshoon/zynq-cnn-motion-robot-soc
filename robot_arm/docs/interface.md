# Robot Arm 2D Control Interface

## Project Mode

The initial 2D-only mode uses a single human arm in a plane.
The current Agent1 implementation also estimates base yaw and wrist roll.
Its definitive angle contract is in [coordinate_system.md](coordinate_system.md).

Dynamic joints:

- Shoulder Pitch
- Elbow Pitch
- Wrist Pitch
- Gripper

Fixed joints in 2D mode:

- Base Yaw = neutral
- Wrist Roll = neutral

## Input Image Coordinate (pixels, not reconstructed 3D)

- +X: right
- +Y: down

Agent1 reconstructed camera 3D uses +Y up and +Z away from the camera.
See the linked body-frame and angle definitions before mapping to robot angles.

## Robot 2D Coordinate

- +X: forward / horizontal
- +Z: up

## Software Pipeline

HumanPose2D
→ Human Target Angle
→ HumanJointTarget
→ Robot Calibration
→ JointCommand
→ Output Controller
→ Robot Arm

## Gripper

Finger1 and Finger2 are supplied from color detection.

- gripper_norm = 0.0 : Close
- gripper_norm = 1.0 : Open
