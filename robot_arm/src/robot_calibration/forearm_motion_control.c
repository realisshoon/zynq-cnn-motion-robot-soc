#include <math.h>
#include <string.h>

#include "robot_calibration/forearm_motion_control.h"
#include "robot_calibration/forearm_calibration_config.h"

#define FOREARM_PITCH_RANGE_EPSILON 0.01f

static float map_joint_angle(float human_deg, const JointCalibration *config)
{
    return human_deg
         * config->scale
         * (float)config->direction
         + config->zero_offset_deg;
}

static float clamp_value(float value, float min_value, float max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static float wrap_to_180(float deg)
{
    deg = fmodf(deg, 360.0f);
    if (deg > 180.0f) deg -= 360.0f;
    if (deg < -180.0f) deg += 360.0f;
    return deg;
}

static float unwrap_angle(float raw_deg, float last_unwrapped_deg, int has_reference)
{
    float last_raw_deg;
    float diff;

    if (!has_reference) return raw_deg;

    last_raw_deg = wrap_to_180(last_unwrapped_deg);
    diff = wrap_to_180(raw_deg - last_raw_deg);

    return last_unwrapped_deg + diff;
}

static float clamp_joint_angle(float angle_deg, const JointCalibration *config)
{
    return clamp_value(angle_deg, config->min_deg, config->max_deg);
}

int forearm_motion_control_validate_target(const HumanForearmTarget *target)
{
    if (target == NULL) return 0;
    if (target->valid == 0U) return 0;
    if (!isfinite(target->elbow_roll_deg)) return 0;
    if (!isfinite(target->elbow_pitch_deg)) return 0;
    if (!isfinite(target->wrist_pitch_deg)) return 0;
    if (!isfinite(target->wrist_roll_deg)) return 0;
    if (!isfinite(target->gripper_norm)) return 0;
    /* elbow_pitch_deg는 A1 계약상 [-90,90], wrap 없음. */
    if (target->elbow_pitch_deg < -90.0f - FOREARM_PITCH_RANGE_EPSILON ||
        target->elbow_pitch_deg > 90.0f + FOREARM_PITCH_RANGE_EPSILON) return 0;
    if (target->gripper_norm < 0.0f || target->gripper_norm > 1.0f) return 0;
    return 1;
}

void forearm_motion_control_map_target(const HumanForearmTarget *input, ForearmJointCommand *output)
{
    if (input == NULL || output == NULL) return;

    output->elbow_roll_deg = map_joint_angle(input->elbow_roll_deg, &forearm_calibration_config.elbow_roll);
    output->elbow_pitch_deg = map_joint_angle(input->elbow_pitch_deg, &forearm_calibration_config.elbow_pitch);
    output->wrist_pitch_deg = map_joint_angle(input->wrist_pitch_deg, &forearm_calibration_config.wrist_pitch);
    output->wrist_roll_deg = map_joint_angle(input->wrist_roll_deg, &forearm_calibration_config.wrist_roll);
    output->gripper_norm = input->gripper_norm;
    output->valid = input->valid;
}

void forearm_motion_control_apply_limits(ForearmJointCommand *command)
{
    if (command == NULL) return;
    command->elbow_roll_deg = clamp_joint_angle(command->elbow_roll_deg, &forearm_calibration_config.elbow_roll);
    command->elbow_pitch_deg = clamp_joint_angle(command->elbow_pitch_deg, &forearm_calibration_config.elbow_pitch);
    command->wrist_pitch_deg = clamp_joint_angle(command->wrist_pitch_deg, &forearm_calibration_config.wrist_pitch);
    command->wrist_roll_deg = clamp_joint_angle(command->wrist_roll_deg, &forearm_calibration_config.wrist_roll);
}

void forearm_motion_control_unwrap_state_init(ForearmAngleUnwrapState *state)
{
    if (state == NULL) return;
    memset(state, 0, sizeof(*state));
}

void forearm_motion_control_unwrap_target(ForearmAngleUnwrapState *state, HumanForearmTarget *target)
{
    if (state == NULL || target == NULL) return;

    target->elbow_roll_deg = unwrap_angle(target->elbow_roll_deg, state->yaw_deg, state->has_reference);
    target->wrist_pitch_deg = unwrap_angle(target->wrist_pitch_deg, state->wrist_pitch_deg, state->has_reference);
    target->wrist_roll_deg = unwrap_angle(target->wrist_roll_deg, state->wrist_roll_deg, state->has_reference);

    state->yaw_deg = target->elbow_roll_deg;
    state->wrist_pitch_deg = target->wrist_pitch_deg;
    state->wrist_roll_deg = target->wrist_roll_deg;
    state->has_reference = 1;
}
