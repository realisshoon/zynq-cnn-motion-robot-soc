#include <stddef.h>
#include <math.h>

#include "common/robot_types.h"
#include "robot_calibration/robot_calibration.h"
#include "robot_calibration/robot_calibration_config.h"
#include "robot_calibration/motion_control.h"

// 이 파일 안에서만 쓰는, 즉 application에서 사용하지 않는 함수는 static이며 header에 포함하지 않는다.
// 결국 함수간, 모듈간 인터페이스가 된다.
// 각 raw data에 configuration data를 곱한다.
static float map_joint_angle(float human_deg, const JointCalibration *config)
{
    return human_deg
         * config->scale
         * (float)config->direction
         + config->zero_offset_deg;
}
// clamping, 최대/최소값을 제한한다.
static float clamp_value(float value, float min_value, float max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}
// configuration에 적용된 clamping을 적용한다.
static float clamp_joint_angle(float angle_deg, const JointCalibration *config)
{
    return clamp_value(angle_deg, config->min_deg, config->max_deg);
}

// 외부 함수는 header에 포함한다.
// 1st. 입력 유효 확인 함수
int motion_control_validate_target(const HumanJointTarget *target)
{
    // if invalid
    if(target == NULL) return 0;
    if(target->valid == 0) return 0;
    if(!isfinite(target->base_deg)) return 0;
    if(!isfinite(target->shoulder_deg)) return 0;
    if(!isfinite(target->elbow_deg)) return 0;
    if(!isfinite(target->wrist_pitch_deg)) return 0;
    if(!isfinite(target->wrist_roll_deg)) return 0;
    if(!isfinite(target->gripper_norm)) return 0;
    // else valid
    return 1;
}

// configuration에 따른 변환 수행
// gripper_norm은 agent1 -> agent2 -> agent3 구간에서 값이 바뀌면 안 되므로
// 보정 없이 그대로 복사만 한다.
void motion_control_map_target(const HumanJointTarget *input, JointCommand *output)
{
    output->base_deg = map_joint_angle(input->base_deg, &robot_calibration_config.base);
    output->shoulder_deg = map_joint_angle(input->shoulder_deg, &robot_calibration_config.shoulder);
    output->elbow_deg = map_joint_angle(input->elbow_deg, &robot_calibration_config.elbow);
    output->wrist_pitch_deg = map_joint_angle(input->wrist_pitch_deg, &robot_calibration_config.wrist_pitch);
    output->wrist_roll_deg = map_joint_angle(input->wrist_roll_deg, &robot_calibration_config.wrist_roll);
    output->gripper_norm = input->gripper_norm;
}
// limit 적용 함수
// gripper_norm은 그대로 전달하는 값이라 clamp 대상에서 제외한다.
void motion_control_apply_limits(JointCommand *command)
{
    if (command == NULL) return;
    command->base_deg = clamp_joint_angle(command->base_deg, &robot_calibration_config.base);
    command->shoulder_deg = clamp_joint_angle(command->shoulder_deg, &robot_calibration_config.shoulder);
    command->elbow_deg = clamp_joint_angle(command->elbow_deg, &robot_calibration_config.elbow);
    command->wrist_pitch_deg = clamp_joint_angle(command->wrist_pitch_deg, &robot_calibration_config.wrist_pitch);
    command->wrist_roll_deg = clamp_joint_angle(command->wrist_roll_deg, &robot_calibration_config.wrist_roll);
}
