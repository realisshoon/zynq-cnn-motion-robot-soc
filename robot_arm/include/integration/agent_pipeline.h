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
    HumanForearmTarget target;   /* Agent1 출력의 복사본. A2가 대표각으로 정규화하므로 원본은 건드리지 않는다. */
    uint8_t target_ready;      /* 이번 프레임에 Agent1이 valid 타겟을 냈는지 */
    ForearmJointCommand command;      /* Agent2가 마지막으로 승인해서 set_target한 명령 */
    uint8_t command_valid;

    /* 틱 경로 */
    ForearmJointCommand output;       /* 이번 틱의 Agent2 출력 */
    /* PWM enabled면 마지막 HAL 성공값, disabled면 마지막 변환 성공값. */
    ServoPwmCommand pwm;
    uint8_t output_enabled;    /* 0: compute/trace only; robot PWM remains disabled */
    ForearmJointCommand agent3_command; /* 가장 최근 Agent3 전달 시도 명령(TK trace용) */
    uint8_t agent3_command_valid;
    uint32_t agent3_command_tick; /* 위 명령을 전달하려 한 실행 control step 번호 */
    ForearmJointCommand applied_command; /* HAL 적용까지 성공한 마지막 관절 명령 */
    uint8_t applied_command_valid;

    /* 디버그용 통계 */
    uint32_t frames_in;
    uint32_t targets_valid;
    uint32_t commands_accepted;
    uint32_t commands_rejected;
    uint32_t retargets;
    /* 실제로 실행한 Robot control step 횟수. missed timer tick은 platform이
     * overrun으로 버리므로 발생한 모든 20 ms timer tick의 개수는 아니다. */
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
int agent_pipeline_init_mode(AgentPipelineContext *ctx, int enable_robot_pwm);
int agent_pipeline_set_output_enabled(AgentPipelineContext *ctx, int enabled);

/* HumanPose2D -> HumanForearmTarget. Agent1이 valid 타겟을 냈으면 1. */
int agent1_run(AgentPipelineContext *ctx, const HumanPose2D *pose, float dt_sec);
int agent1_run_stereo(AgentPipelineContext *ctx, const HumanPose2D *image_pose,
                      const HumanPose3D *measured_pose, float dt_sec);

/* validate -> 대표각 정규화 -> 범위 밖 축 HOLD -> 안전검사 -> set_target. 승인했으면 1. */
int agent2_run(AgentPipelineContext *ctx);

/* LIVE/RECORD/ALIGN 제어 틱 1회: 전체 tick을 세고 램프를 한 틱 진행한다.
 * Direct PLAY는 Agent2를 우회하므로 Record/Replay controller가 같은 counter를 센다. */
int agent2_tick(AgentPipelineContext *ctx);

/* ForearmJointCommand -> PWM 변환 후 서보 레지스터에 적용. 성공 1. */
int agent3_run(AgentPipelineContext *ctx);

/* 명시적인 command를 PWM으로 변환한다. PWM enabled에서는 HAL 성공 후에만
 * ctx->pwm과 applied_command를 확정한다. PWM disabled에서는 HAL을 호출하지 않고
 * 변환된 PWM을 software/trace용 ctx->pwm에 저장하며 applied_command는 갱신하지
 * 않는다. Replay가 ctx->output을 덮어쓰지 않고 이 API를 사용한다. */
int agent3_apply_command(AgentPipelineContext *ctx,
                         const ForearmJointCommand *command);

/* TK trace가 이전 control step의 Agent3 command를 재사용하지 않게 한다. */
int agent3_command_is_current_tick(const AgentPipelineContext *ctx);

#endif /* INTEGRATION_AGENT_PIPELINE_H */
