#ifndef ROBOT_PIPELINE_H
#define ROBOT_PIPELINE_H

#include <stdint.h>

#include "common/robot_types.h"
#include "app/robot/control/servo_control.h"
#include "app/robot/control/forearm_motion_control.h"
#include "app/robot/control/forearm_calibration.h"

/* [TRACE] robot_target_update이 이번 프레임에 한 일. UART 로그(integration/telemetry.h)의 A2 줄이 읽는다. */
typedef enum {
    ROBOT_TARGET_NONE = 0,          /* 실행하지 않음(이번 프레임에 사람 목표 타겟이 없음) */
    ROBOT_TARGET_NEW,               /* 새 목표를 승인하고 재계획함 */
    ROBOT_TARGET_SAME,               /* 직전과 같은 명령이라 재계획하지 않음 */
    ROBOT_TARGET_REJECT_VALIDATE,   /* 입력 검증 실패 */
    ROBOT_TARGET_REJECT_SAFETY      /* 안전검사 거부 */
} RobotTargetResult;

typedef enum {
    ROBOT_OUTPUT_RESULT_NONE = 0,
    ROBOT_OUTPUT_RESULT_VIRTUAL_ACTIVE,
    ROBOT_OUTPUT_RESULT_ENABLED,
    ROBOT_OUTPUT_RESULT_DISABLED,
    ROBOT_OUTPUT_RESULT_REFERENCE_INVALID,
    ROBOT_OUTPUT_RESULT_REFERENCE_LIMITS,
    ROBOT_OUTPUT_RESULT_REFERENCE_UNSAFE,
    ROBOT_OUTPUT_RESULT_REFERENCE_STATE,
    ROBOT_OUTPUT_RESULT_CONVERSION_FAILED,
    ROBOT_OUTPUT_RESULT_HAL_APPLY_FAILED,
    ROBOT_OUTPUT_RESULT_HAL_ENABLE_FAILED,
    ROBOT_OUTPUT_RESULT_HAL_DISABLE_FAILED,
    ROBOT_OUTPUT_RESULT_HAL_RECOVERY_DISABLE_FAILED
} RobotOutputResult;

#define GRIPPER_CONTROL_CONFIRM_SAMPLES 3U
#define GRIPPER_CONTROL_SAMPLE_MAX_GAP_US 250000U
#define GRIPPER_CONTROL_CLOSE_HOLD_US 1500000U
#define GRIPPER_CONTROL_CLOSE_MAX_WAIT_US 2000000U
#define GRIPPER_CONTROL_CLOSE_START_PX 18.0f
#define GRIPPER_CONTROL_CANCEL_PX 20.0f
#define GRIPPER_CONTROL_CANCEL_SAMPLES 2U
#define GRIPPER_CONTROL_CANDIDATE_WINDOW_US 600000U
#define GRIPPER_CONTROL_STRONG_OPEN_NORM 0.8f
#define GRIPPER_CONTROL_STRONG_OPEN_US 800000U
#define GRIPPER_CONTROL_OPEN_MISSING_GRACE_US 200000U

typedef struct {
    float candidate;
    uint32_t time_us, candidate_time_us, frame_id;
    uint8_t seen, outside, pending, ramping;
} ElbowReentryState;

typedef struct {
    float goal;
    uint32_t time_us;
    uint8_t valid, held, pending_epoch, active;
} ElbowReturnState;

typedef struct {
    float goal, candidate;
    uint32_t time_us, candidate_us, candidate_frame, last_fresh_us, last_fresh_frame;
    uint32_t candidate_epoch;
    uint8_t valid, held, outside, active, pending, have_candidate, have_fresh, need_two;
} WristReturnState;

typedef struct {
    uint32_t frame_id;
    uint32_t sample_time_us;
    uint32_t confirmed_time_us;
    uint32_t applied_time_us;
    uint32_t candidate_started_us;
    uint8_t candidate_active;
    uint8_t cancel_count;
    uint8_t have_frame;
    uint8_t source;
    uint8_t close_count;
    uint8_t open_count;
    uint8_t latched;
    uint8_t closed_applied;
} GripperLatchState;

typedef struct {
    uint32_t started_us, fresh_us, frame_id;
    uint8_t pending, have_frame, source, count;
} GripperOpenConfirmState;

typedef struct {
    float angles[7][4], speed;
    uint32_t times[7], low_since_us, frame_id, fresh_us;
    uint8_t samples, speed_valid, moving, low_valid;
    uint8_t armed, locked, open_count, have_frame, source, manual_open;
    GripperOpenConfirmState strong_open;
} GripperMotionHoldState;

/*
 * 시계가 두 개다.
 *  - 프레임 경로(가변 주기, 입력이 올 때): human_target_update -> robot_motion_set_target
 *  - 제어 틱 경로(고정 20ms):              robot_motion_step -> servo_output_update
 * 속도·가속도 제한은 20ms 틱 기준이므로 프레임 도착과 분리한다.
 *
 * 반환값 규칙: "이 단계가 다음 단계에 넘길 유효한 결과를 냈으면 1, 아니면 0".
 */
typedef struct {
    /* 제어 상태 (프레임 간 유지) */
    ForearmAngleUnwrapState unwrap;
    ForearmMotionState motion;
    GripperLatchState gripper_latch;
    GripperMotionHoldState gripper_motion_hold;
    ElbowReentryState elbow_reentry;
    ElbowReturnState elbow_return;
    WristReturnState wrist_return[2];
    uint8_t wrist_observation_fresh;
    uint8_t gripper_independent;
    uint8_t arm_stationary, arm_tracking_started;
    uint32_t arm_input_epoch;
    uint8_t gripper_fresh;
    float gripper_desired;
    float gripper_distance_px;

    /* 프레임 경로 */
    HumanPose2D pose;
    float dt_sec;
    HumanForearmTarget target;   /* 사람 목표 출력의 복사본. A2가 대표각으로 정규화하므로 원본은 건드리지 않는다. */
    uint8_t target_ready;      /* 이번 프레임에 사람 목표이 valid 타겟을 냈는지 */
    ForearmJointCommand command;      /* 운동 제어가 마지막으로 승인해서 set_target한 명령 */
    uint8_t command_valid;

    /* 틱 경로 */
    ForearmJointCommand output;       /* 이번 틱의 운동 제어 출력 */
    /* PWM enabled면 마지막 HAL 성공값, disabled면 마지막 변환 성공값. */
    ServoPwmCommand pwm;
    uint8_t output_enabled;    /* 0: compute/trace only; robot PWM remains disabled */
    uint8_t virtual_compute;
    uint32_t virtual_session, virtual_last_session, virtual_renewed_us;
    uint8_t output_parked;
    uint8_t output_faulted;
    ForearmJointCommand servo_command; /* 가장 최근 Agent3 전달 시도 명령(TK trace용) */
    uint8_t servo_command_valid;
    uint32_t servo_command_tick; /* 위 명령을 전달하려 한 실행 control step 번호 */
    ForearmJointCommand applied_command; /* HAL 적용까지 성공한 마지막 관절 명령 */
    uint8_t applied_command_valid;
    RobotOutputResult output_result;

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

    /* [TRACE] UART 로그(integration/telemetry.h)용 기록. 파이프라인 동작에는 쓰지 않는다. */
    int8_t pose_result;              /* forearm_target_run 반환값: 1 새 타겟, 0 HOLD, -1 타겟 없음 */
    uint8_t target_result;         /* RobotTargetResult: 이번 프레임의 robot_motion_set_target 결과 */
    ForearmJointCommand mapped_target;    /* 이번 프레임에 매핑된 명령. 안전검사에서 거부되면 valid=0 */
} RobotPipeline;

/*
 * Bootstrap home commands with physical output disabled. Success 0, failure -1.
 * platform_init()(servo_hal_init 포함)이 먼저 성공해 있어야 한다.
 */
int robot_motion_init(RobotPipeline *ctx);
int servo_output_set_enabled(RobotPipeline *ctx, int enabled);
#define ROBOT_VIRTUAL_LEASE_US 2000000U
int robot_motion_calculation_enabled(const RobotPipeline *ctx);
/* op 1=new session, 2=renew, 0=end; each request carries a nonzero nonce. */
int robot_motion_virtual_request(RobotPipeline *ctx, unsigned op,
                                   uint32_t session, uint32_t now_us);
int robot_motion_virtual_service(RobotPipeline *ctx, uint32_t now_us);
void gripper_control_reset(RobotPipeline *ctx);
const char *servo_output_result_name(const RobotPipeline *ctx);

/* HumanPose2D -> HumanForearmTarget. 사람 목표이 valid 타겟을 냈으면 1. */
int human_target_update(RobotPipeline *ctx, const HumanPose2D *pose, float dt_sec);
int human_target_update_stereo(RobotPipeline *ctx, const HumanPose2D *image_pose,
                      const HumanPose3D *measured_pose, float dt_sec);

/* validate -> 대표각 정규화 -> 범위 밖 축 HOLD -> 안전검사 -> set_target. 승인했으면 1. */
int robot_motion_set_target(RobotPipeline *ctx);
void robot_motion_sync_input(RobotPipeline *ctx, int stationary, uint32_t input_epoch);
int gripper_control_update(RobotPipeline *ctx, const HumanPose2D *pose);
int gripper_control_set_manual(RobotPipeline *ctx, int open);

/* LIVE/RECORD/ALIGN 제어 틱 1회: 전체 tick을 세고 램프를 한 틱 진행한다.
 * Direct PLAY는 운동 제어를 우회하므로 Record/Replay controller가 같은 counter를 센다. */
int robot_motion_step(RobotPipeline *ctx);

/* ForearmJointCommand -> PWM 변환 후 서보 레지스터에 적용. 성공 1. */
int servo_output_update(RobotPipeline *ctx);

/* 명시적인 command를 PWM으로 변환한다. PWM enabled에서는 HAL 성공 후에만
 * ctx->pwm과 applied_command를 확정한다. PWM disabled에서는 HAL을 호출하지 않고
 * 변환된 PWM을 software/trace용 ctx->pwm에 저장하며 applied_command는 갱신하지
 * 않는다. Replay가 ctx->output을 덮어쓰지 않고 이 API를 사용한다. */
int servo_output_apply(RobotPipeline *ctx,
                         const ForearmJointCommand *command);

/* TK trace가 이전 control step의 Agent3 command를 재사용하지 않게 한다. */
int servo_output_is_current_tick(const RobotPipeline *ctx);

#endif /* ROBOT_PIPELINE_H */
