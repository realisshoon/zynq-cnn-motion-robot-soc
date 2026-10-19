#include "pipeline_internal.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "app/robot/vision/human_target/forearm_target.h"

float gripper_latch_update(GripperLatchState *latch, float desired,
                                  float held, int fresh, uint32_t frame_id,
                                  uint8_t source, uint32_t now_us)
{
    uint32_t advance = frame_id - latch->frame_id;
    if (latch->have_frame && !advance) return held;
    if (latch->have_frame && advance >= 0x80000000U)
        memset(latch, 0, sizeof(*latch));
    if (latch->have_frame && (source != latch->source ||
        now_us - latch->sample_time_us > GRIPPER_CONTROL_SAMPLE_MAX_GAP_US)) {
        latch->close_count = 0U;
        latch->open_count = 0U;
    }
    latch->frame_id = frame_id;
    latch->have_frame = 1U;
    latch->sample_time_us = now_us;
    latch->source = source;
    if (!fresh || !isfinite(desired)) {
        latch->close_count = 0U;
        latch->open_count = 0U;
        return latch->latched ? 0.0f : held;
    }
    if (latch->latched) {
        int hold_done = latch->closed_applied
            ? now_us - latch->applied_time_us >= GRIPPER_CONTROL_CLOSE_HOLD_US
            : now_us - latch->confirmed_time_us >= GRIPPER_CONTROL_CLOSE_MAX_WAIT_US;
        if (hold_done && desired > 0.0f) {
            ++latch->open_count;
            if (latch->open_count >= GRIPPER_CONTROL_CONFIRM_SAMPLES) {
                latch->latched = 0U;
                latch->closed_applied = 0U;
                latch->open_count = 0U;
                return desired;
            }
        } else latch->open_count = 0U;
        return 0.0f;
    }
    if (desired == 0.0f) {
        ++latch->close_count;
        if (latch->close_count >= GRIPPER_CONTROL_CONFIRM_SAMPLES) {
            latch->close_count = 0U;
            latch->open_count = 0U;
            latch->latched = 1U;
            latch->closed_applied = 0U;
            latch->confirmed_time_us = now_us;
            return 0.0f;
        }
        return held;
    }
    latch->close_count = 0U;
    return desired;
}

static float gripper_hysteresis_update(GripperLatchState *latch, float desired,
    float distance_px, float held, int fresh, uint32_t frame_id,
    uint8_t source, uint32_t now_us)
{
    uint32_t advance = frame_id - latch->frame_id;
    if (latch->have_frame && !advance) return held;
    if (latch->have_frame && advance >= 0x80000000U)
        memset(latch, 0, sizeof(*latch));
    if (latch->latched)
        return gripper_latch_update(latch, desired, held, fresh, frame_id, source, now_us);
    if (!fresh || !isfinite(desired) || !isfinite(distance_px) || distance_px < 0.0f ||
        (latch->have_frame && (source != latch->source ||
         now_us - latch->sample_time_us > GRIPPER_CONTROL_SAMPLE_MAX_GAP_US)) ||
        (latch->candidate_active &&
         now_us - latch->candidate_started_us > GRIPPER_CONTROL_CANDIDATE_WINDOW_US)) {
        latch->candidate_active = 0U;
        latch->close_count = 0U;
        latch->cancel_count = 0U;
    }
    latch->frame_id = frame_id;
    latch->have_frame = 1U;
    latch->sample_time_us = now_us;
    latch->source = source;
    if (!fresh || !isfinite(desired) || !isfinite(distance_px) || distance_px < 0.0f)
        return held;
    if (distance_px <= GRIPPER_CONTROL_CLOSE_START_PX) {
        if (!latch->candidate_active) {
            latch->candidate_active = 1U;
            latch->candidate_started_us = now_us;
        }
        latch->cancel_count = 0U;
        ++latch->close_count;
        if (latch->close_count >= GRIPPER_CONTROL_CONFIRM_SAMPLES) {
            latch->candidate_active = 0U;
            latch->close_count = 0U;
            latch->open_count = 0U;
            latch->latched = 1U;
            latch->closed_applied = 0U;
            latch->confirmed_time_us = now_us;
            return 0.0f;
        }
        return held;
    }
    if (latch->candidate_active) {
        if (distance_px > GRIPPER_CONTROL_CANCEL_PX) {
            ++latch->cancel_count;
            if (latch->cancel_count >= GRIPPER_CONTROL_CANCEL_SAMPLES) {
                latch->candidate_active = 0U;
                latch->close_count = 0U;
                latch->cancel_count = 0U;
                return desired;
            }
        } else latch->cancel_count = 0U;
        return held;
    }
    return desired;
}

void gripper_latch_applied(GripperLatchState *latch, float command, uint32_t now_us)
{
    if (latch->latched && !latch->closed_applied && command == 0.0f) {
        latch->closed_applied = 1U;
        latch->applied_time_us = now_us;
    }
}

void gripper_control_reset(RobotPipeline *ctx)
{
    if (ctx != NULL) memset(&ctx->gripper_latch, 0, sizeof(ctx->gripper_latch));
    if (ctx != NULL) {
        uint8_t manual_open = ctx->gripper_motion_hold.manual_open;
        memset(&ctx->gripper_motion_hold, 0, sizeof(ctx->gripper_motion_hold));
        ctx->gripper_motion_hold.manual_open = manual_open;
    }
    forearm_target_reset_gripper();
    if (ctx != NULL) {
        memset(&ctx->elbow_reentry, 0, sizeof(ctx->elbow_reentry));
        memset(&ctx->elbow_return, 0, sizeof(ctx->elbow_return));
        memset(ctx->wrist_return, 0, sizeof(ctx->wrist_return));
        ctx->wrist_observation_fresh = 0U;
    }
}

void gripper_motion_sample(GripperMotionHoldState *state,
                                  const ForearmJointCommand *command, uint32_t now_us)
{
    float angles[4] = {command->elbow_roll_deg, command->elbow_pitch_deg,
                       command->wrist_pitch_deg, command->wrist_roll_deg};
    unsigned sample, axis;
    uint32_t span;
    if (state->samples && now_us == state->times[state->samples - 1U]) return;
    if (state->samples && now_us - state->times[state->samples - 1U] > 100000U) {
        state->samples = 0U;
        state->speed_valid = state->low_valid = state->open_count = 0U;
    }
    while (state->samples && (state->samples == 7U || now_us - state->times[0] > 120000U)) {
        state->samples--;
        memmove(state->angles, state->angles + 1, state->samples * sizeof(state->angles[0]));
        memmove(state->times, state->times + 1, state->samples * sizeof(state->times[0]));
    }
    memcpy(state->angles[state->samples], angles, sizeof(angles));
    state->times[state->samples++] = now_us;
    span = now_us - state->times[0];
    state->speed_valid = span >= 60000U;
    if (!state->speed_valid) return;
    state->speed = 0.0f;
    for (axis = 0U; axis < 4U; axis++) {
        float distance = 0.0f;
        for (sample = 1U; sample < state->samples; sample++)
            distance += fabsf(state->angles[sample][axis] - state->angles[sample - 1U][axis]);
        distance *= 1000000.0f / (float)span;
        if (distance > state->speed) state->speed = distance;
    }
}

static int gripper_strong_open_confirm(GripperOpenConfirmState *state, float command,
                                      int latched, int fresh, uint32_t frame_id,
                                      uint8_t source, uint32_t now_us)
{
    int valid;
    if (latched) {
        memset(state, 0, sizeof(*state));
        return 0;
    }
    if (!fresh) {
        if (!state->have_frame || now_us - state->fresh_us > GRIPPER_CONTROL_OPEN_MISSING_GRACE_US)
            state->pending = state->count = 0U;
        return 0;
    }
    valid = isfinite(command) && command >= GRIPPER_CONTROL_STRONG_OPEN_NORM;
    if (!valid || (state->have_frame && (frame_id == state->frame_id || source != state->source ||
        now_us - state->fresh_us > GRIPPER_CONTROL_SAMPLE_MAX_GAP_US)))
        state->pending = state->count = 0U;
    state->have_frame = 1U;
    state->source = source;
    state->frame_id = frame_id;
    state->fresh_us = now_us;
    if (!valid) return 0;
    if (!state->pending) {
        state->pending = 1U;
        state->started_us = now_us;
    }
    if (state->count < 255U) state->count++;
    return state->count >= GRIPPER_CONTROL_CONFIRM_SAMPLES &&
        now_us - state->started_us >= GRIPPER_CONTROL_STRONG_OPEN_US;
}

static float gripper_motion_select(GripperMotionHoldState *state, float command,
                                  int latched, int fresh, uint32_t frame_id,
                                  uint8_t source, uint32_t now_us)
{
    int known = state->speed_valid && state->samples &&
        now_us - state->times[state->samples - 1U] <= 100000U;
    int new_frame = fresh && (!state->have_frame || frame_id != state->frame_id || source != state->source);
    int slow;
    int strong_open;
    if (state->manual_open) return 1.0f;
    if (state->have_frame && frame_id == state->frame_id && source == state->source)
        memset(&state->strong_open, 0, sizeof(state->strong_open));
    strong_open = gripper_strong_open_confirm(&state->strong_open, command, latched,
        fresh && new_frame, frame_id, source, now_us);
    if (!known) {
        state->low_valid = state->open_count = 0U;
    } else if (state->speed >= 8.0f) {
        state->moving = 1U;
        state->low_valid = state->open_count = 0U;
    } else if (state->speed <= 3.0f) {
        if (!state->low_valid) {
            state->low_valid = 1U;
            state->low_since_us = now_us;
        }
        if (now_us - state->low_since_us >= 200000U) state->moving = 0U;
    } else {
        state->low_valid = state->open_count = 0U;
    }
    if (new_frame) {
        if (state->have_frame && (source != state->source || now_us - state->fresh_us > 250000U))
            state->open_count = 0U;
        state->frame_id = frame_id;
        state->source = source;
        state->fresh_us = now_us;
        state->have_frame = 1U;
    } else state->open_count = 0U;
    if (new_frame && latched && command == 0.0f) state->armed = 1U;
    if (state->armed && (state->moving || !known)) state->locked = 1U;
    if (!state->locked) {
        memset(&state->strong_open, 0, sizeof(state->strong_open));
        if (new_frame && !latched && command >= 0.05f && known && !state->moving)
            state->armed = 0U;
        return command;
    }
    if (known && new_frame && strong_open) {
        state->locked = state->armed = state->open_count = 0U;
        memset(&state->strong_open, 0, sizeof(state->strong_open));
        return command;
    }
    slow = known && state->speed <= 3.0f && !state->moving && state->low_valid &&
        now_us - state->low_since_us >= 200000U;
    if (slow && new_frame && command >= 0.05f && !latched) {
        if (++state->open_count >= 3U) {
            state->locked = state->armed = state->open_count = 0U;
            memset(&state->strong_open, 0, sizeof(state->strong_open));
            return command;
        }
    } else state->open_count = 0U;
    return 0.0f;
}

int gripper_control_set_manual(RobotPipeline *ctx, int open)
{
    ForearmJointCommand command, current;
    if (ctx == NULL || !ctx->output_enabled || !ctx->gripper_independent) return 0;
    command = ctx->command_valid ? ctx->command : ctx->output;
    current = ctx->output;
    if (open) {
        command.gripper_norm = current.gripper_norm = 1.0f;
        if (!command.valid || !current.valid || !forearm_safety_check_apply(&command, NULL) ||
            !forearm_safety_check_apply(&current, NULL)) return 0;
    }
    memset(&ctx->gripper_motion_hold, 0, sizeof(ctx->gripper_motion_hold));
    memset(&ctx->gripper_latch, 0, sizeof(ctx->gripper_latch));
    forearm_target_reset_gripper();
    ctx->gripper_motion_hold.manual_open = open ? 1U : 0U;
    if (open) {
        ctx->command = command;
        ctx->command_valid = 1U;
        ctx->motion.gripper = 1.0f;
    }
    return 1;
}

int gripper_control_update(RobotPipeline *ctx, const HumanPose2D *pose)
{
    GripperLatchState next;
    GripperMotionHoldState next_hold;
    ForearmJointCommand command, current;
    float desired = NAN;
    int fresh;
    if (ctx == NULL || pose == NULL || !ctx->gripper_independent || !robot_motion_calculation_enabled(ctx)) return 0;
    fresh = forearm_target_update_gripper(pose, &desired);
    ctx->gripper_fresh = fresh ? 1U : 0U;
    ctx->gripper_desired = desired;
    ctx->gripper_distance_px = fresh ? forearm_target_gripper_distance_px() : NAN;
    command = ctx->command_valid ? ctx->command : ctx->output;
    next = ctx->gripper_latch;
    next_hold = ctx->gripper_motion_hold;
    command.gripper_norm = gripper_hysteresis_update(&next, desired, ctx->gripper_distance_px, command.gripper_norm,
        fresh, pose->frame_id, pose->gripper_2d.source, robot_pipeline_time_us(ctx));
    command.gripper_norm = gripper_motion_select(&next_hold, command.gripper_norm,
        next.latched, fresh, pose->frame_id, pose->gripper_2d.source, robot_pipeline_time_us(ctx));
    current = ctx->output;
    current.gripper_norm = command.gripper_norm;
    if (!command.valid || !current.valid || !forearm_safety_check_apply(&command, NULL) ||
        !forearm_safety_check_apply(&current, NULL)) return -1;
    ctx->gripper_latch = next;
    ctx->gripper_motion_hold = next_hold;
    ctx->motion.gripper = command.gripper_norm;
    ctx->command = command;
    ctx->command_valid = 1U;
    return fresh ? 1 : 0;
}
