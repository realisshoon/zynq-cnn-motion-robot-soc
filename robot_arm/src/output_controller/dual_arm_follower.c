#include "output_controller/dual_arm_follower.h"
#include "dual_arm_config.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

static const ForearmRobotGeometry left_geometry = {
    ROBOT_LEFT_FOREARM_CM, ROBOT_LEFT_WRIST_ROLL_END_CM,
    ROBOT_LEFT_ROLL_END_TIP_CM, ROBOT_LEFT_TABLE_Z_CM
};

static int same_axes(const ForearmJointCommand *first, const ForearmJointCommand *second)
{
    return fabsf(first->elbow_roll_deg - second->elbow_roll_deg) < 0.0001f &&
           fabsf(first->elbow_pitch_deg - second->elbow_pitch_deg) < 0.0001f &&
           fabsf(first->wrist_pitch_deg - second->wrist_pitch_deg) < 0.0001f &&
           fabsf(first->wrist_roll_deg - second->wrist_roll_deg) < 0.0001f;
}

int dual_arm_left_is_safe(const ForearmJointCommand *command, uint32_t *issues)
{
    ForearmJointCommand clamped;
    if (command == NULL || !command->valid || !isfinite(command->gripper_norm) ||
        command->gripper_norm < 0.0f || command->gripper_norm > 1.0f) {
        if (issues != NULL) *issues = FOREARM_SAFETY_CHECK_INVALID_COMMAND;
        return 0;
    }
    clamped = *command;
    forearm_motion_control_apply_limits(&clamped);
    if (!same_axes(&clamped, command)) {
        if (issues != NULL) *issues = FOREARM_SAFETY_CHECK_INVALID_COMMAND;
        return 0;
    }
    return forearm_safety_check_geometry(command, &left_geometry, issues);
}

void dual_arm_follower_init(DualArmFollower *state)
{
    if (state != NULL) memset(state, 0, sizeof(*state));
}

void dual_arm_follower_pause(DualArmFollower *state)
{
    if (state == NULL) return;
    state->enabled = 0U;
    state->need_rejoin = state->have_command;
    state->mode = DUAL_ARM_OFF;
    forearm_calibration_state_init(&state->motion);
}

static void hold_axes(DualArmFollower *state, const ForearmJointCommand *right,
                      uint32_t flags, ForearmJointCommand *left)
{
    *left = state->applied;
    left->gripper_norm = right->gripper_norm;
    state->need_rejoin = 1U;
    state->safety_flags = flags;
    state->mode = DUAL_ARM_HOLDING;
    state->hold_ticks++;
    forearm_calibration_state_init(&state->motion);
}

int dual_arm_follower_prepare(DualArmFollower *state,
    const ForearmJointCommand *right, int advance, ForearmJointCommand *left)
{
    uint32_t flags;
    if (state == NULL || right == NULL || left == NULL ||
        !right->valid || !isfinite(right->gripper_norm) ||
        right->gripper_norm < 0.0f || right->gripper_norm > 1.0f ||
        !forearm_safety_check_apply(right, NULL)) return 0;
    if (!state->have_command) {
        if (!dual_arm_left_is_safe(right, &flags)) return 0;
        *left = *right;
        state->applied = *left;
        state->have_command = 1U;
        state->safety_flags = 0U;
        return 1;
    }
    if (!dual_arm_left_is_safe(&state->applied, &flags)) return 0;
    if (!advance) {
        *left = state->applied;
        state->need_rejoin = !same_axes(left, right);
        return 1;
    }
    if (!dual_arm_left_is_safe(right, &flags)) {
        hold_axes(state, right, flags, left);
    } else if (state->need_rejoin) {
        if (!state->motion.has_target) {
            forearm_calibration_set_target(&state->motion, &state->applied);
            forearm_calibration_step(&state->motion, left);
        }
        forearm_calibration_set_target(&state->motion, right);
        forearm_calibration_step(&state->motion, left);
        if (state->motion.held || !dual_arm_left_is_safe(left, &flags)) {
            if (state->motion.held) flags = state->motion.blocked_flags;
            hold_axes(state, right, flags, left);
        } else {
            state->rejoin_ticks++;
            state->safety_flags = 0U;
            state->need_rejoin = !same_axes(left, right);
            state->mode = state->need_rejoin ? DUAL_ARM_REJOINING : DUAL_ARM_TRACKING;
        }
    } else {
        *left = *right;
        state->tracking_ticks++;
        state->safety_flags = 0U;
        state->mode = DUAL_ARM_TRACKING;
    }
    if (!dual_arm_left_is_safe(left, NULL)) return 0;
    state->applied = *left;
    return 1;
}

const char *dual_arm_follower_mode_name(DualArmFollowerMode mode)
{
    switch (mode) {
        case DUAL_ARM_OFF: return "OFF";
        case DUAL_ARM_TRACKING: return "TRACK";
        case DUAL_ARM_HOLDING: return "LEFT_SAFETY_HOLD";
        case DUAL_ARM_REJOINING: return "LEFT_BOUNDED_REJOIN";
        case DUAL_ARM_HAL_FAULT: return "HAL_FAULT";
        default: return "UNKNOWN";
    }
}
