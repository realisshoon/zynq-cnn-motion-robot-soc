#include "pipeline_internal.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "app/robot/vision/human_target/forearm_target.h"
#include "app/robot/control/servo_control.h"
#include "drivers/pwm/servo_hal.h"
#include "app/uart/telemetry.h"

const ForearmJointCommand robot_home_pose = {
    .elbow_roll_deg = 90.0f,
    .elbow_pitch_deg = 70.0f,
    .wrist_pitch_deg = 100.0f,
    .wrist_roll_deg = 90.0f,
    .gripper_norm = 0.05f,
    .valid = 1U
};

int robot_pipeline_same_command(const ForearmJointCommand *a, const ForearmJointCommand *b)
{
    return a->elbow_roll_deg == b->elbow_roll_deg &&
           a->elbow_pitch_deg == b->elbow_pitch_deg &&
           a->wrist_pitch_deg == b->wrist_pitch_deg &&
           a->wrist_roll_deg == b->wrist_roll_deg &&
           a->gripper_norm == b->gripper_norm &&
           a->valid == b->valid;
}

static int home_within_limits(void)
{
    ForearmJointCommand clamped = robot_home_pose;

    forearm_motion_control_apply_limits(&clamped);
    return robot_pipeline_same_command(&clamped, &robot_home_pose);
}

static int home_is_safe(void)
{
    return forearm_safety_check_apply(&robot_home_pose, NULL);
}

uint32_t robot_pipeline_time_us(const RobotPipeline *ctx)
{
    (void)ctx;
    return platform_telemetry_time_us();
}

int robot_motion_init(RobotPipeline *ctx)
{
    ServoPwmCommand pwm;

    if (ctx == NULL) return -1;

    if (!servo_hal_set_output_permitted(0)) {
        ctx->output_faulted = 1U;
        return -1;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->output_enabled = 0U;
    ctx->output_parked = 1U;

    if (forearm_target_init() != 0) return -1;
    forearm_motion_control_unwrap_state_init(&ctx->unwrap);
    forearm_calibration_state_init(&ctx->motion);
    if (!home_within_limits() || !home_is_safe()) return -1;

    /*
     * 홈으로 부트스트랩한다. Agent2는 첫 set_target을 램프 없이 스냅하므로,
     * 홈을 먼저 넣어 두면 첫 실제 타겟이 홈에서부터 속도제한 램프로 출발한다.
     * (홈은 상수라 apply()를 거치지 않고 set_target에 직접 넣는다.)
     */
    forearm_calibration_set_target(&ctx->motion, &robot_home_pose);
    forearm_calibration_step(&ctx->motion, &ctx->output);

    /* Bootstrap software telemetry only. No shadow write/UPDATE/ENABLE at boot. */
    if (!servo_control_convert(&ctx->output, &pwm)) return -1;
    ctx->pwm = pwm;
    ctx->servo_command = ctx->output;
    ctx->servo_command_valid = 1U;
    ctx->servo_command_tick = ctx->ticks;
    return 0;
}

int robot_motion_calculation_enabled(const RobotPipeline *ctx)
{
    return ctx != NULL && !ctx->output_faulted &&
        (ctx->output_enabled || ctx->virtual_compute || !ctx->output_parked);
}

static void virtual_end(RobotPipeline *ctx)
{
    ctx->virtual_compute = 0U;
    ctx->virtual_session = 0U;
    ctx->target_ready = 0U;
    ctx->output_parked = 1U;
    /* HOLD the virtual pose; never promote it into applied_command. */
    forearm_calibration_state_init(&ctx->motion);
    forearm_calibration_set_target(&ctx->motion, &ctx->output);
    ctx->command = ctx->output;
    ctx->command_valid = ctx->output.valid;
    gripper_control_reset(ctx);
}

int robot_motion_virtual_service(RobotPipeline *ctx, uint32_t now_us)
{
    if (ctx != NULL && ctx->virtual_compute &&
        (ctx->output_enabled || servo_hal_output_permitted() || ctx->output_faulted ||
         (uint32_t)(now_us - ctx->virtual_renewed_us) >= ROBOT_VIRTUAL_LEASE_US)) {
        if (ctx->output_enabled || servo_hal_output_permitted()) {
            /* Broken compute/output invariant: revoke software authorization.
             * A failed disable is still physical UNKNOWN, never a safety proof. */
            ctx->output_faulted = 1U;
            if (servo_hal_set_output_permitted(0)) ctx->output_enabled = 0U;
        }
        virtual_end(ctx);
        return 1;
    }
    return 0;
}

int robot_motion_virtual_request(RobotPipeline *ctx, unsigned op,
                                   uint32_t session, uint32_t now_us)
{
    if (ctx == NULL || session == 0U || op > 2U) return 0;
    (void)robot_motion_virtual_service(ctx, now_us);
    if (op == 0U) {
        if (!ctx->virtual_compute || session != ctx->virtual_session) return 0;
        virtual_end(ctx);
        return 1;
    }
    if (ctx->output_enabled || servo_hal_output_permitted() || ctx->output_faulted) return 0;
    if (op == 2U) {
        if (!ctx->virtual_compute || session != ctx->virtual_session) return 0;
        ctx->virtual_renewed_us = now_us;
        return 1;
    }
    /* Strictly newer serial nonce; stale start/renew cannot resume a ended session. */
    if (ctx->virtual_last_session &&
        (uint32_t)(session - ctx->virtual_last_session) >= 0x80000000U) return 0;
    if (session == ctx->virtual_last_session) return 0;
    virtual_end(ctx);
    forearm_motion_control_unwrap_state_init(&ctx->unwrap);
    memset(&ctx->elbow_reentry, 0, sizeof(ctx->elbow_reentry));
    memset(&ctx->elbow_return, 0, sizeof(ctx->elbow_return));
    memset(ctx->wrist_return, 0, sizeof(ctx->wrist_return));
    ctx->arm_tracking_started = ctx->arm_stationary = 0U;
    ctx->virtual_last_session = ctx->virtual_session = session;
    ctx->virtual_renewed_us = now_us;
    ctx->virtual_compute = 1U;
    ctx->servo_command_valid = 0U;
    return 1;
}

void robot_motion_sync_input(RobotPipeline *ctx, int stationary, uint32_t input_epoch)
{
    if (ctx == NULL) return;
    if (ctx->arm_input_epoch != input_epoch) {
        ctx->arm_tracking_started = 0U;
        ctx->elbow_return.pending_epoch = ctx->elbow_return.valid;
        memset(&ctx->elbow_reentry, 0, sizeof(ctx->elbow_reentry));
    }
    ctx->arm_input_epoch = input_epoch;
    ctx->arm_stationary = stationary ? 1U : 0U;
}

static float elbow_reentry_update(ElbowReentryState *state, float desired,
    float reference, int reachable, int held, uint32_t frame_id, uint32_t now)
{
    uint32_t elapsed = now - state->time_us;
    if (state->seen && frame_id == state->frame_id) return reference;
    state->seen = 1U;
    state->frame_id = frame_id;
    state->time_us = now;
    if (!reachable) {
        state->outside = 1U;
        state->pending = state->ramping = 0U;
        return reference;
    }
    if (held) {
        state->pending = 0U;
        return reference;
    }
    if (state->outside) {
        uint32_t span = now - state->candidate_time_us;
        if (!state->pending || span > 250000U || fabsf(desired - state->candidate) > 10.0f) {
            state->pending = 1U;
            state->candidate = desired;
            state->candidate_time_us = now;
            return reference;
        }
        if (span < 80000U) return reference;
        state->outside = state->pending = 0U;
        state->ramping = 1U;
    }
    if (state->ramping) {
        float limit;
        if (elapsed > 250000U) {
            state->outside = 1U;
            state->pending = 0U;
            return reference;
        }
        limit = 45.0f * (float)elapsed / 1000000.0f;
        if (desired > reference + limit) return reference + limit;
        if (desired < reference - limit) return reference - limit;
        state->ramping = 0U;
    }
    return desired;
}

static float elbow_return_prepare(ElbowReturnState *state, float goal,
    float reference, int held, uint32_t now)
{
    uint32_t elapsed = now - state->time_us;
    float result = goal;
    if (state->valid && (state->pending_epoch || (state->held && !held)) &&
        fabsf(goal - reference) > 8.0f) state->active = 1U;
    if (state->active) {
        float limit;
        if (elapsed > 100000U) elapsed = 100000U;
        limit = 45.0f * (float)elapsed / 1000000.0f;
        if (goal > reference + limit) result = reference + limit;
        else if (goal < reference - limit) result = reference - limit;
        else state->active = 0U;
    }
    state->goal = goal;
    state->valid = 1U;
    state->held = held ? 1U : 0U;
    state->pending_epoch = 0U;
    state->time_us = now;
    return result;
}

static void elbow_return_tick(RobotPipeline *ctx)
{
    ForearmJointCommand command;
    float difference;
    if (!robot_motion_calculation_enabled(ctx) || !ctx->command_valid ||
        !ctx->elbow_return.valid || !ctx->elbow_return.active) return;
    command = ctx->command;
    command.elbow_roll_deg = ctx->elbow_return.goal;
    if (!forearm_safety_check_apply(&command, NULL)) return;
    difference = command.elbow_roll_deg - ctx->command.elbow_roll_deg;
    if (difference > 0.90f) command.elbow_roll_deg = ctx->command.elbow_roll_deg + 0.90f;
    else if (difference < -0.90f) command.elbow_roll_deg = ctx->command.elbow_roll_deg - 0.90f;
    if (!forearm_safety_check_apply(&command, NULL)) return;
    ctx->elbow_return.time_us = robot_pipeline_time_us(ctx);
    if (fabsf(ctx->elbow_return.goal - command.elbow_roll_deg) <= 0.0001f)
        ctx->elbow_return.active = 0U;
    if (!robot_pipeline_same_command(&command, &ctx->command) || ctx->motion.held) {
        forearm_calibration_set_target(&ctx->motion, &command);
        ctx->command = command;
        ctx->retargets++;
    }
}

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

static float wrist_return_reference(RobotPipeline *ctx, unsigned axis)
{
    if (ctx->wrist_return[axis].valid) return ctx->wrist_return[axis].goal;
    return *wrist_return_angle(&ctx->command, axis);
}

static void wrist_return_prepare(RobotPipeline *ctx, ForearmJointCommand *command,
                                WristReturnState *next_states)
{
    unsigned axis;
    uint32_t now_us = robot_pipeline_time_us(ctx);
    int held = ctx->gripper_independent && ctx->output_enabled && ctx->arm_stationary &&
        ctx->arm_tracking_started && ctx->command_valid;
    for (axis = 0U; axis < 2U; ++axis) {
        WristReturnState *state = &next_states[axis];
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

static void wrist_return_tick(RobotPipeline *ctx)
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
        WristReturnState *state = &ctx->wrist_return[axis];
        if (!state->active) continue;
        state->time_us = robot_pipeline_time_us(ctx);
        if (fabsf(*wrist_return_angle(&command, axis) - state->goal) < 0.0001f)
            state->active = 0U;
    }
    if (!robot_pipeline_same_command(&command, &ctx->command) || ctx->motion.held) {
        forearm_calibration_set_target(&ctx->motion, &command);
        ctx->command = command;
        ctx->retargets++;
    }
}

int robot_motion_set_target(RobotPipeline *ctx)
{
    ForearmJointCommand command;
    GripperLatchState next_gripper;
    ElbowReentryState next_elbow;
    ElbowReturnState next_return;
    WristReturnState next_wrist[2];
    int elbow_roll_reachable;

    if (ctx != NULL) {
        ctx->target_result = ROBOT_TARGET_NONE; /* [TRACE] 이번 프레임의 결과를 먼저 "없음"으로 둔다. */
    }
    if (ctx == NULL || !robot_motion_calculation_enabled(ctx) ||
        !ctx->target_ready) return 0;

    /* 호출 순서 계약: unwrap은 validate를 통과한 타겟만 받는다. */
    if (!forearm_motion_control_validate_target(&ctx->target)) {
        if (!ctx->gripper_independent) {
            ctx->gripper_latch.close_count = 0U;
            ctx->gripper_latch.open_count = 0U;
        }
        ctx->commands_rejected++;
        ctx->target_result = ROBOT_TARGET_REJECT_VALIDATE; /* [TRACE] */
        return 0;
    }
    forearm_motion_control_unwrap_target(&ctx->unwrap, &ctx->target);
    elbow_roll_reachable = forearm_motion_control_elbow_roll_reachable(ctx->target.elbow_roll_deg);

    forearm_motion_control_map_target(&ctx->target, &command);
    forearm_motion_control_apply_limits(&command);

    /* A wrapped direction outside the finite elbow-roll travel is not an
     * endpoint target. Keep the last approved target (or boot position),
     * while allowing the other joints to follow if the mixed pose is safe. */
    if (!elbow_roll_reachable) {
        command.elbow_roll_deg = ctx->elbow_return.valid ? ctx->elbow_return.goal : ctx->command_valid ? ctx->command.elbow_roll_deg
                                                    : ctx->output.elbow_roll_deg;
    }

    next_elbow = ctx->elbow_reentry;
    command.elbow_roll_deg = elbow_reentry_update(&next_elbow, command.elbow_roll_deg,
        ctx->elbow_return.valid ? ctx->elbow_return.goal : ctx->command_valid ? ctx->command.elbow_roll_deg : ctx->output.elbow_roll_deg,
        elbow_roll_reachable, ctx->gripper_independent && robot_motion_calculation_enabled(ctx) &&
        ctx->arm_stationary && ctx->arm_tracking_started && ctx->command_valid,
        ctx->target.frame_id, robot_pipeline_time_us(ctx));

    if (ctx->target.wrist_valid) {
        if (!forearm_motion_control_wrist_pitch_reachable(ctx->target.wrist_pitch_deg)) {
            command.wrist_pitch_deg = ctx->command_valid ? wrist_return_reference(ctx, 0U)
                                                        : ctx->output.wrist_pitch_deg;
        }
        if (!forearm_motion_control_wrist_roll_reachable(ctx->target.wrist_roll_deg)) {
            command.wrist_roll_deg = ctx->command_valid ? wrist_return_reference(ctx, 1U)
                                                       : ctx->output.wrist_roll_deg;
        }
    }

    /* Wrist angles and 2D finger separation have independent validity.
     * Safety is checked on the final mixed command in either case. */
    if (!ctx->target.wrist_valid) {
        command.wrist_pitch_deg = ctx->output.wrist_pitch_deg;
        command.wrist_roll_deg = ctx->output.wrist_roll_deg;
    }
    if (ctx->gripper_independent)
        command.gripper_norm = ctx->command_valid ? ctx->command.gripper_norm : ctx->output.gripper_norm;
    else if (!ctx->target.gripper_valid)
        command.gripper_norm = ctx->output.gripper_norm;
    next_gripper = ctx->gripper_latch;
    if (ctx->gripper_independent && robot_motion_calculation_enabled(ctx) && ctx->arm_stationary &&
        ctx->arm_tracking_started && ctx->command_valid) {
        command.elbow_roll_deg = ctx->elbow_return.valid ? ctx->elbow_return.goal : ctx->command.elbow_roll_deg;
        command.elbow_pitch_deg = ctx->command.elbow_pitch_deg;
        command.wrist_pitch_deg = wrist_return_reference(ctx, 0U);
        command.wrist_roll_deg = wrist_return_reference(ctx, 1U);
    }
    if (robot_motion_calculation_enabled(ctx) && !ctx->gripper_independent) {
        float held_gripper = ctx->command_valid ? ctx->command.gripper_norm : ctx->output.gripper_norm;
        int gripper_fresh = ctx->target.gripper_valid &&
            ctx->target.frame_id == ctx->pose.frame_id && forearm_target_gripper_fresh();
        command.gripper_norm = gripper_latch_update(&next_gripper, command.gripper_norm,
            held_gripper, gripper_fresh, ctx->pose.frame_id, ctx->pose.gripper_2d.source,
            robot_pipeline_time_us(ctx));
    }
    next_return = ctx->elbow_return;
    memcpy(next_wrist, ctx->wrist_return, sizeof(next_wrist));
    if (forearm_safety_check_apply(&command, NULL))
        wrist_return_prepare(ctx, &command, next_wrist);
    if (forearm_safety_check_apply(&command, NULL)) {
        command.elbow_roll_deg = elbow_return_prepare(&next_return, command.elbow_roll_deg,
            ctx->command_valid ? ctx->command.elbow_roll_deg : ctx->output.elbow_roll_deg,
            ctx->gripper_independent && robot_motion_calculation_enabled(ctx) && ctx->arm_stationary &&
            ctx->arm_tracking_started && ctx->command_valid, robot_pipeline_time_us(ctx));
    }
    command.valid = forearm_safety_check_apply(&command, NULL) ? 1U : 0U;
    /* 무효/위험이면 폐기하고 마지막으로 승인한 목표를 계속 유지한다. */
    if (!command.valid) {
        if (!ctx->gripper_independent) {
            ctx->gripper_latch.close_count = 0U;
            ctx->gripper_latch.open_count = 0U;
        }
        ctx->commands_rejected++;
        ctx->mapped_target = command; /* [TRACE] 거부돼도 매핑된 각도는 남는다(valid만 0). 사유 flags를 로그에서 다시 구한다. */
        ctx->target_result = ROBOT_TARGET_REJECT_SAFETY; /* [TRACE] */
        return 0;
    }
    ctx->elbow_return = next_return;
    memcpy(ctx->wrist_return, next_wrist, sizeof(next_wrist));
    ctx->elbow_reentry = next_elbow;
    ctx->gripper_latch = next_gripper;
    ctx->commands_accepted++;
    ctx->arm_tracking_started = 1U;
    ctx->mapped_target = command; /* [TRACE] */

    /* HOLD 프레임처럼 직전과 같은 명령이면 재계획하지 않는다(램프가 속도 0에서 다시 시작되는 것을 막는다).
     * 단, motion이 hold 중(motion.held!=0)이면 값이 같아도 반드시 다시
     * set_target을 불러야 한다 -- forearm_calibration_step()의 emergency_hold가
     * 축의 target=q/v=0으로 얼어붙여 놨으므로, set_target 호출 자체가 재개의
     * 유일한 신호다(forearm_calibration.h의 forearm_calibration_set_target()
     * 주석 참고). blocked_flags는 매틱 자동으로 OK로 돌아갈 수 있어(멈춘 자리
     * 자체는 항상 안전검사를 통과함) 재개 판단 기준으로 쓰면 안 된다 --
     * Codex 코드리뷰 2026-09-24에서 이 자리에 blocked_flags를 쓰던 이전 수정이
     * 대부분 상황에서 무효화된다는 걸 발견했다. 여기서 스킵하면 같은 명령으로는
     * 영원히 안 풀린다. */
    if (ctx->command_valid && robot_pipeline_same_command(&command, &ctx->command) &&
        ctx->motion.held == 0) {
        ctx->target_result = ROBOT_TARGET_SAME; /* [TRACE] */
        return 1;
    }

    forearm_calibration_set_target(&ctx->motion, &command);
    ctx->command = command;
    ctx->command_valid = 1U;
    ctx->retargets++;
    ctx->target_result = ROBOT_TARGET_NEW; /* [TRACE] */
    return 1;
}

int robot_motion_step(RobotPipeline *ctx)
{
    if (ctx == NULL) return 0;

    ctx->ticks++;
    wrist_return_tick(ctx);
    elbow_return_tick(ctx);
    if (robot_motion_calculation_enabled(ctx))
        forearm_calibration_step(&ctx->motion, &ctx->output);
    return ctx->output.valid ? 1 : 0;
}
