#include "integration/agent_pipeline.h"

#include <stddef.h>
#include <math.h>
#include <string.h>

#include "human_target_angle/agent1_forearm_stage.h"
#include "output_controller/output_control.h"
#include "output_controller/servo_hal.h"
#include "integration/trace.h"

/*
 * [TRACE] UART 로그용 기록. ROBOT_TRACE를 정의하지 않으면 아무것도 하지 않는다.
 * 호출 지점마다 "[TRACE]" 태그를 붙였다.
 */
#ifdef ROBOT_TRACE
#define TRACE_SET_A1_RC(ctx, rc)       ((ctx)->a1_rc = (int8_t)(rc))
#define TRACE_SET_A2_RESULT(ctx, r)    ((ctx)->a2_result = (uint8_t)(r))
#define TRACE_SET_A2_MAPPED(ctx, cmd)  ((ctx)->a2_mapped = (cmd))
#else
#define TRACE_SET_A1_RC(ctx, rc)       ((void)0)
#define TRACE_SET_A2_RESULT(ctx, r)    ((void)0)
#define TRACE_SET_A2_MAPPED(ctx, cmd)  ((void)0)
#endif

/* 데모에서 사용하는 팔. 입력 좌표의 팔과 반드시 일치시켜야 한다(Agent1 담당자 확인). */
#define AGENT_PIPELINE_ACTIVE_ARM POSE_ARM_RIGHT

/*
 * 전원 인가 시 팔이 놓이는 홈 자세(서보 각도 기준). gripper_norm은 0.0=Close, 1.0=Open이다.
 * PR #55/#57(Agent1)에서 idle 자세로 갱신됐다. wrist_roll 보정 offset은 실측 87도
 * (forearm_calibration_config.c)지만 홈 상수는 아직 90이다 -- 실측 offset 기반 최종값은 아니다.
 * agent_pipeline_init()이 home_within_limits()/home_is_safe()로 부팅 시 범위와
 * FK 안전검사를 둘 다 확인한다.
 */
static const ForearmJointCommand k_home_pose = {
    .elbow_roll_deg = 90.0f,
    .elbow_pitch_deg = 70.0f,
    .wrist_pitch_deg = 100.0f,
    .wrist_roll_deg = 90.0f,
    .gripper_norm = 0.05f,
    .valid = 1U
};

static int same_command(const ForearmJointCommand *a, const ForearmJointCommand *b)
{
    return a->elbow_roll_deg == b->elbow_roll_deg &&
           a->elbow_pitch_deg == b->elbow_pitch_deg &&
           a->wrist_pitch_deg == b->wrist_pitch_deg &&
           a->wrist_roll_deg == b->wrist_roll_deg &&
           a->gripper_norm == b->gripper_norm &&
           a->valid == b->valid;
}

/* 홈이 관절 한계 안인지 확인한다(설정 오류를 부팅 시점에 잡는다). */
static int home_within_limits(void)
{
    ForearmJointCommand clamped = k_home_pose;

    forearm_motion_control_apply_limits(&clamped);
    return same_command(&clamped, &k_home_pose);
}

/* 홈이 FK 자기충돌/테이블충돌도 통과하는지 확인한다. 홈은 forearm_calibration_apply()의
 * 안전검사 경로를 거치지 않고 set_target()에 직접 들어가므로(위 주석 참고), 이 검사가
 * 없으면 홈 값이 unsafe해도 agent_pipeline_init()이 그냥 성공하고 forearm_calibration_step()이
 * 매틱 output->valid=1을 찍어 그 위험한 자세를 그대로 서보로 내보낸다 -- step()은 후보가
 * 안전검사에 실패해도 항상 output->valid=1로 끝맺으므로(직전 승인 위치를 계속 보여주기
 * 위해서다), "검사 존재"와 "검사 실패 시 출력 차단"이 다르다(Codex 코드리뷰 2026-09-24
 * 발견). 여기서 미리 막아 그 취약 경로 자체를 없앤다. */
static int home_is_safe(void)
{
    return forearm_safety_check_apply(&k_home_pose, NULL);
}

static float gripper_latch_update(AgentGripperLatch *latch, float desired,
                                  float held, int fresh, uint32_t frame_id,
                                  uint8_t source, uint32_t now_us)
{
    uint32_t advance = frame_id - latch->frame_id;
    if (latch->have_frame && !advance) return held;
    if (latch->have_frame && advance >= 0x80000000U)
        memset(latch, 0, sizeof(*latch));
    if (latch->have_frame && (source != latch->source ||
        now_us - latch->sample_time_us > AGENT_GRIPPER_SAMPLE_MAX_GAP_US)) {
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
            ? now_us - latch->applied_time_us >= AGENT_GRIPPER_CLOSE_HOLD_US
            : now_us - latch->confirmed_time_us >= AGENT_GRIPPER_CLOSE_MAX_WAIT_US;
        if (hold_done && desired > 0.0f) {
            ++latch->open_count;
            if (latch->open_count >= AGENT_GRIPPER_CONFIRM_SAMPLES) {
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
        if (latch->close_count >= AGENT_GRIPPER_CONFIRM_SAMPLES) {
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

static float gripper_hysteresis_update(AgentGripperLatch *latch, float desired,
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
         now_us - latch->sample_time_us > AGENT_GRIPPER_SAMPLE_MAX_GAP_US)) ||
        (latch->candidate_active &&
         now_us - latch->candidate_started_us > AGENT_GRIPPER_CANDIDATE_WINDOW_US)) {
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
    if (distance_px <= AGENT_GRIPPER_CLOSE_START_PX) {
        if (!latch->candidate_active) {
            latch->candidate_active = 1U;
            latch->candidate_started_us = now_us;
        }
        latch->cancel_count = 0U;
        ++latch->close_count;
        if (latch->close_count >= AGENT_GRIPPER_CONFIRM_SAMPLES) {
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
        if (distance_px > AGENT_GRIPPER_CANCEL_PX) {
            ++latch->cancel_count;
            if (latch->cancel_count >= AGENT_GRIPPER_CANCEL_SAMPLES) {
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

static void gripper_latch_applied(AgentGripperLatch *latch, float command, uint32_t now_us)
{
    if (latch->latched && !latch->closed_applied && command == 0.0f) {
        latch->closed_applied = 1U;
        latch->applied_time_us = now_us;
    }
}

void agent_pipeline_reset_gripper_latch(AgentPipelineContext *ctx)
{
    if (ctx != NULL) memset(&ctx->gripper_latch, 0, sizeof(ctx->gripper_latch));
    if (ctx != NULL) {
        uint8_t manual_open = ctx->gripper_motion_hold.manual_open;
        memset(&ctx->gripper_motion_hold, 0, sizeof(ctx->gripper_motion_hold));
        ctx->gripper_motion_hold.manual_open = manual_open;
    }
    agent1_forearm_stage_reset_gripper();
    if (ctx != NULL) {
        memset(&ctx->elbow_reentry, 0, sizeof(ctx->elbow_reentry));
        memset(&ctx->elbow_return, 0, sizeof(ctx->elbow_return));
        memset(ctx->wrist_return, 0, sizeof(ctx->wrist_return));
        ctx->wrist_observation_fresh = 0U;
    }
}

static uint32_t gripper_latch_time_us(const AgentPipelineContext *ctx)
{
#ifdef ROBOT_TRACE
    (void)ctx;
    return platform_trace_time_us();
#else
    return ctx->ticks * 20000U;
#endif
}

int agent_pipeline_init_mode(AgentPipelineContext *ctx, int enable_robot_pwm)
{
    ServoPwmCommand pwm;

    if (ctx == NULL) return -1;

    memset(ctx, 0, sizeof(*ctx));
    ctx->output_enabled = enable_robot_pwm ? 1U : 0U;

    if (agent1_forearm_stage_init() != 0) return -1;
    forearm_motion_control_unwrap_state_init(&ctx->unwrap);
    forearm_calibration_state_init(&ctx->motion);
    output_control_init();

    if (!home_within_limits() || !home_is_safe()) return -1;

    /*
     * 홈으로 부트스트랩한다. Agent2는 첫 set_target을 램프 없이 스냅하므로,
     * 홈을 먼저 넣어 두면 첫 실제 타겟이 홈에서부터 속도제한 램프로 출발한다.
     * (홈은 상수라 apply()를 거치지 않고 set_target에 직접 넣는다.)
     */
    forearm_calibration_set_target(&ctx->motion, &k_home_pose);
    forearm_calibration_step(&ctx->motion, &ctx->output);

    /* 홈 shadow 쓰기 + UPDATE 후 enable 순서로 부팅 직후 서보가 튀지 않게 한다. */
    if (!output_control_update(&ctx->output, &pwm)) return -1;
    ctx->pwm = pwm;
    ctx->agent3_command = ctx->output;
    ctx->agent3_command_valid = 1U;
    ctx->agent3_command_tick = ctx->ticks;
    if (ctx->output_enabled) {
        if (!servo_hal_apply_joint_command(&ctx->output, &pwm, 0)) return -1;
        ctx->applied_command = ctx->output;
        ctx->applied_command_valid = 1U;
        if (!servo_hal_enable()) return -1;
    } else {
        if (!servo_hal_disable()) return -1;
    }

    return 0;
}

int agent_pipeline_init(AgentPipelineContext *ctx)
{
    return agent_pipeline_init_mode(ctx, 1);
}

static AgentPipelineOutputResult prepare_output_reference(
    const AgentPipelineContext *ctx, ForearmJointCommand *reference,
    ForearmMotionState *motion, ServoPwmCommand *pwm)
{
    ForearmJointCommand clamped;
    ForearmJointCommand output;
    unsigned axis;
    float positions[FOREARM_MOTION_JOINT_COUNT];

    *reference = ctx->applied_command_valid ? ctx->applied_command : k_home_pose;
    if (!reference->valid || !isfinite(reference->elbow_roll_deg) ||
        !isfinite(reference->elbow_pitch_deg) || !isfinite(reference->wrist_pitch_deg) ||
        !isfinite(reference->wrist_roll_deg) || !isfinite(reference->gripper_norm)) {
        return AGENT_OUTPUT_RESULT_REFERENCE_INVALID;
    }
    clamped = *reference;
    forearm_motion_control_apply_limits(&clamped);
    if (!same_command(&clamped, reference) || reference->gripper_norm < 0.0f ||
        reference->gripper_norm > 1.0f) return AGENT_OUTPUT_RESULT_REFERENCE_LIMITS;
    if (!forearm_safety_check_apply(reference, NULL))
        return AGENT_OUTPUT_RESULT_REFERENCE_UNSAFE;

    forearm_calibration_state_init(motion);
    forearm_calibration_set_target(motion, reference);
    forearm_calibration_step(motion, &output);
    if (!same_command(&output, reference) || !motion->has_target || motion->held ||
        motion->blocked_flags != FOREARM_SAFETY_CHECK_OK ||
        motion->gripper != reference->gripper_norm) return AGENT_OUTPUT_RESULT_REFERENCE_STATE;
    positions[0] = reference->elbow_roll_deg;
    positions[1] = reference->elbow_pitch_deg;
    positions[2] = reference->wrist_pitch_deg;
    positions[3] = reference->wrist_roll_deg;
    for (axis = 0; axis < FOREARM_MOTION_JOINT_COUNT; ++axis) {
        if (motion->axes[axis].v != 0.0 || motion->axes[axis].q != positions[axis] ||
            motion->axes[axis].q != motion->axes[axis].target)
            return AGENT_OUTPUT_RESULT_REFERENCE_STATE;
    }
    if (!output_control_update(reference, pwm)) return AGENT_OUTPUT_RESULT_CONVERSION_FAILED;
    return AGENT_OUTPUT_RESULT_NONE;
}

static void commit_output_reference(AgentPipelineContext *ctx,
                                    const ForearmJointCommand *reference,
                                    const ForearmMotionState *motion,
                                    const ServoPwmCommand *pwm)
{
    ctx->motion = *motion;
    ctx->output = *reference;
    ctx->command = *reference;
    ctx->command_valid = 1U;
    ctx->agent3_command = *reference;
    ctx->agent3_command_valid = 1U;
    ctx->agent3_command_tick = ctx->ticks;
    ctx->pwm = *pwm;
    ctx->target_ready = 0U;
    forearm_motion_control_unwrap_state_init(&ctx->unwrap);
}

int agent_pipeline_set_output_enabled(AgentPipelineContext *ctx, int enabled)
{
    ForearmJointCommand reference;
    ForearmMotionState motion;
    ServoPwmCommand pwm;
    AgentPipelineOutputResult result;

    if (ctx == NULL) return 0;
    if (enabled && ctx->output_faulted) {
        ctx->output_result = AGENT_OUTPUT_RESULT_HAL_RECOVERY_DISABLE_FAILED;
        return 0;
    }
    if (enabled && ctx->output_enabled) {
        ctx->output_result = AGENT_OUTPUT_RESULT_ENABLED;
        return 1;
    }
    result = prepare_output_reference(ctx, &reference, &motion, &pwm);
    if (!enabled) {
        if (!servo_hal_disable()) {
            ctx->servo_errors++;
            ctx->output_result = AGENT_OUTPUT_RESULT_HAL_DISABLE_FAILED;
            return 0;
        }
        ctx->output_enabled = 0U;
        ctx->arm_stationary = ctx->arm_tracking_started = 0U;
        ctx->output_parked = 1U;
        ctx->output_faulted = 0U;
        agent_pipeline_reset_gripper_latch(ctx);
        if (result != AGENT_OUTPUT_RESULT_NONE) {
            ctx->output_result = result;
            return 0;
        }
        commit_output_reference(ctx, &reference, &motion, &pwm);
        ctx->output_result = AGENT_OUTPUT_RESULT_DISABLED;
        return 1;
    }
    if (result != AGENT_OUTPUT_RESULT_NONE) {
        ctx->output_result = result;
        return 0;
    }
    result = AGENT_OUTPUT_RESULT_HAL_APPLY_FAILED;
    if (servo_hal_apply_joint_command(&reference, &pwm, 0)) {
        result = servo_hal_enable() ? AGENT_OUTPUT_RESULT_NONE
                                   : AGENT_OUTPUT_RESULT_HAL_ENABLE_FAILED;
    }
    if (result != AGENT_OUTPUT_RESULT_NONE) {
        ctx->servo_errors++;
        if (!servo_hal_disable()) {
            ctx->servo_errors++;
            ctx->output_faulted = 1U;
            result = AGENT_OUTPUT_RESULT_HAL_RECOVERY_DISABLE_FAILED;
        }
        ctx->output_result = result;
        return 0;
    }
    commit_output_reference(ctx, &reference, &motion, &pwm);
    ctx->applied_command = reference;
    ctx->applied_command_valid = 1U;
    ctx->output_enabled = 1U;
    ctx->output_parked = 0U;
    ctx->output_result = AGENT_OUTPUT_RESULT_ENABLED;
    agent_pipeline_reset_gripper_latch(ctx);
    ctx->servo_writes++;
    return 1;
}

const char *agent_pipeline_output_result_name(const AgentPipelineContext *ctx)
{
    if (ctx == NULL) return "invalid_pipeline";
    switch (ctx->output_result) {
        case AGENT_OUTPUT_RESULT_NONE: return "none";
        case AGENT_OUTPUT_RESULT_ENABLED: return "enabled_physical_pose_not_verified";
        case AGENT_OUTPUT_RESULT_DISABLED: return "disabled_last_written_command_latched";
        case AGENT_OUTPUT_RESULT_REFERENCE_INVALID: return "rejected_resume_reference_invalid";
        case AGENT_OUTPUT_RESULT_REFERENCE_LIMITS: return "rejected_resume_reference_limits";
        case AGENT_OUTPUT_RESULT_REFERENCE_UNSAFE: return "rejected_resume_reference_FK_safety";
        case AGENT_OUTPUT_RESULT_REFERENCE_STATE: return "rejected_resume_reference_state";
        case AGENT_OUTPUT_RESULT_CONVERSION_FAILED: return "rejected_resume_PWM_conversion";
        case AGENT_OUTPUT_RESULT_HAL_APPLY_FAILED: return "HAL_resume_apply_failed";
        case AGENT_OUTPUT_RESULT_HAL_ENABLE_FAILED: return "HAL_enable_failed_output_disabled";
        case AGENT_OUTPUT_RESULT_HAL_DISABLE_FAILED: return "HAL_disable_failed_state_preserved";
        case AGENT_OUTPUT_RESULT_HAL_RECOVERY_DISABLE_FAILED:
            return "HAL_recovery_disable_failed_hardware_state_unverified";
        default: return "unknown_output_result";
    }
}

int agent1_run(AgentPipelineContext *ctx, const HumanPose2D *pose, float dt_sec)
{
    int rc;

    if (ctx == NULL || pose == NULL) return 0;

    ctx->pose = *pose;
    ctx->dt_sec = dt_sec;
    ctx->frames_in++;
    ctx->target_ready = 0U;
    ctx->wrist_observation_fresh = 0U;

    /*
     * 반환값(1/0/-1)은 보지 않는다. 짧은 dropout 동안 HOLD(0)에서도 valid=1인
     * 마지막 정상 타겟이 나오므로, 다음 단계 진행 여부는 출력 valid로만 판단한다.
     */
    rc = agent1_forearm_stage_run(&ctx->pose, AGENT_PIPELINE_ACTIVE_ARM, dt_sec);
    TRACE_SET_A1_RC(ctx, rc); /* [TRACE] 로그용으로만 보관한다. 다음 단계 진행 여부의 판단에는 쓰지 않는다. */
    (void)rc;

    if (!agent1_forearm_stage_output()->valid) return 0;

    ctx->target = *agent1_forearm_stage_output();
    ctx->target_ready = 1U;
    ctx->targets_valid++;
    return 1;
}

int agent1_run_stereo(AgentPipelineContext *ctx, const HumanPose2D *image_pose,
                      const HumanPose3D *measured_pose, float dt_sec)
{
    int rc;
    if (ctx == NULL || image_pose == NULL || measured_pose == NULL) return 0;
    ctx->pose = *image_pose;
    ctx->dt_sec = dt_sec;
    ctx->frames_in++;
    ctx->target_ready = 0U;
    ctx->wrist_observation_fresh = measured_pose->valid &&
        measured_pose->wrist.valid && measured_pose->finger1.valid &&
        measured_pose->finger2.valid && measured_pose->frame_id == image_pose->frame_id;
    rc = agent1_forearm_stage_run_stereo(image_pose, measured_pose,
                                        AGENT_PIPELINE_ACTIVE_ARM, dt_sec);
    TRACE_SET_A1_RC(ctx, rc);
    (void)rc;
    if (!agent1_forearm_stage_output()->valid) return 0;
    ctx->target = *agent1_forearm_stage_output();
    ctx->target_ready = 1U;
    ctx->targets_valid++;
    return 1;
}

void agent_pipeline_sync_arm_motion(AgentPipelineContext *ctx, int stationary, uint32_t input_epoch)
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

static float elbow_reentry_update(AgentElbowReentry *state, float desired,
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

static float elbow_return_prepare(AgentElbowReturn *state, float goal,
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

static void elbow_return_tick(AgentPipelineContext *ctx)
{
    ForearmJointCommand command;
    float difference;
    if (!ctx->output_enabled || ctx->output_parked || !ctx->command_valid ||
        !ctx->elbow_return.valid || !ctx->elbow_return.active) return;
    command = ctx->command;
    command.elbow_roll_deg = ctx->elbow_return.goal;
    if (!forearm_safety_check_apply(&command, NULL)) return;
    difference = command.elbow_roll_deg - ctx->command.elbow_roll_deg;
    if (difference > 0.90f) command.elbow_roll_deg = ctx->command.elbow_roll_deg + 0.90f;
    else if (difference < -0.90f) command.elbow_roll_deg = ctx->command.elbow_roll_deg - 0.90f;
    if (!forearm_safety_check_apply(&command, NULL)) return;
    ctx->elbow_return.time_us = gripper_latch_time_us(ctx);
    if (fabsf(ctx->elbow_return.goal - command.elbow_roll_deg) <= 0.0001f)
        ctx->elbow_return.active = 0U;
    if (!same_command(&command, &ctx->command) || ctx->motion.held) {
        forearm_calibration_set_target(&ctx->motion, &command);
        ctx->command = command;
        ctx->retargets++;
    }
}

#include "wrist_return.h"

int agent2_run(AgentPipelineContext *ctx)
{
    ForearmJointCommand command;
    AgentGripperLatch next_gripper;
    AgentElbowReentry next_elbow;
    AgentElbowReturn next_return;
    AgentWristReturn next_wrist[2];
    int elbow_roll_reachable;

    if (ctx != NULL) {
        TRACE_SET_A2_RESULT(ctx, A2_RESULT_NONE); /* [TRACE] 이번 프레임의 결과를 먼저 "없음"으로 둔다. */
    }
    if (ctx == NULL || (!ctx->output_enabled && ctx->output_parked) ||
        !ctx->target_ready) return 0;

    /* 호출 순서 계약: unwrap은 validate를 통과한 타겟만 받는다. */
    if (!forearm_motion_control_validate_target(&ctx->target)) {
        if (!ctx->gripper_independent) {
            ctx->gripper_latch.close_count = 0U;
            ctx->gripper_latch.open_count = 0U;
        }
        ctx->commands_rejected++;
        TRACE_SET_A2_RESULT(ctx, A2_RESULT_REJECT_VALIDATE); /* [TRACE] */
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
        elbow_roll_reachable, ctx->gripper_independent && ctx->output_enabled &&
        ctx->arm_stationary && ctx->arm_tracking_started && ctx->command_valid,
        ctx->target.frame_id, gripper_latch_time_us(ctx));

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
    if (ctx->gripper_independent && ctx->output_enabled && ctx->arm_stationary &&
        ctx->arm_tracking_started && ctx->command_valid) {
        command.elbow_roll_deg = ctx->elbow_return.valid ? ctx->elbow_return.goal : ctx->command.elbow_roll_deg;
        command.elbow_pitch_deg = ctx->command.elbow_pitch_deg;
        command.wrist_pitch_deg = wrist_return_reference(ctx, 0U);
        command.wrist_roll_deg = wrist_return_reference(ctx, 1U);
    }
    if (ctx->output_enabled && !ctx->gripper_independent) {
        float held_gripper = ctx->command_valid ? ctx->command.gripper_norm : ctx->output.gripper_norm;
        int gripper_fresh = ctx->target.gripper_valid &&
            ctx->target.frame_id == ctx->pose.frame_id && agent1_forearm_stage_gripper_fresh();
        command.gripper_norm = gripper_latch_update(&next_gripper, command.gripper_norm,
            held_gripper, gripper_fresh, ctx->pose.frame_id, ctx->pose.gripper_2d.source,
            gripper_latch_time_us(ctx));
    }
    next_return = ctx->elbow_return;
    memcpy(next_wrist, ctx->wrist_return, sizeof(next_wrist));
    if (forearm_safety_check_apply(&command, NULL))
        wrist_return_prepare(ctx, &command, next_wrist);
    if (forearm_safety_check_apply(&command, NULL)) {
        command.elbow_roll_deg = elbow_return_prepare(&next_return, command.elbow_roll_deg,
            ctx->command_valid ? ctx->command.elbow_roll_deg : ctx->output.elbow_roll_deg,
            ctx->gripper_independent && ctx->output_enabled && ctx->arm_stationary &&
            ctx->arm_tracking_started && ctx->command_valid, gripper_latch_time_us(ctx));
    }
    command.valid = forearm_safety_check_apply(&command, NULL) ? 1U : 0U;
    /* 무효/위험이면 폐기하고 마지막으로 승인한 목표를 계속 유지한다. */
    if (!command.valid) {
        if (!ctx->gripper_independent) {
            ctx->gripper_latch.close_count = 0U;
            ctx->gripper_latch.open_count = 0U;
        }
        ctx->commands_rejected++;
        TRACE_SET_A2_MAPPED(ctx, command); /* [TRACE] 거부돼도 매핑된 각도는 남는다(valid만 0). 사유 flags를 로그에서 다시 구한다. */
        TRACE_SET_A2_RESULT(ctx, A2_RESULT_REJECT_SAFETY); /* [TRACE] */
        return 0;
    }
    ctx->elbow_return = next_return;
    memcpy(ctx->wrist_return, next_wrist, sizeof(next_wrist));
    ctx->elbow_reentry = next_elbow;
    ctx->gripper_latch = next_gripper;
    ctx->commands_accepted++;
    ctx->arm_tracking_started = 1U;
    TRACE_SET_A2_MAPPED(ctx, command); /* [TRACE] */

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
    if (ctx->command_valid && same_command(&command, &ctx->command) &&
        ctx->motion.held == 0) {
        TRACE_SET_A2_RESULT(ctx, A2_RESULT_SAME); /* [TRACE] */
        return 1;
    }

    forearm_calibration_set_target(&ctx->motion, &command);
    ctx->command = command;
    ctx->command_valid = 1U;
    ctx->retargets++;
    TRACE_SET_A2_RESULT(ctx, A2_RESULT_NEW); /* [TRACE] */
    return 1;
}

static void gripper_motion_sample(AgentGripperMotionHold *state,
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

static int gripper_strong_open_confirm(AgentGripperOpenConfirm *state, float command,
                                      int latched, int fresh, uint32_t frame_id,
                                      uint8_t source, uint32_t now_us)
{
    int valid;
    if (latched) {
        memset(state, 0, sizeof(*state));
        return 0;
    }
    if (!fresh) {
        if (!state->have_frame || now_us - state->fresh_us > AGENT_GRIPPER_OPEN_MISSING_GRACE_US)
            state->pending = state->count = 0U;
        return 0;
    }
    valid = isfinite(command) && command >= AGENT_GRIPPER_STRONG_OPEN_NORM;
    if (!valid || (state->have_frame && (frame_id == state->frame_id || source != state->source ||
        now_us - state->fresh_us > AGENT_GRIPPER_SAMPLE_MAX_GAP_US)))
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
    return state->count >= AGENT_GRIPPER_CONFIRM_SAMPLES &&
        now_us - state->started_us >= AGENT_GRIPPER_STRONG_OPEN_US;
}

static float gripper_motion_select(AgentGripperMotionHold *state, float command,
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

int agent_pipeline_gripper_manual(AgentPipelineContext *ctx, int open)
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
    agent1_forearm_stage_reset_gripper();
    ctx->gripper_motion_hold.manual_open = open ? 1U : 0U;
    if (open) {
        ctx->command = command;
        ctx->command_valid = 1U;
        ctx->motion.gripper = 1.0f;
    }
    return 1;
}

int agent_gripper_run(AgentPipelineContext *ctx, const HumanPose2D *pose)
{
    AgentGripperLatch next;
    AgentGripperMotionHold next_hold;
    ForearmJointCommand command, current;
    float desired = NAN;
    int fresh;
    if (ctx == NULL || pose == NULL || !ctx->gripper_independent || !ctx->output_enabled) return 0;
    fresh = agent1_forearm_stage_update_gripper(pose, &desired);
    ctx->gripper_fresh = fresh ? 1U : 0U;
    ctx->gripper_desired = desired;
    ctx->gripper_distance_px = fresh ? agent1_forearm_stage_gripper_distance_px() : NAN;
    command = ctx->command_valid ? ctx->command : ctx->output;
    next = ctx->gripper_latch;
    next_hold = ctx->gripper_motion_hold;
    command.gripper_norm = gripper_hysteresis_update(&next, desired, ctx->gripper_distance_px, command.gripper_norm,
        fresh, pose->frame_id, pose->gripper_2d.source, gripper_latch_time_us(ctx));
    command.gripper_norm = gripper_motion_select(&next_hold, command.gripper_norm,
        next.latched, fresh, pose->frame_id, pose->gripper_2d.source, gripper_latch_time_us(ctx));
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

int agent2_tick(AgentPipelineContext *ctx)
{
    if (ctx == NULL) return 0;

    ctx->ticks++;
    wrist_return_tick(ctx);
    elbow_return_tick(ctx);
    if (ctx->output_enabled || !ctx->output_parked)
        forearm_calibration_step(&ctx->motion, &ctx->output);
    return ctx->output.valid ? 1 : 0;
}

int agent3_run(AgentPipelineContext *ctx)
{
    if (ctx == NULL) return 0;
    return agent3_apply_command(ctx, &ctx->output);
}

int agent3_apply_command(AgentPipelineContext *ctx,
                         const ForearmJointCommand *command)
{
    ServoPwmCommand pwm;

    if (ctx == NULL || command == NULL || !command->valid) return 0;
    ctx->agent3_command = *command;
    ctx->agent3_command_valid = 1U;
    ctx->agent3_command_tick = ctx->ticks;

    /* PWM-disabled에서는 변환값을 trace용으로 보존한다. PWM-enabled에서는
     * HAL 실패 시 마지막 실제 적용 PWM을 보존하고 성공 후에만 commit한다. */
    if (!output_control_update(command, &pwm)) {
        ctx->servo_errors++;
        return 0;
    }
    if (!ctx->output_enabled) {
        ctx->pwm = pwm;
        return 1;
    }
    if (!servo_hal_apply_joint_command(command, &pwm, 1)) {
        ctx->servo_errors++;
        if (servo_hal_dual_status() != NULL &&
            servo_hal_dual_status()->mode == DUAL_ARM_HAL_FAULT) {
            ctx->output_enabled = 0U;
            ctx->output_faulted = 1U;
        }
        return 0;
    }

    ctx->pwm = pwm;
    ctx->applied_command = *command;
    ctx->applied_command_valid = 1U;
    gripper_motion_sample(&ctx->gripper_motion_hold, command, gripper_latch_time_us(ctx));
    gripper_latch_applied(&ctx->gripper_latch, command->gripper_norm, gripper_latch_time_us(ctx));
    ctx->servo_writes++;
    return 1;
}

int agent3_command_is_current_tick(const AgentPipelineContext *ctx)
{
    return ctx != NULL && ctx->agent3_command_valid != 0U &&
           ctx->agent3_command_tick == ctx->ticks;
}
