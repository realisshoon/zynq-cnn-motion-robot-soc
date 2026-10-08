#ifndef AGENT_WRIST_RETURN_H
#define AGENT_WRIST_RETURN_H

static float *wrist_return_angle(ForearmJointCommand *command, unsigned axis)
{
    return axis == 0U ? &command->wrist_pitch_deg : &command->wrist_roll_deg;
}

static float wrist_return_advance(float reference, float goal, float limit)
{
    if (goal > reference + limit) return reference + limit;
    if (goal < reference - limit) return reference - limit;
    return goal;
}

static float wrist_return_reference(AgentPipelineContext *ctx, unsigned axis)
{
    if (ctx->wrist_return[axis].valid) return ctx->wrist_return[axis].goal;
    return *wrist_return_angle(&ctx->command, axis);
}

static void wrist_return_prepare(AgentPipelineContext *ctx, ForearmJointCommand *command,
                                AgentWristReturn *next_states)
{
    unsigned axis;
    uint32_t now_us = gripper_latch_time_us(ctx);
    int held = ctx->gripper_independent && ctx->output_enabled && ctx->arm_stationary &&
        ctx->arm_tracking_started && ctx->command_valid;
    for (axis = 0U; axis < 2U; ++axis) {
        AgentWristReturn *state = &next_states[axis];
        int reachable = axis == 0U ? forearm_motion_control_wrist_pitch_reachable(ctx->target.wrist_pitch_deg)
                                  : forearm_motion_control_wrist_roll_reachable(ctx->target.wrist_roll_deg);
        float reference = *wrist_return_angle(&ctx->command, axis);
        float proposed = *wrist_return_angle(command, axis);
        uint32_t elapsed = now_us - state->time_us;
        int released = state->valid && state->held && !ctx->arm_stationary;
        int reentered = state->valid && state->outside && reachable && ctx->target.wrist_valid;
        int triggered = !held && (released || reentered) && fabsf(proposed - reference) > 8.0f;
        if (state->candidate_epoch != ctx->arm_input_epoch || elapsed > 250000U)
            state->have_candidate = 0U;
        if (triggered && !state->pending) {
            state->pending = 1U;
            state->have_candidate = 0U;
            state->active = 0U;
            state->need_two = 0U;
        }
        if (state->pending && (held || !reachable ||
            (ctx->wrist_observation_fresh && ctx->target.wrist_valid &&
             fabsf(proposed - reference) <= 8.0f))) {
            state->pending = state->have_candidate = state->active = state->need_two = 0U;
            state->goal = proposed;
        }
        if (state->pending) {
            int valid = ctx->wrist_observation_fresh && reachable && ctx->target.wrist_valid && !held &&
                (!state->have_fresh || ctx->pose.frame_id != state->last_fresh_frame);
            if (valid) {
                uint32_t span = now_us - state->candidate_us;
                if ((!state->have_fresh || now_us - state->last_fresh_us > 250000U) &&
                    fabsf(proposed - reference) > 60.0f) state->need_two = 1U;
                if (!state->need_two) {
                    state->pending = state->have_candidate = 0U;
                    state->active = 1U;
                    state->goal = proposed;
                } else if (!state->have_candidate || span > 250000U ||
                    fabsf(proposed - state->candidate) > 15.0f) {
                    state->have_candidate = 1U;
                    state->candidate = proposed;
                    state->candidate_us = now_us;
                    state->candidate_frame = ctx->pose.frame_id;
                } else if (ctx->pose.frame_id != state->candidate_frame && span >= 80000U) {
                    state->pending = state->have_candidate = 0U;
                    state->active = 1U;
                    state->goal = proposed;
                }
            }
            if (state->pending) *wrist_return_angle(command, axis) = reference;
        } else if (!state->active || (ctx->wrist_observation_fresh && reachable && !held)) {
            state->goal = proposed;
        }
        if (state->active && !state->pending) {
            float result;
            if (elapsed > 100000U) elapsed = 100000U;
            result = wrist_return_advance(reference, state->goal, 45.0f * (float)elapsed / 1000000.0f);
            *wrist_return_angle(command, axis) = result;
            if (fabsf(result - state->goal) < 0.0001f) state->active = 0U;
        }
        state->time_us = now_us;
        state->held = ctx->arm_stationary ? 1U : 0U;
        if (ctx->target.wrist_valid) state->outside = reachable ? 0U : 1U;
        if (!state->valid) state->goal = proposed;
        if (ctx->wrist_observation_fresh && ctx->target.wrist_valid &&
            (!state->have_fresh || ctx->pose.frame_id != state->last_fresh_frame)) {
            state->last_fresh_us = now_us;
            state->last_fresh_frame = ctx->pose.frame_id;
            state->have_fresh = 1U;
        }
        state->candidate_epoch = ctx->arm_input_epoch;
        state->valid = 1U;
    }
}

static void wrist_return_tick(AgentPipelineContext *ctx)
{
    ForearmJointCommand destination, command;
    unsigned axis;
    if (!ctx->output_enabled || ctx->output_parked || !ctx->command_valid) return;
    destination = command = ctx->command;
    for (axis = 0U; axis < 2U; ++axis)
        if (ctx->wrist_return[axis].valid && ctx->wrist_return[axis].active)
            *wrist_return_angle(&destination, axis) = ctx->wrist_return[axis].goal;
    if (!forearm_safety_check_apply(&destination, NULL)) return;
    for (axis = 0U; axis < 2U; ++axis)
        if (ctx->wrist_return[axis].valid && ctx->wrist_return[axis].active)
            *wrist_return_angle(&command, axis) = wrist_return_advance(
                *wrist_return_angle(&command, axis), ctx->wrist_return[axis].goal, 0.90f);
    if (!forearm_safety_check_apply(&command, NULL)) return;
    for (axis = 0U; axis < 2U; ++axis) {
        AgentWristReturn *state = &ctx->wrist_return[axis];
        if (!state->active) continue;
        state->time_us = gripper_latch_time_us(ctx);
        if (fabsf(*wrist_return_angle(&command, axis) - state->goal) < 0.0001f)
            state->active = 0U;
    }
    if (!same_command(&command, &ctx->command) || ctx->motion.held) {
        forearm_calibration_set_target(&ctx->motion, &command);
        ctx->command = command;
        ctx->retargets++;
    }
}

#endif
