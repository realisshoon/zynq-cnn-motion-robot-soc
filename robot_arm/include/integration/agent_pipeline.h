#ifndef INTEGRATION_AGENT_PIPELINE_H
#define INTEGRATION_AGENT_PIPELINE_H

#include <stdint.h>

#include "common/robot_types.h"
#include "output_controller/servo_control.h"
#include "robot_calibration/forearm_motion_control.h"
#include "robot_calibration/forearm_calibration.h"

#ifdef ROBOT_TRACE
/* [TRACE] agent2_run이 이번 프레임에 한 일. UART 로그(integration/trace.h)의 A2 줄이 읽는다. */
typedef enum {
    A2_RESULT_NONE = 0,          /* 실행하지 않음(이번 프레임에 Agent1 타겟이 없음) */
    A2_RESULT_NEW,               /* 새 목표를 승인하고 재계획함 */
    A2_RESULT_SAME,               /* 직전과 같은 명령이라 재계획하지 않음 */
    A2_RESULT_REJECT_VALIDATE,   /* 입력 검증 실패 */
    A2_RESULT_REJECT_SAFETY      /* 안전검사 거부 */
} Agent2Result;
#endif

/*
 * 2026-09-22: Agent1(agent1_forearm_stage_*)/Agent3(ForearmJointCommand PWM
 * 변환)가 새 5축(팔꿈치부터 시작하는 수평 설치)으로 전환하면서, Agent3의
 * output_control_update()/servo_control_convert()가 legacy JointCommand를
 * 더 이상 받지 않는다(ServoPwmCommand도 5채널로 교체됨). 그래서 이 파이프라인
 * 전체를 legacy HumanJointTarget/JointCommand에서 새 HumanForearmTarget/
 * ForearmJointCommand로 옮긴다 -- 이제 legacy 경로는 Agent3까지 갈 수 없어
 * 병행 유지가 불가능하다. Agent1/Agent2/Agent3 각 내부 소스는 수정하지 않고
 * 기존 공개 API만 호출한다.
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
    ForearmAngleUnwrapState unwrap;
    ForearmMotionState motion;

    /* 프레임 경로 */
    HumanPose2D pose;
    float dt_sec;
    HumanForearmTarget target;   /* Agent1 출력의 복사본. unwrap이 in-place로 고치므로 원본은 건드리지 않는다. */
    uint8_t target_ready;      /* 이번 프레임에 Agent1이 valid 타겟을 냈는지 */
    ForearmJointCommand command;      /* Agent2가 마지막으로 승인해서 set_target한 명령 */
    uint8_t command_valid;

    /* 틱 경로 */
    ForearmJointCommand output;       /* 이번 틱의 Agent2 출력 */
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

#ifdef ROBOT_TRACE
    /* [TRACE] UART 로그(integration/trace.h)용 기록. 파이프라인 동작에는 쓰지 않는다. */
    int8_t a1_rc;              /* agent1_forearm_stage_run 반환값: 1 새 타겟, 0 HOLD, -1 타겟 없음 */
    uint8_t a2_result;         /* Agent2Result: 이번 프레임의 agent2_run 결과 */
    ForearmJointCommand a2_mapped;    /* 이번 프레임에 매핑된 명령. 안전검사에서 거부되면 valid=0 */
#endif
} AgentPipelineContext;

/*
 * 각 Agent 초기화 후 홈 자세로 부트스트랩하고 서보를 enable한다. 성공 0, 실패 -1.
 * platform_init()(servo_hal_init 포함)이 먼저 성공해 있어야 한다.
 */
int agent_pipeline_init(AgentPipelineContext *ctx);

/* HumanPose2D -> HumanForearmTarget. Agent1이 valid 타겟을 냈으면 1. */
int agent1_run(AgentPipelineContext *ctx, const HumanPose2D *pose, float dt_sec);

/* validate -> unwrap -> apply -> set_target. 새 목표를 승인했으면 1. */
int agent2_run(AgentPipelineContext *ctx);

/* 제어 틱 1회: 램프를 한 틱 진행. 출력이 유효하면 1. */
int agent2_tick(AgentPipelineContext *ctx);

/* ForearmJointCommand -> PWM 변환 후 서보 레지스터에 적용. 성공 1. */
int agent3_run(AgentPipelineContext *ctx);

#endif /* INTEGRATION_AGENT_PIPELINE_H */
