#ifndef RECORD_REPLAY_MOTION_RECORD_REPLAY_H
#define RECORD_REPLAY_MOTION_RECORD_REPLAY_H

#include <stdint.h>

#include "integration/agent_pipeline.h"

/*
 * DDR 전용 v1의 임시 최대 sample 수.
 * record/replay 버퍼는 motion_record_replay.c의 전역 정적 배열이므로
 * stack/heap이 아니라 linker가 정하는 .bss에 놓인다. MotionSample 하나는
 * 20 byte이므로 현재 값에서는 버퍼 하나가 20,480 byte, 두 버퍼 합계가
 * 40,960 byte다. 최종 Vitis linker map에서 실제 PS DDR 배치와 남은 공간을
 * 확인하기 전에는 이 값을 늘리지 않는다.
 */
#ifndef MOTION_RECORD_REPLAY_MAX_SAMPLES
#define MOTION_RECORD_REPLAY_MAX_SAMPLES 1024U
#endif

/*
 * 20 ms(50 Hz)마다 저장하는 로봇 명령 한 개.
 * timestamp 없이 배열 index 자체가 시간축이므로 RECORD 중 tick이 누락되면
 * 즉시 녹화를 중단한다. 상태값이나 향후 SD 파일 metadata는 이 구조체에
 * 넣지 않아 5개 float, 20 byte 형식을 고정한다.
 */
typedef struct {
    float elbow_roll_deg;
    float elbow_pitch_deg;
    float wrist_pitch_deg;
    float wrist_roll_deg;
    float gripper_norm;
} MotionSample;

typedef enum {
    MOTION_RR_LIVE = 0,  /* 카메라 입력을 Agent2/Agent3로 전달하는 실시간 추종 */
    MOTION_RR_RECORDING, /* LIVE 제어 결과를 매 20 ms record_buffer에 추가 */
    MOTION_RR_ALIGNING,  /* Motion 제어로 Replay Sample0까지 안전하게 이동 */
    MOTION_RR_PLAYING,   /* Motion을 우회하고 replay_buffer를 50 Hz 직접 출력 */
    MOTION_RR_HOLDING    /* Replay 오류 후 마지막 HAL 성공 명령을 계속 유지 */
} MotionRecordReplayMode;

typedef enum {
    MOTION_RR_APPLIED_NONE = 0,
    MOTION_RR_APPLIED_HAL,
    /* 구형/host context용 fallback. software output일 뿐 실제 HAL 적용을
     * 증명하지 않으므로 향후 output enable 정책에서 HAL과 구분해야 한다. */
    MOTION_RR_APPLIED_SOFTWARE_OUTPUT
} MotionRecordReplayAppliedSource;

typedef enum {
    MOTION_RR_REASON_NONE = 0,
    MOTION_RR_REASON_STOPPED,
    MOTION_RR_REASON_BUFFER_FULL,
    MOTION_RR_REASON_BUSY,
    MOTION_RR_REASON_EMPTY,
    MOTION_RR_REASON_CONFIG,
    MOTION_RR_REASON_INVALID_COMMAND,
    MOTION_RR_REASON_RANGE,
    MOTION_RR_REASON_SAFETY,
    MOTION_RR_REASON_DELTA,
    MOTION_RR_REASON_ACCELERATION,
    MOTION_RR_REASON_GRIPPER_DELTA,
    MOTION_RR_REASON_TICK_OVERRUN,
    MOTION_RR_REASON_AGENT3_FAILURE,
    MOTION_RR_REASON_ALIGN_TIMEOUT,
    /* 호환성과 내부 index 이상 검출용. 정상 PLAY 끝은 EOF가 아니라
     * Sample0 복귀 ALIGN으로 이어지므로 이 값을 사용하지 않는다. */
    MOTION_RR_REASON_EOF
} MotionRecordReplayReason;

/*
 * Record/Replay 상태머신의 전체 실행 상태.
 * 대용량 sample 배열은 이 구조체 안에 넣지 않고 .c의 정적 버퍼에 둔다.
 */
typedef struct {
    MotionRecordReplayMode mode;
    MotionRecordReplayReason reason; /* 마지막 상태 전이 또는 오류 원인 */
    uint32_t record_count;           /* record_buffer에 저장된 유효 sample 수 */
    uint32_t replay_count;           /* replay_buffer의 유효 sample 수 */
    uint32_t replay_index;           /* 다음 Direct PLAY에서 출력할 index */
    uint32_t align_ticks;            /* 현재 ALIGN에서 실행한 control step 수 */
    uint32_t align_timeout_ticks;    /* 이 횟수를 넘으면 ALIGN_TIMEOUT HOLD */
    uint32_t observed_tick_overruns; /* ALIGN/PLAY 시작 때의 overrun 기준값 */
    uint32_t record_tick_overrun_baseline; /* RECORD 첫 실제 tick의 기준값 */
    float align_gripper_norm;        /* rate limit을 적용한 현재 gripper 값 */
    float align_gripper_max_delta_norm; /* ALIGN 한 tick의 gripper 최대 변화량 */
    ForearmJointCommand align_target; /* 현재 ALIGN 목적지, 항상 Sample0 */
    ForearmJointCommand last_applied_replay_command; /* 마지막 Agent3 성공 명령 */
    uint8_t last_applied_replay_valid; /* 위 command를 사용할 수 있으면 1 */
    uint8_t record_tick_baseline_valid; /* RECORD overrun 기준을 잡았으면 1 */
    MotionRecordReplayAppliedSource last_applied_replay_source;
    /* 저장값은 항상 A2 출력 명령이다. 아래 source는 그 명령이 같은 tick에
     * 실제 HAL 적용까지 성공했는지, PWM-disabled software 변환만 성공했는지를
     * 구분한다. */
    MotionRecordReplayAppliedSource record_source;
    MotionRecordReplayAppliedSource replay_source;
} MotionRecordReplay;

void motion_record_replay_init(MotionRecordReplay *controller);

/*
 * ALIGN의 gripper 속도와 timeout을 설정한다.
 * 검증되지 않은 실물 속도를 코드에 임의로 넣지 않기 위해 설정 전에는
 * PLAY 시작을 거부한다. gripper 값은 0~1 정규화 단위의 tick당 변화량이다.
 */
int motion_record_replay_configure_align(MotionRecordReplay *controller,
                                         float gripper_max_delta_norm_per_tick,
                                         uint32_t timeout_ticks);

int motion_record_replay_start_record(MotionRecordReplay *controller);
int motion_record_replay_stop_record(MotionRecordReplay *controller);

/* 완료된 record_buffer를 별도 replay_buffer로 복사한다. */
int motion_record_replay_copy_record_to_replay(MotionRecordReplay *controller);

/* host test 및 향후 SD loader 경계. 외부 포인터를 보관하지 않고 내용을 복사한다. */
int motion_record_replay_load_replay(MotionRecordReplay *controller,
                                     const MotionSample *samples,
                                     uint32_t count);

int motion_record_replay_start_play(MotionRecordReplay *controller,
                                    AgentPipelineContext *pipeline,
                                    uint32_t tick_overrun_count);

/*
 * ALIGN/PLAY/HOLD를 사용자가 중단할 때 호출한다.
 * 마지막 Agent3 성공 명령으로 Motion을 reseed한 뒤 LIVE로 복귀하며, Replay 중
 * 쌓인 stale camera target은 버려 다음 fresh frame부터 다시 추종한다.
 */
int motion_record_replay_stop_play(MotionRecordReplay *controller,
                                   AgentPipelineContext *pipeline);

/* 오류 HOLD만 해제하는 기존 호환 API. 오류 reason은 보존한 채 LIVE로 간다. */
int motion_record_replay_resume_live(MotionRecordReplay *controller,
                                     AgentPipelineContext *pipeline);

/*
 * debounce와 rising-edge 검출이 끝난 1회 pulse용 software event API.
 * 물리 GPIO level을 직접 넣는 함수가 아니며 pulse 한 번당 정확히 한 번 호출한다.
 * 실제 GPIO 주소, pin 번호, debounce 시간은 이 모듈의 책임이 아니다.
 */
int motion_record_replay_on_record_button_pulse(MotionRecordReplay *controller);
int motion_record_replay_on_play_button_pulse(MotionRecordReplay *controller,
                                              AgentPipelineContext *pipeline,
                                              uint32_t tick_overrun_count);

/* 현재 mode에 맞는 20 ms control step을 정확히 한 번 실행한다. */
int motion_record_replay_control_tick(MotionRecordReplay *controller,
                                      AgentPipelineContext *pipeline,
                                      uint32_t tick_overrun_count);

/*
 * Camera/Agent1은 모든 mode에서 최신 frame/filter 처리를 계속할 수 있다.
 * Agent2의 새 target 반영은 LIVE/RECORDING에서만 허용하여 Replay와 충돌을 막는다.
 */
int motion_record_replay_agent2_run_allowed(const MotionRecordReplay *controller);

MotionRecordReplayMode motion_record_replay_mode(const MotionRecordReplay *controller);
MotionRecordReplayReason motion_record_replay_reason(const MotionRecordReplay *controller);
uint32_t motion_record_replay_record_count(const MotionRecordReplay *controller);
uint32_t motion_record_replay_replay_count(const MotionRecordReplay *controller);
uint32_t motion_record_replay_replay_index(const MotionRecordReplay *controller);
MotionRecordReplayAppliedSource motion_record_replay_record_source(
    const MotionRecordReplay *controller);
MotionRecordReplayAppliedSource motion_record_replay_replay_source(
    const MotionRecordReplay *controller);
const char *motion_record_replay_mode_name(MotionRecordReplayMode mode);
const char *motion_record_replay_reason_name(MotionRecordReplayReason reason);
const char *motion_record_replay_source_name(
    MotionRecordReplayAppliedSource source);
int motion_record_replay_get_record_sample(const MotionRecordReplay *controller,
                                           uint32_t index,
                                           MotionSample *sample);
int motion_record_replay_get_replay_sample(const MotionRecordReplay *controller,
                                           uint32_t index,
                                           MotionSample *sample);

#endif /* RECORD_REPLAY_MOTION_RECORD_REPLAY_H */
