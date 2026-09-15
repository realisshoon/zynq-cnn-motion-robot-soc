# Robot Arm 2D Control Interface

## Project Mode

Initial implementation uses a single human arm in a 2D plane.

Dynamic joints:

- Shoulder Pitch
- Elbow Pitch
- Wrist Pitch
- Gripper

Fixed joints in 2D mode:

- Base Yaw = neutral
- Wrist Roll = neutral

## Camera Coordinate

- +X: right
- +Y: down

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
