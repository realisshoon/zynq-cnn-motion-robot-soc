#include "integration/agent_pipeline.h"

#include <stddef.h>
#include <string.h>

#include "human_target_angle/agent1_forearm_stage.h"
#include "output_controller/output_control.h"
#include "output_controller/servo_hal.h"

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
 * 전원 인가 시 팔이 놓이는 홈 자세(서보 각도 기준).
 * TODO: 지금은 자리표시자다. 실측 offset이 나오면 교체하고 RTL reset 값과 일치시킨다.
 * gripper_norm은 0.0=Close, 1.0=Open이다. 지금은 servo_hal_startup()의 안전 자세
 * (5채널 모두 1500us)와 맞추려고 4관절 90도 + gripper 0.5(=1500us)로 뒀다.
 */
static const ForearmJointCommand k_home_pose = {
    .elbow_roll_deg = 70.0f,
    .elbow_pitch_deg = 110.0f,
    .wrist_pitch_deg = 100.0f,
    .wrist_roll_deg = 87.0f,
    .gripper_norm = 0.7f,
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

int agent_pipeline_init(AgentPipelineContext *ctx)
{
    if (ctx == NULL) return -1;

    memset(ctx, 0, sizeof(*ctx));

    if (agent1_forearm_stage_init() != 0) return -1;
    forearm_motion_control_unwrap_state_init(&ctx->unwrap);
    forearm_calibration_state_init(&ctx->motion);
    output_control_init();

    if (!home_within_limits()) return -1;

    /*
     * 홈으로 부트스트랩한다. Agent2는 첫 set_target을 램프 없이 스냅하므로,
     * 홈을 먼저 넣어 두면 첫 실제 타겟이 홈에서부터 속도제한 램프로 출발한다.
     * (홈은 상수라 apply()를 거치지 않고 set_target에 직접 넣는다.)
     */
    forearm_calibration_set_target(&ctx->motion, &k_home_pose);
    forearm_calibration_step(&ctx->motion, &ctx->output);

    /* 홈 shadow 쓰기 + UPDATE 후 enable 순서로 부팅 직후 서보가 튀지 않게 한다. */
    if (!output_control_update(&ctx->output, &ctx->pwm)) return -1;
    if (!servo_hal_apply(&ctx->pwm)) return -1;
    if (!servo_hal_enable()) return -1;

    return 0;
}

int agent1_run(AgentPipelineContext *ctx, const HumanPose2D *pose, float dt_sec)
{
    int rc;

    if (ctx == NULL || pose == NULL) return 0;

    ctx->pose = *pose;
    ctx->dt_sec = dt_sec;
    ctx->frames_in++;
    ctx->target_ready = 0U;

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

int agent2_run(AgentPipelineContext *ctx)
{
    ForearmJointCommand command;

    if (ctx != NULL) {
        TRACE_SET_A2_RESULT(ctx, A2_RESULT_NONE); /* [TRACE] 이번 프레임의 결과를 먼저 "없음"으로 둔다. */
    }
    if (ctx == NULL || !ctx->target_ready) return 0;

    /* 호출 순서 계약: unwrap은 validate를 통과한 타겟만 받는다. */
    if (!forearm_motion_control_validate_target(&ctx->target)) {
        ctx->commands_rejected++;
        TRACE_SET_A2_RESULT(ctx, A2_RESULT_REJECT_VALIDATE); /* [TRACE] */
        return 0;
    }
    forearm_motion_control_unwrap_target(&ctx->unwrap, &ctx->target);

    /* 무효/위험이면 폐기하고 마지막으로 승인한 목표를 계속 유지한다. */
    if (!forearm_calibration_apply(&ctx->target, &command)) {
        ctx->commands_rejected++;
        TRACE_SET_A2_MAPPED(ctx, command); /* [TRACE] 거부돼도 매핑된 각도는 남는다(valid만 0). 사유 flags를 로그에서 다시 구한다. */
        TRACE_SET_A2_RESULT(ctx, A2_RESULT_REJECT_SAFETY); /* [TRACE] */
        return 0;
    }
    ctx->commands_accepted++;
    TRACE_SET_A2_MAPPED(ctx, command); /* [TRACE] */

    /* HOLD 프레임처럼 직전과 같은 명령이면 재계획하지 않는다(램프가 속도 0에서 다시 시작되는 것을 막는다).
     * 단, motion이 hold 중(blocked_flags != OK)이면 값이 같아도 반드시 다시
     * set_target을 불러야 한다 -- forearm_calibration_step()의 emergency_hold가
     * 축의 target=q/v=0으로 얼어붙여 놨으므로, set_target 호출 자체가 재개의
     * 유일한 신호다(forearm_calibration.h의 forearm_calibration_set_target()
     * 주석 참고). 여기서 스킵하면 같은 명령으로는 영원히 안 풀린다. */
    if (ctx->command_valid && same_command(&command, &ctx->command) &&
        ctx->motion.blocked_flags == FOREARM_SAFETY_CHECK_OK) {
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

int agent2_tick(AgentPipelineContext *ctx)
{
    if (ctx == NULL) return 0;

    ctx->ticks++;
    forearm_calibration_step(&ctx->motion, &ctx->output);
    return ctx->output.valid ? 1 : 0;
}

int agent3_run(AgentPipelineContext *ctx)
{
    if (ctx == NULL || !ctx->output.valid) return 0;

    /* 변환에 실패하면 pwm은 갱신되지 않으므로 레지스터에 쓰지 않는다. */
    if (!output_control_update(&ctx->output, &ctx->pwm)) {
        ctx->servo_errors++;
        return 0;
    }
    if (!servo_hal_apply(&ctx->pwm)) {
        ctx->servo_errors++;
        return 0;
    }

    ctx->servo_writes++;
    return 1;
}
