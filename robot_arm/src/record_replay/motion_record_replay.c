#include "record_replay/motion_record_replay.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "robot_calibration/forearm_calibration_config.h"
#include "robot_calibration/forearm_safety_check.h"

#define MOTION_RR_DELTA_TOLERANCE_DEG 0.0001f
#define MOTION_RR_ALIGN_POSITION_TOLERANCE_DEG 0.05f
#define MOTION_RR_ALIGN_VELOCITY_TOLERANCE_DEG_S 0.05
#define MOTION_RR_ALIGN_GRIPPER_TOLERANCE 0.0001f

/*
 * 녹화 중인 데이터와 재생 중인 데이터를 분리한 선형 버퍼.
 * 전역 정적 배열이므로 stack/heap을 쓰지 않고 linker가 .bss 주소를 정한다.
 * 고정 물리 주소는 없으며 단일 MotionRecordReplay controller 사용을 전제로 한다.
 */
static MotionSample record_buffer[MOTION_RECORD_REPLAY_MAX_SAMPLES];
static MotionSample replay_buffer[MOTION_RECORD_REPLAY_MAX_SAMPLES];

typedef char motion_sample_must_be_five_floats[
    sizeof(MotionSample) == 5U * sizeof(float) ? 1 : -1];

/* pipeline command에서 저장 형식의 다섯 값만 추출한다. */
static MotionSample command_to_sample(const ForearmJointCommand *command)
{
    MotionSample sample;
    sample.elbow_roll_deg = command->elbow_roll_deg;
    sample.elbow_pitch_deg = command->elbow_pitch_deg;
    sample.wrist_pitch_deg = command->wrist_pitch_deg;
    sample.wrist_roll_deg = command->wrist_roll_deg;
    sample.gripper_norm = command->gripper_norm;
    return sample;
}

/* 저장 sample을 Agent3가 받을 수 있는 유효 command로 복원한다. */
static ForearmJointCommand sample_to_command(const MotionSample *sample)
{
    ForearmJointCommand command;
    command.elbow_roll_deg = sample->elbow_roll_deg;
    command.elbow_pitch_deg = sample->elbow_pitch_deg;
    command.wrist_pitch_deg = sample->wrist_pitch_deg;
    command.wrist_roll_deg = sample->wrist_roll_deg;
    command.gripper_norm = sample->gripper_norm;
    command.valid = 1U;
    return command;
}

/* NaN/Inf와 valid=0을 가장 먼저 걸러 후속 비교와 FK 계산을 보호한다. */
static int finite_command(const ForearmJointCommand *command)
{
    return command != NULL && command->valid != 0U &&
           isfinite(command->elbow_roll_deg) &&
           isfinite(command->elbow_pitch_deg) &&
           isfinite(command->wrist_pitch_deg) &&
           isfinite(command->wrist_roll_deg) &&
           isfinite(command->gripper_norm);
}

static int within(float value, const JointCalibration *calibration)
{
    return value >= calibration->min_deg && value <= calibration->max_deg;
}

/*
 * Replay command 한 개의 정적 안전성 검사.
 * 검사 순서는 finite/valid → 관절·gripper 범위 → 3D FK safety다.
 */
static MotionRecordReplayReason validate_command(const ForearmJointCommand *command)
{
    if (!finite_command(command)) return MOTION_RR_REASON_INVALID_COMMAND;
    if (!within(command->elbow_roll_deg, &forearm_calibration_config.elbow_roll) ||
        !within(command->elbow_pitch_deg, &forearm_calibration_config.elbow_pitch) ||
        !within(command->wrist_pitch_deg, &forearm_calibration_config.wrist_pitch) ||
        !within(command->wrist_roll_deg, &forearm_calibration_config.wrist_roll) ||
        command->gripper_norm < 0.0f || command->gripper_norm > 1.0f) {
        return MOTION_RR_REASON_RANGE;
    }
    if (!forearm_safety_check_apply(command, NULL)) return MOTION_RR_REASON_SAFETY;
    return MOTION_RR_REASON_NONE;
}

static int delta_ok(float previous, float current, float maximum)
{
    return fabsf(current - previous) <= maximum + MOTION_RR_DELTA_TOLERANCE_DEG;
}

/*
 * 연속 두 sample 사이의 회전 4축 변화량 검사.
 * gripper는 회전축 delta 규칙을 적용하지 않고 ALIGN에서 별도 rate limit한다.
 */
static MotionRecordReplayReason validate_delta(const ForearmJointCommand *previous,
                                               const ForearmJointCommand *current)
{
    if (!delta_ok(previous->elbow_roll_deg, current->elbow_roll_deg,
                  forearm_calibration_config.elbow_roll.max_delta_deg) ||
        !delta_ok(previous->elbow_pitch_deg, current->elbow_pitch_deg,
                  forearm_calibration_config.elbow_pitch.max_delta_deg) ||
        !delta_ok(previous->wrist_pitch_deg, current->wrist_pitch_deg,
                  forearm_calibration_config.wrist_pitch.max_delta_deg) ||
        !delta_ok(previous->wrist_roll_deg, current->wrist_roll_deg,
                  forearm_calibration_config.wrist_roll.max_delta_deg)) {
        return MOTION_RR_REASON_DELTA;
    }
    return MOTION_RR_REASON_NONE;
}

/*
 * PLAY/ALIGN을 시작하기 전에 replay_buffer 전체를 선검사한다.
 * 뒤쪽 sample 하나라도 잘못됐으면 로봇을 Sample0으로 움직이기 전에 거부한다.
 * PLAY 중에도 같은 검사를 다시 수행하여 메모리 훼손 같은 실행 중 오류를 막는다.
 */
static MotionRecordReplayReason preflight_replay(uint32_t count)
{
    ForearmJointCommand previous;
    uint32_t i;

    for (i = 0U; i < count; ++i) {
        ForearmJointCommand current = sample_to_command(&replay_buffer[i]);
        MotionRecordReplayReason reason = validate_command(&current);
        if (reason != MOTION_RR_REASON_NONE) return reason;
        if (i > 0U) {
            reason = validate_delta(&previous, &current);
            if (reason != MOTION_RR_REASON_NONE) return reason;
        }
        previous = current;
    }
    return MOTION_RR_REASON_NONE;
}

/* current를 target 쪽으로 maximum_delta 이하만 이동시키는 선형 rate limit. */
static float approach(float current, float target, float maximum_delta)
{
    float difference = target - current;
    if (fabsf(difference) <= maximum_delta) return target;
    return current + (difference > 0.0f ? maximum_delta : -maximum_delta);
}

/* 회전 4축의 위치 오차와 속도가 모두 충분히 작을 때 ALIGN 완료로 본다. */
static int rotary_align_complete(const AgentPipelineContext *pipeline,
                                 const ForearmJointCommand *target)
{
    unsigned i;
    if (fabsf(pipeline->output.elbow_roll_deg - target->elbow_roll_deg) >
        MOTION_RR_ALIGN_POSITION_TOLERANCE_DEG) return 0;
    if (fabsf(pipeline->output.elbow_pitch_deg - target->elbow_pitch_deg) >
        MOTION_RR_ALIGN_POSITION_TOLERANCE_DEG) return 0;
    if (fabsf(pipeline->output.wrist_pitch_deg - target->wrist_pitch_deg) >
        MOTION_RR_ALIGN_POSITION_TOLERANCE_DEG) return 0;
    if (fabsf(pipeline->output.wrist_roll_deg - target->wrist_roll_deg) >
        MOTION_RR_ALIGN_POSITION_TOLERANCE_DEG) return 0;
    for (i = 0U; i < FOREARM_MOTION_JOINT_COUNT; ++i) {
        if (fabs(pipeline->motion.axes[i].v) >
            MOTION_RR_ALIGN_VELOCITY_TOLERANCE_DEG_S) return 0;
    }
    return 1;
}

/*
 * Motion 내부 q를 실제 마지막 명령 자세로 다시 만든다.
 * state_init 뒤 command를 최초 target으로 넣으므로 q=command, v=0에서 시작한다.
 * Direct PLAY가 Motion state를 진행하지 않기 때문에 loop 복귀 전에 반드시 필요하다.
 */
static void reseed_motion(AgentPipelineContext *pipeline,
                          const ForearmJointCommand *command)
{
    forearm_calibration_state_init(&pipeline->motion);
    forearm_calibration_set_target(&pipeline->motion, command);
    pipeline->output = *command;
}

/*
 * Replay를 끄고 실시간 추종으로 복귀하는 공통 처리.
 * 이전 카메라 target과 unwrap 기준을 지워서 Replay 중 쌓인 frame이 즉시 실행되지
 * 않게 하고, 다음 fresh camera frame이 들어왔을 때 새 target으로 다시 계획한다.
 */
static void resync_live(MotionRecordReplay *controller,
                        AgentPipelineContext *pipeline,
                        MotionRecordReplayReason reason)
{
    ForearmJointCommand command = controller->last_applied_replay_command;

    reseed_motion(pipeline, &command);
    memset(&pipeline->command, 0, sizeof(pipeline->command));
    pipeline->command_valid = 0U;
    forearm_motion_control_unwrap_state_init(&pipeline->unwrap);
    pipeline->target_ready = 0U;

    controller->mode = MOTION_RR_LIVE;
    controller->reason = reason;
}

/* Replay 오류를 명시적인 HOLDING 상태로 고정하고 호출자에게 실패를 반환한다. */
static int enter_holding(MotionRecordReplay *controller,
                         MotionRecordReplayReason reason)
{
    controller->mode = MOTION_RR_HOLDING;
    controller->reason = reason;
    return 0;
}

/* PWM-disabled 통합 빌드에서는 Agent3 변환 성공과 실제 HAL 적용을 구분한다. */
static void remember_agent3_success(MotionRecordReplay *controller,
                                    const AgentPipelineContext *pipeline,
                                    const ForearmJointCommand *command)
{
    controller->last_applied_replay_command = *command;
    controller->last_applied_replay_valid = 1U;
    controller->last_applied_replay_source = pipeline->output_enabled != 0U
        ? MOTION_RR_APPLIED_HAL
        : MOTION_RR_APPLIED_SOFTWARE_OUTPUT;
}

void motion_record_replay_init(MotionRecordReplay *controller)
{
    if (controller == NULL) return;
    memset(controller, 0, sizeof(*controller));
    controller->mode = MOTION_RR_LIVE;
}

int motion_record_replay_configure_align(MotionRecordReplay *controller,
                                         float gripper_max_delta_norm_per_tick,
                                         uint32_t timeout_ticks)
{
    if (controller == NULL || !isfinite(gripper_max_delta_norm_per_tick) ||
        gripper_max_delta_norm_per_tick <= 0.0f ||
        gripper_max_delta_norm_per_tick > 1.0f || timeout_ticks == 0U) return 0;
    if (controller->mode == MOTION_RR_ALIGNING ||
        controller->mode == MOTION_RR_PLAYING) return 0;
    controller->align_gripper_max_delta_norm = gripper_max_delta_norm_per_tick;
    controller->align_timeout_ticks = timeout_ticks;
    return 1;
}

int motion_record_replay_start_record(MotionRecordReplay *controller)
{
    if (controller == NULL) return 0;
    if (controller->mode != MOTION_RR_LIVE) {
        controller->reason = MOTION_RR_REASON_BUSY;
        return 0;
    }
    controller->record_count = 0U;
    controller->record_tick_baseline_valid = 0U;
    controller->reason = MOTION_RR_REASON_NONE;
    controller->mode = MOTION_RR_RECORDING;
    return 1;
}

int motion_record_replay_stop_record(MotionRecordReplay *controller)
{
    if (controller == NULL || controller->mode != MOTION_RR_RECORDING) return 0;
    controller->mode = MOTION_RR_LIVE;
    controller->reason = MOTION_RR_REASON_STOPPED;
    return 1;
}

int motion_record_replay_copy_record_to_replay(MotionRecordReplay *controller)
{
    if (controller == NULL || controller->mode != MOTION_RR_LIVE ||
        controller->record_count == 0U) return 0;
    memcpy(replay_buffer, record_buffer,
           controller->record_count * sizeof(record_buffer[0]));
    controller->replay_count = controller->record_count;
    controller->replay_index = 0U;
    controller->reason = MOTION_RR_REASON_NONE;
    return 1;
}

int motion_record_replay_load_replay(MotionRecordReplay *controller,
                                     const MotionSample *samples,
                                     uint32_t count)
{
    if (controller == NULL || samples == NULL || count == 0U ||
        count > MOTION_RECORD_REPLAY_MAX_SAMPLES ||
        controller->mode != MOTION_RR_LIVE) return 0;
    memcpy(replay_buffer, samples, count * sizeof(replay_buffer[0]));
    controller->replay_count = count;
    controller->replay_index = 0U;
    controller->reason = MOTION_RR_REASON_NONE;
    return 1;
}

int motion_record_replay_start_play(MotionRecordReplay *controller,
                                    AgentPipelineContext *pipeline,
                                    uint32_t tick_overrun_count)
{
    ForearmJointCommand sample0;
    MotionRecordReplayReason reason;

    if (controller == NULL || pipeline == NULL) return 0;
    if (controller->mode != MOTION_RR_LIVE) {
        controller->reason = MOTION_RR_REASON_BUSY;
        return 0;
    }
    if (controller->replay_count == 0U) {
        controller->reason = MOTION_RR_REASON_EMPTY;
        return 0;
    }
    if (controller->align_gripper_max_delta_norm <= 0.0f ||
        controller->align_timeout_ticks == 0U) {
        controller->reason = MOTION_RR_REASON_CONFIG;
        return 0;
    }

    reason = preflight_replay(controller->replay_count);
    if (reason != MOTION_RR_REASON_NONE) {
        controller->reason = reason;
        return 0;
    }
    sample0 = sample_to_command(&replay_buffer[0]);

    if (pipeline->applied_command_valid != 0U &&
        validate_command(&pipeline->applied_command) == MOTION_RR_REASON_NONE) {
        controller->last_applied_replay_command = pipeline->applied_command;
        controller->last_applied_replay_valid = 1U;
        controller->last_applied_replay_source = MOTION_RR_APPLIED_HAL;
        controller->align_gripper_norm = pipeline->applied_command.gripper_norm;
    } else if (validate_command(&pipeline->output) == MOTION_RR_REASON_NONE) {
        /* 현재 dev/robot은 boot command를 HAL에 적용하므로 정상 통합에서는
         * 위 applied_command 경로를 사용한다. 이 분기는 구형/host context를
         * 위한 호환 fallback이며 실제 HAL write가 증명된 자세가 아니다.
         * 향후 output_enabled/PWM-disabled 정책에서도 이 구분을 유지해야 한다. */
        controller->last_applied_replay_command = pipeline->output;
        controller->last_applied_replay_valid = 1U;
        controller->last_applied_replay_source =
            MOTION_RR_APPLIED_SOFTWARE_OUTPUT;
        controller->align_gripper_norm = pipeline->output.gripper_norm;
    } else {
        controller->reason = MOTION_RR_REASON_INVALID_COMMAND;
        return 0;
    }

    controller->align_target = sample0;
    controller->align_ticks = 0U;
    controller->replay_index = 0U;
    controller->observed_tick_overruns = tick_overrun_count;
    controller->reason = MOTION_RR_REASON_NONE;
    controller->mode = MOTION_RR_ALIGNING;
    forearm_calibration_set_target(&pipeline->motion, &sample0);
    pipeline->target_ready = 0U;
    return 1;
}

/*
 * LIVE와 RECORD의 공통 control step.
 * RECORD hook은 반드시 Agent2 출력 생성/검증 뒤, Agent3 적용 전에 실행된다.
 * timestamp가 없으므로 overrun이나 Agent2 실패로 한 sample이라도 빠질 상황이면
 * 녹화를 종료하여 배열 index=20 ms라는 시간축 계약을 보존한다.
 */
static int live_or_record_tick(MotionRecordReplay *controller,
                               AgentPipelineContext *pipeline,
                               uint32_t tick_overrun_count)
{
    MotionRecordReplayReason reason;
    int recording = controller->mode == MOTION_RR_RECORDING;

    if (!agent2_tick(pipeline)) {
        if (recording) {
            controller->mode = MOTION_RR_LIVE;
            controller->reason = MOTION_RR_REASON_INVALID_COMMAND;
        }
        return 0;
    }
    reason = validate_command(&pipeline->output);
    if (reason != MOTION_RR_REASON_NONE) {
        if (recording) {
            controller->mode = MOTION_RR_LIVE;
        }
        controller->reason = reason;
        return 0;
    }

    if (recording) {
        if (controller->record_tick_baseline_valid == 0U) {
            controller->record_tick_overrun_baseline = tick_overrun_count;
            controller->record_tick_baseline_valid = 1U;
        } else if (tick_overrun_count !=
                   controller->record_tick_overrun_baseline) {
            /* 누락된 20 ms 뒤 sample을 추가하면 Replay 시간이 압축된다.
             * sample은 저장하지 않되 이번 LIVE 제어 출력은 Agent3까지 전달한다. */
            controller->mode = MOTION_RR_LIVE;
            controller->reason = MOTION_RR_REASON_TICK_OVERRUN;
            return agent3_apply_command(pipeline, &pipeline->output);
        }
        if (controller->record_count < MOTION_RECORD_REPLAY_MAX_SAMPLES) {
            record_buffer[controller->record_count++] =
                command_to_sample(&pipeline->output);
            if (controller->record_count == MOTION_RECORD_REPLAY_MAX_SAMPLES) {
                controller->mode = MOTION_RR_LIVE;
                controller->reason = MOTION_RR_REASON_BUFFER_FULL;
            }
        } else {
            controller->mode = MOTION_RR_LIVE;
            controller->reason = MOTION_RR_REASON_BUFFER_FULL;
        }
    }

    /* 녹화 대상은 Agent2가 생성한 trajectory다. append 뒤 HAL 적용이 실패해도
     * 이미 기록한 sample을 되돌리지 않는 기존 계약을 유지한다. */
    return agent3_apply_command(pipeline, &pipeline->output);
}

/*
 * 현재 실제 자세에서 Replay Sample0으로 안전하게 이동하는 20 ms step.
 * 회전 4축은 기존 Motion 속도/가속도 제한을 사용하고, gripper는 별도의
 * align_gripper_max_delta_norm으로 제한한다. 실패하면 자동 재시작하지 않고 HOLD한다.
 */
static int align_tick(MotionRecordReplay *controller,
                      AgentPipelineContext *pipeline,
                      uint32_t tick_overrun_count)
{
    ForearmJointCommand command;
    MotionRecordReplayReason reason;

    if (tick_overrun_count != controller->observed_tick_overruns) {
        /* 이 함수 호출 자체는 실행된 control step 한 번이므로 ticks에 센다.
         * platform에서 버린 timer event 개수는 ticks에 추가하지 않는다. */
        ++pipeline->ticks;
        return enter_holding(controller, MOTION_RR_REASON_TICK_OVERRUN);
    }
    ++controller->align_ticks;
    if (!agent2_tick(pipeline)) {
        return enter_holding(controller, MOTION_RR_REASON_INVALID_COMMAND);
    }
    if (controller->align_ticks > controller->align_timeout_ticks) {
        return enter_holding(controller, MOTION_RR_REASON_ALIGN_TIMEOUT);
    }
    if (pipeline->motion.held != 0) {
        return enter_holding(controller, MOTION_RR_REASON_SAFETY);
    }

    controller->align_gripper_norm = approach(
        controller->align_gripper_norm,
        controller->align_target.gripper_norm,
        controller->align_gripper_max_delta_norm);
    command = pipeline->output;
    command.gripper_norm = controller->align_gripper_norm;
    reason = validate_command(&command);
    if (reason != MOTION_RR_REASON_NONE) {
        return enter_holding(controller, reason);
    }
    if (!agent3_apply_command(pipeline, &command)) {
        return enter_holding(controller, MOTION_RR_REASON_AGENT3_FAILURE);
    }
    remember_agent3_success(controller, pipeline, &command);

    if (rotary_align_complete(pipeline, &controller->align_target) &&
        fabsf(controller->align_gripper_norm -
              controller->align_target.gripper_norm) <=
            MOTION_RR_ALIGN_GRIPPER_TOLERANCE) {
        controller->mode = MOTION_RR_PLAYING;
        controller->replay_index = 0U;
        controller->observed_tick_overruns = tick_overrun_count;
    }
    return 1;
}

/*
 * replay_buffer 한 sample을 Agent3에 직접 적용하는 20 ms step.
 * Agent2 Motion을 다시 거치지 않아 기록된 50 Hz trajectory를 그대로 재생한다.
 */
static int play_tick(MotionRecordReplay *controller,
                     AgentPipelineContext *pipeline,
                     uint32_t tick_overrun_count)
{
    ForearmJointCommand command;
    MotionRecordReplayReason reason;

    /* ctx->ticks는 실제 실행한 20 ms control step 수다. Direct PLAY는
     * agent2_tick()을 우회하므로 이 경로에서 정확히 한 번 증가시킨다. */
    ++pipeline->ticks;
    if (tick_overrun_count != controller->observed_tick_overruns) {
        return enter_holding(controller, MOTION_RR_REASON_TICK_OVERRUN);
    }
    if (controller->replay_index >= controller->replay_count) {
        return enter_holding(controller, MOTION_RR_REASON_EOF);
    }

    command = sample_to_command(&replay_buffer[controller->replay_index]);
    reason = validate_command(&command);
    if (reason == MOTION_RR_REASON_NONE && controller->replay_index > 0U) {
        ForearmJointCommand previous =
            sample_to_command(&replay_buffer[controller->replay_index - 1U]);
        reason = validate_delta(&previous, &command);
    }
    if (reason != MOTION_RR_REASON_NONE) {
        return enter_holding(controller, reason);
    }
    if (!agent3_apply_command(pipeline, &command)) {
        return enter_holding(controller, MOTION_RR_REASON_AGENT3_FAILURE);
    }

    remember_agent3_success(controller, pipeline, &command);
    ++controller->replay_index;
    if (controller->replay_index == controller->replay_count) {
        ForearmJointCommand sample0 = sample_to_command(&replay_buffer[0]);

        /* Direct PLAY 동안 Motion 내부 상태는 이전 Sample0 부근에 남아 있다.
         * 실제 마지막 HAL 성공 자세로 q/v를 먼저 reseed하고 Sample0을 target으로
         * 설정해야 마지막→Sample0 경계를 20 ms에 점프하지 않고 ALIGN할 수 있다. */
        reseed_motion(pipeline, &controller->last_applied_replay_command);
        forearm_calibration_set_target(&pipeline->motion, &sample0);
        controller->align_target = sample0;
        controller->align_gripper_norm =
            controller->last_applied_replay_command.gripper_norm;
        controller->align_ticks = 0U;
        controller->observed_tick_overruns = tick_overrun_count;
        controller->reason = MOTION_RR_REASON_NONE;
        controller->mode = MOTION_RR_ALIGNING;
        pipeline->target_ready = 0U;
    }
    return 1;
}

/* 오류 뒤 마지막 Agent3 성공 command를 매 20 ms 다시 적용해 자세를 유지한다. */
static int holding_tick(MotionRecordReplay *controller,
                        AgentPipelineContext *pipeline)
{
    ++pipeline->ticks;
    if (controller->last_applied_replay_valid == 0U) return 0;
    /* 동일 command 재적용까지 실패해도 최초 HOLD 원인은 보존한다.
     * 추가 HAL 실패 횟수는 Agent3의 servo_errors에 별도로 기록된다. */
    return agent3_apply_command(
        pipeline, &controller->last_applied_replay_command);
}

int motion_record_replay_control_tick(MotionRecordReplay *controller,
                                      AgentPipelineContext *pipeline,
                                      uint32_t tick_overrun_count)
{
    if (controller == NULL || pipeline == NULL) return 0;
    switch (controller->mode) {
        case MOTION_RR_LIVE:
        case MOTION_RR_RECORDING:
            return live_or_record_tick(
                controller, pipeline, tick_overrun_count);
        case MOTION_RR_ALIGNING:
            return align_tick(controller, pipeline, tick_overrun_count);
        case MOTION_RR_PLAYING:
            return play_tick(controller, pipeline, tick_overrun_count);
        case MOTION_RR_HOLDING:
            return holding_tick(controller, pipeline);
        default:
            return 0;
    }
}

int motion_record_replay_agent2_run_allowed(const MotionRecordReplay *controller)
{
    return controller != NULL &&
           (controller->mode == MOTION_RR_LIVE ||
            controller->mode == MOTION_RR_RECORDING);
}

int motion_record_replay_resume_live(MotionRecordReplay *controller,
                                     AgentPipelineContext *pipeline)
{
    if (controller == NULL || pipeline == NULL ||
        controller->mode != MOTION_RR_HOLDING ||
        controller->last_applied_replay_valid == 0U) return 0;
    resync_live(controller, pipeline, controller->reason);
    return 1;
}

int motion_record_replay_stop_play(MotionRecordReplay *controller,
                                   AgentPipelineContext *pipeline)
{
    if (controller == NULL || pipeline == NULL ||
        (controller->mode != MOTION_RR_ALIGNING &&
         controller->mode != MOTION_RR_PLAYING &&
         controller->mode != MOTION_RR_HOLDING) ||
        controller->last_applied_replay_valid == 0U) return 0;
    resync_live(controller, pipeline, MOTION_RR_REASON_STOPPED);
    return 1;
}

int motion_record_replay_on_record_button_pulse(MotionRecordReplay *controller)
{
    if (controller == NULL) return 0;
    if (controller->mode == MOTION_RR_LIVE) {
        return motion_record_replay_start_record(controller);
    }
    if (controller->mode == MOTION_RR_RECORDING) {
        if (!motion_record_replay_stop_record(controller)) return 0;
        if (controller->record_count > 0U) {
            if (!motion_record_replay_copy_record_to_replay(controller)) return 0;
            controller->reason = MOTION_RR_REASON_STOPPED;
        }
        /* 빈 녹화로 기존의 정상 Replay 데이터를 실수로 파괴하지 않는다. */
        return 1;
    }
    return 0;
}

int motion_record_replay_on_play_button_pulse(MotionRecordReplay *controller,
                                              AgentPipelineContext *pipeline,
                                              uint32_t tick_overrun_count)
{
    if (controller == NULL || pipeline == NULL) return 0;
    if (controller->mode == MOTION_RR_LIVE) {
        return motion_record_replay_start_play(
            controller, pipeline, tick_overrun_count);
    }
    if (controller->mode == MOTION_RR_RECORDING) {
        controller->reason = MOTION_RR_REASON_BUSY;
        return 0;
    }
    return motion_record_replay_stop_play(controller, pipeline);
}

MotionRecordReplayMode motion_record_replay_mode(const MotionRecordReplay *controller)
{
    return controller != NULL ? controller->mode : MOTION_RR_LIVE;
}

MotionRecordReplayReason motion_record_replay_reason(const MotionRecordReplay *controller)
{
    return controller != NULL ? controller->reason : MOTION_RR_REASON_INVALID_COMMAND;
}

uint32_t motion_record_replay_record_count(const MotionRecordReplay *controller)
{
    return controller != NULL ? controller->record_count : 0U;
}

uint32_t motion_record_replay_replay_count(const MotionRecordReplay *controller)
{
    return controller != NULL ? controller->replay_count : 0U;
}

uint32_t motion_record_replay_replay_index(const MotionRecordReplay *controller)
{
    return controller != NULL ? controller->replay_index : 0U;
}

int motion_record_replay_get_record_sample(const MotionRecordReplay *controller,
                                           uint32_t index,
                                           MotionSample *sample)
{
    if (controller == NULL || sample == NULL ||
        index >= controller->record_count) return 0;
    *sample = record_buffer[index];
    return 1;
}

int motion_record_replay_get_replay_sample(const MotionRecordReplay *controller,
                                           uint32_t index,
                                           MotionSample *sample)
{
    if (controller == NULL || sample == NULL ||
        index >= controller->replay_count) return 0;
    *sample = replay_buffer[index];
    return 1;
}
