#ifndef INTEGRATION_AGENT_PIPELINE_H
#define INTEGRATION_AGENT_PIPELINE_H

#include <stdint.h>

#include "common/robot_types.h"
#include "output_controller/servo_control.h"
#include "robot_calibration/motion_control.h"
#include "robot_calibration/robot_calibration.h"

/*
 * Agent1 -> Agent2 -> Agent3 연결용 얇은 wrapper.
 * 각 Agent의 내부 소스는 수정하지 않고 기존 공개 API만 호출한다.
 *
 * 시계가 두 개다.
 *  - 프레임 경로(가변 주기, 입력이 올 때): agent1_run -> agent2_run
 *  - 제어 틱 경로(고정 20ms):              agent2_tick -> agent3_run
 * Agent2의 속도제한(max_delta_deg)이 20ms 틱 기준이라서 프레임 도착과 분리한다.
 *
 * 반환값 규칙: "이 단계가 다음 단계에 넘길 유효한 결과를 냈으면 1, 아니면 0".
 */
typedef struct {
    /* Agent 상태 (프레임 간 유지) */
    HumanAngleUnwrapState unwrap;
    RobotMotionState motion;

    /* 프레임 경로 */
    HumanPose2D pose;
    float dt_sec;
    HumanJointTarget target;   /* Agent1 출력의 복사본. unwrap이 in-place로 고치므로 원본은 건드리지 않는다. */
    uint8_t target_ready;      /* 이번 프레임에 Agent1이 valid 타겟을 냈는지 */
    JointCommand command;      /* Agent2가 마지막으로 승인해서 set_target한 명령 */
    uint8_t command_valid;

    /* 틱 경로 */
    JointCommand output;       /* 이번 틱의 Agent2 출력 */
    ServoPwmCommand pwm;       /* 마지막으로 변환에 성공한 PWM 명령 */

    /* 디버그용 통계 */
    uint32_t frames_in;
    uint32_t targets_valid;
    uint32_t commands_accepted;
    uint32_t commands_rejected;
    uint32_t retargets;
    uint32_t ticks;
    uint32_t servo_writes;
    uint32_t servo_errors;
} AgentPipelineContext;

/*
 * 각 Agent 초기화 후 홈 자세로 부트스트랩하고 서보를 enable한다. 성공 0, 실패 -1.
 * platform_init()(servo_hal_init 포함)이 먼저 성공해 있어야 한다.
 */
int agent_pipeline_init(AgentPipelineContext *ctx);

/* HumanPose2D -> HumanJointTarget. Agent1이 valid 타겟을 냈으면 1. */
int agent1_run(AgentPipelineContext *ctx, const HumanPose2D *pose, float dt_sec);

/* validate -> unwrap -> apply -> set_target. 새 목표를 승인했으면 1. */
int agent2_run(AgentPipelineContext *ctx);

/* 제어 틱 1회: 램프를 한 틱 진행. 출력이 유효하면 1. */
int agent2_tick(AgentPipelineContext *ctx);

/* JointCommand -> PWM 변환 후 서보 레지스터에 적용. 성공 1. */
int agent3_run(AgentPipelineContext *ctx);

#endif /* INTEGRATION_AGENT_PIPELINE_H */
