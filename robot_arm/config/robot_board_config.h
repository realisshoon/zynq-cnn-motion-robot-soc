#ifndef ROBOT_BOARD_CONFIG_H
#define ROBOT_BOARD_CONFIG_H

#include "firmware_config.h"

#if (defined(ROBOT_STEREO_LEFT) && defined(ROBOT_STEREO_RIGHT))
#error "Select exactly one stereo board role for split-board control."
#endif

#define ROBOT_RIGHT_FOREARM_CM 16.0f
#define ROBOT_RIGHT_WRIST_ROLL_END_CM 7.0f
#define ROBOT_RIGHT_ROLL_END_TIP_CM 13.0f
#define ROBOT_RIGHT_TABLE_Z_CM -10.0f

#define ROBOT_LEFT_FOREARM_CM 13.5f
#define ROBOT_LEFT_WRIST_ROLL_END_CM 6.0f
#define ROBOT_LEFT_ROLL_END_TIP_CM 13.0f
#define ROBOT_LEFT_TABLE_Z_CM -10.0f

#define ROBOT_LEFT_GRIPPER_CLOSE_MAX_US 2200U
#define ROBOT_LEFT_GRIPPER_OPEN_MIN_US 1722U

#endif
