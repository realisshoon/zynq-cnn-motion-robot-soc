#include <stddef.h>
#include <string.h>

#include "robot_calibration/forearm_calibration.h"
#include "robot_calibration/forearm_calibration_config.h"
#include "robot_calibration/forearm_safety_check.h"
#include "robot_calibration/motion_limits.h"
#include "robot_calibration/motion_smoothing.h"

int forearm_calibration_apply(const HumanForearmTarget *input, ForearmJointCommand *output)
{
    if (output == NULL) return 0;
    output->valid = 0;
    if (!forearm_motion_control_validate_target(input)) return 0;

    forearm_motion_control_map_target(input, output);
    forearm_motion_control_apply_limits(output);
    output->valid = 1;

    if (!forearm_safety_check_apply(output, NULL)) {
        output->valid = 0;
        return 0;
    }

    return 1;
}

/* 배열 순서: elbow_roll, elbow_pitch, wrist_pitch, wrist_roll (gripper 제외). */
static void joint_command_to_array(const ForearmJointCommand *command, float array[FOREARM_MOTION_JOINT_COUNT])
{
    array[0] = command->elbow_roll_deg;
    array[1] = command->elbow_pitch_deg;
    array[2] = command->wrist_pitch_deg;
    array[3] = command->wrist_roll_deg;
}

static void array_to_joint_command(const float array[FOREARM_MOTION_JOINT_COUNT], ForearmJointCommand *command)
{
    command->elbow_roll_deg = array[0];
    command->elbow_pitch_deg = array[1];
    command->wrist_pitch_deg = array[2];
    command->wrist_roll_deg = array[3];
}

static void fill_max_delta_per_tick(float array[FOREARM_MOTION_JOINT_COUNT])
{
    array[0] = forearm_calibration_config.elbow_roll.max_delta_deg;
    array[1] = forearm_calibration_config.elbow_pitch.max_delta_deg;
    array[2] = forearm_calibration_config.wrist_pitch.max_delta_deg;
    array[3] = forearm_calibration_config.wrist_roll.max_delta_deg;
}

void forearm_calibration_state_init(ForearmMotionState *state)
{
    if (state == NULL) return;
    memset(state, 0, sizeof(*state));
}

void forearm_calibration_set_target(ForearmMotionState *state, const ForearmJointCommand *target)
{
    float target_array[FOREARM_MOTION_JOINT_COUNT];
    float max_delta[FOREARM_MOTION_JOINT_COUNT];
    int total_ticks = 0;
    int in_motion;
    int i;

    if (state == NULL || target == NULL) return;

    in_motion = state->has_target && state->ticks_elapsed < state->stretched_ticks;

    joint_command_to_array(target, target_array);
    fill_max_delta_per_tick(max_delta);
    state->gripper = target->gripper_norm;

    if (!state->has_target) {
        memcpy(state->current, target_array, sizeof(state->current));
        state->has_target = 1;
    }

    for (i = 0; i < FOREARM_MOTION_JOINT_COUNT; i++) {
        int joint_ticks = motion_limits_ticks_to_target(
            state->current[i], target_array[i], max_delta[i]);

        if (joint_ticks < 0) {
            return;
        }
        if (joint_ticks > total_ticks) {
            total_ticks = joint_ticks;
        }
    }

    memcpy(state->start, state->current, sizeof(state->start));
    memcpy(state->target, target_array, sizeof(state->target));
    memcpy(state->max_delta_per_tick, max_delta, sizeof(state->max_delta_per_tick));
    state->linear_retarget = in_motion;
    state->stretched_ticks = in_motion ? total_ticks : motion_smoothing_stretch_ticks(total_ticks);
    state->ticks_elapsed = 0;
    state->blocked_flags = FOREARM_SAFETY_CHECK_OK;
}

void forearm_calibration_step(ForearmMotionState *state, ForearmJointCommand *output)
{
    float progress;
    float eased;
    float next[FOREARM_MOTION_JOINT_COUNT];
    ForearmJointCommand candidate;
    ForearmSafetyCheckFlags issues;
    int next_tick;
    int i;

    if (state == NULL || output == NULL) return;

    if (!state->has_target || state->stretched_ticks <= 0) {
        array_to_joint_command(state->current, output);
        output->gripper_norm = state->gripper;
        output->valid = state->has_target ? 1 : 0;
        return;
    }

    next_tick = state->ticks_elapsed < state->stretched_ticks
        ? state->ticks_elapsed + 1 : state->ticks_elapsed;

    progress = (float)next_tick / (float)state->stretched_ticks;
    eased = state->linear_retarget ? progress : motion_smoothing_ease(progress);

    for (i = 0; i < FOREARM_MOTION_JOINT_COUNT; i++) {
        float ideal = state->start[i] + (state->target[i] - state->start[i]) * eased;
        next[i] = motion_limits_step_toward(state->current[i], ideal, state->max_delta_per_tick[i]);
    }

    array_to_joint_command(next, &candidate);
    candidate.gripper_norm = state->gripper;
    candidate.valid = 1;
    if (forearm_safety_check_apply(&candidate, &issues)) {
        memcpy(state->current, next, sizeof(state->current));
        state->ticks_elapsed = next_tick;
        state->blocked_flags = FOREARM_SAFETY_CHECK_OK;
    } else {
        /* Keep both position and trajectory time at the last accepted sample.
         * Do not skip through a collision on later ticks. A new goal may recover. */
        state->blocked_flags = issues;
    }

    array_to_joint_command(state->current, output);
    output->gripper_norm = state->gripper;
    output->valid = 1;
}
