#ifndef ROBOT_CALIBRATION_CONFIG_H
#define ROBOT_CALIBRATION_CONFIG_H

#include "common/robot_types.h"

// 하나의 관절에 대한 보정
typedef struct {
    float scale; // 사람 관절 움직임의 크기 조정
    int direction; // 회전 방향 반전, 1 또는 -1
    float zero_offset_deg; // 로봇 관절의 중립 위치 보정
    float min_deg; // 로봇 관절 최소 허용각
    float max_deg; // 로봇 관절 최대 허용각
    float max_delta_deg; // 로봇 관절 최대 변화각
} JointCalibration;

// 전체 관절에 대한 보정
// 그리퍼(gripper)는 agent1이 0/1로 양자화해서 보내고 agent2는 값을 그대로
// 전달만 하므로 여기 보정 대상에 포함하지 않는다.
typedef struct {
    JointCalibration base;
    JointCalibration shoulder;
    JointCalibration elbow;
    JointCalibration wrist_pitch;
    JointCalibration wrist_roll;
} RobotCalibrationConfig;

extern const RobotCalibrationConfig robot_calibration_config;

#endif