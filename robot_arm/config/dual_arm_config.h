#ifndef ROBOT_DUAL_ARM_CONFIG_H
#define ROBOT_DUAL_ARM_CONFIG_H

#ifndef ROBOT_SPLIT_BOARD_CONTROL
#define ROBOT_SPLIT_BOARD_CONTROL 0
#endif

#ifndef ROBOT_DUAL_ARM_ENABLE
#define ROBOT_DUAL_ARM_ENABLE 0
#endif

#if ROBOT_SPLIT_BOARD_CONTROL && ROBOT_DUAL_ARM_ENABLE
#error "Split-board control and legacy dual-arm control are mutually exclusive."
#endif

#if ROBOT_SPLIT_BOARD_CONTROL && defined(ROBOT_STEREO_LEFT) && defined(ROBOT_STEREO_RIGHT)
#error "Select exactly one stereo board role for split-board control."
#endif

#if ROBOT_DUAL_ARM_ENABLE && defined(ROBOT_STEREO_LEFT)
#error "Dual robot outputs belong to the RIGHT camera board only."
#endif

#define ROBOT_RIGHT_FOREARM_CM 16.0f
#define ROBOT_RIGHT_WRIST_ROLL_END_CM 7.0f
#define ROBOT_RIGHT_ROLL_END_TIP_CM 13.0f
#define ROBOT_RIGHT_TABLE_Z_CM -10.0f

#define ROBOT_LEFT_FOREARM_CM 13.5f
#define ROBOT_LEFT_WRIST_ROLL_END_CM 6.0f
#define ROBOT_LEFT_ROLL_END_TIP_CM 13.0f
#define ROBOT_LEFT_TABLE_Z_CM -10.0f

#define ROBOT_LEFT_GRIPPER_CLOSE_MAX_US 2400U
#define ROBOT_LEFT_GRIPPER_OPEN_MIN_US 1722U

#endif
