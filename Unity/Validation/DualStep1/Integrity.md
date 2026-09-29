# Dual STEP 1 보존 검증

- PASS: mode 0→1 외 Dual scene 전체 text 불변
- PASS: Main 및 핵심 9개 파일, 외부 작업 파일/HEAD 불변

```text
DualRobotSystem
  RobotArm_L
    Base_Yaw
    __G51V2_BaseHub
    __G51V2_Servo_Base_Yaw
    __G51V2_Base
    BaseMesh
  DualArmDemoController
  RobotArm_R
    __G51V2_Servo_Base_Yaw
    Base_Yaw
    __G51V2_BaseHub
    __G51V2_Base
    BaseMesh
  HandoffPosition
  RedBall
  CenterFrame
    RightMount
    __DualShoulderSideV2_Right_ShoulderMount
    HorizontalBeam
    LeftMount
    Base
    BeamSlot
    __DualShoulderSideV2_Left_ShoulderMount
    ColumnSlot
    BeamSlot
    VerticalColumn
    ColumnSlot
```
