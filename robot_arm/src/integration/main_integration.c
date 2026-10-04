#include "integration/agent_pipeline.h"
#include "integration/input_pose.h"
#include "integration/platform.h"
#include "integration/trace.h"
#include "record_replay/motion_record_replay.h"
#include "integration/cnn_app.h"
#if defined(ROBOT_STEREO_LEFT) || defined(ROBOT_STEREO_RIGHT)
#include "integration/stereo_board.h"
#include "input_pose_cnn.h"
#endif

static void handle_record_or_play_event(MotionRecordReplay *record_replay,
                                       AgentPipelineContext *pipeline,
                                       CnnAppEvent event);

/*
 * 통합 진입점: 새 CNN 프레임의 목표 계산과 20 ms 출력 제어를 분리한다.
 * 프레임: agent1_run -> [LIVE/RECORDING] agent2_run.
 * 제어 틱: Record/Replay 제어기가 모드에 따라 Agent2/Agent3를 호출한다.
 * src/main.c(Agent3의 HAL 데모)는 그대로 두고, 이 파일을 Vitis 통합 단계에서 main으로 쓴다.
 * CNN 완료 결과가 input_pose_cnn을 통해 프레임 경로로 들어온다.
 *
 * [TRACE] 표시가 붙은 줄은 UART 디버그 로그용이다. ROBOT_TRACE를 정의하지 않으면 아무것도 하지 않는다
 * (integration/trace.h). 켜면 UART가 921600 baud로 바뀐다.
 */
int main(void)
{
    AgentPipelineContext pipeline;
    MotionRecordReplay record_replay;
    CnnAppEvent control_event;
    HumanPose2D pose;
#ifdef ROBOT_STEREO_RIGHT
    HumanPose3D measured_pose;
#endif
    float dt_sec;

    if (platform_init() != 0) return -1;
    input_pose_init();
    /* Enable robot PWM only in a separately reviewed servo-test build. */
#if defined(ROBOT_ARM_PWM_ENABLE) && !defined(ROBOT_STEREO_LEFT)
    if (agent_pipeline_init_mode(&pipeline, 1) != 0) return -1;
#else
    if (agent_pipeline_init_mode(&pipeline, 0) != 0) return -1;
#endif
    motion_record_replay_init(&record_replay);
    /* 2026-09-29 사용자 승인 시험값: gripper 전 범위 2초, ALIGN 최대 10초. */
    if (!motion_record_replay_configure_align(&record_replay, 0.01f, 500U)) {
        return -1;
    }
    motion_record_replay_set_repeat(&record_replay, 1);
    TRACE_INIT(); /* [TRACE] 컬럼 정의(# 줄)와 BOOT 이벤트 */
#if defined(ROBOT_STEREO_LEFT) || defined(ROBOT_STEREO_RIGHT)
    if (stereo_board_init() != 0) return -1;
#endif
    if (cnn_app_init() != 0) return -1;

    for (;;) {
        /* 1. CNN 완료 결과 처리 + UART R/P에 따른 녹화/재생 모드 전환. */
        cnn_app_service();
#if defined(ROBOT_STEREO_LEFT) || defined(ROBOT_STEREO_RIGHT)
        stereo_board_service();
#endif
        if (cnn_app_take_control_event(&control_event)) {
            handle_record_or_play_event(&record_replay, &pipeline, control_event);
        }

        /* 2. 프레임 경로(가변 주기): 사람 자세 -> 로봇 목표.
         * A1은 모든 모드에서 실행하고, A2 목표는 LIVE/RECORDING에서만 갱신한다. */
#ifdef ROBOT_STEREO_RIGHT
        if (input_pose_ready() && input_pose_cnn_take_stereo(&pose, &measured_pose, &dt_sec)) {
#else
        if (input_pose_ready() && input_pose_take(&pose, &dt_sec)) {
#endif
            TRACE_IN(&pose);
            TRACE_MARK(); /* [TRACE] 실행시간 측정 시작 */
#ifdef ROBOT_STEREO_RIGHT
            agent1_run_stereo(&pipeline, &pose, &measured_pose, dt_sec);
#else
            agent1_run(&pipeline, &pose, dt_sec);
#endif
            TRACE_A1(&pipeline); /* [TRACE] A1, P3: A2 처리 전 입력을 기록한다. */
            if (motion_record_replay_agent2_run_allowed(&record_replay)) {
                agent2_run(&pipeline);
                TRACE_A2(&pipeline); /* [TRACE] A2 */
            }
        }

        /* 3. 제어 틱 경로(20 ms): 아래 호출 내부에서 모드별로 출력한다.
         * LIVE/RECORDING: agent2_tick -> 검증 -> agent3_apply_command -> 녹화
         * ALIGNING:      agent2_tick -> gripper 제한/검증 -> agent3_apply_command
         * PLAYING:       저장 샘플 검증 -> agent3_apply_command
         * HOLDING:       1회 재생 완료/오류 후 마지막 성공 명령 -> agent3_apply_command
         * 녹화는 RECORDING에서 Agent3 적용 성공 후에만 확정한다.
         * agent3_run을 여기서 추가 호출하면 한 틱에 출력이 중복된다. */
        if (platform_tick_due()) {
            TRACE_MARK(); /* [TRACE] 실행시간 측정 시작 */
            motion_record_replay_control_tick(
                &record_replay, &pipeline, platform_tick_overrun_count());
            TRACE_TK(&pipeline); /* [TRACE] TK: 이번 제어 틱의 출력 처리 뒤 기록. */
        }

        /* 4. 진단 로그 배출과 UART 송신 서비스. */
        TRACE_POLL(&pipeline); /* [TRACE] 링버퍼를 UART로 비운다(논블로킹). 1초마다 SM 줄 */
        platform_uart_service();
    }
}

/* 모드 전환과 UART 응답의 세부 처리는 main의 프레임/틱 흐름에서 분리한다. */
static void handle_record_or_play_event(MotionRecordReplay *record_replay,
                                       AgentPipelineContext *pipeline,
                                       CnnAppEvent event)
{
    int accepted = 0;

    if (event == CNN_APP_EVENT_ASYNC_ENABLE ||
        event == CNN_APP_EVENT_ASYNC_DISABLE || event == CNN_APP_EVENT_ASYNC_STATUS) {
        const char *result = "rejected_not_RIGHT_stereo";
        unsigned enabled = 0U;
#ifdef ROBOT_STEREO_RIGHT
        result = "status";
        if (event == CNN_APP_EVENT_ASYNC_ENABLE) {
            if (!pipeline->output_enabled ||
                motion_record_replay_mode(record_replay) != MOTION_RR_LIVE ||
                record_replay->gripper_catchup != 0U) {
                result = "rejected_enable_PWM_first_and_use_LIVE";
            } else {
                result = stereo_board_set_async_test(1) ? "enabled_UNSYNCHRONIZED_test"
                    : "rejected_board_not_ready";
            }
        } else if (event == CNN_APP_EVENT_ASYNC_DISABLE) {
            result = stereo_board_set_async_test(0) ? "disabled_new_async_targets"
                : "rejected_board_not_ready";
        }
        enabled = (unsigned)stereo_board_async_test_enabled();
#endif
        cnn_app_report_async_result(enabled, (unsigned)pipeline->output_enabled, result,
            motion_record_replay_mode_name(motion_record_replay_mode(record_replay)));
        return;
    }

    if (event == CNN_APP_EVENT_PWM_ENABLE ||
        event == CNN_APP_EVENT_PWM_DISABLE ||
        event == CNN_APP_EVENT_PWM_STATUS) {
        const char *result = "status";
        if (event == CNN_APP_EVENT_PWM_DISABLE) {
            (void)agent_pipeline_set_output_enabled(pipeline, 0);
            if (!pipeline->output_enabled && pipeline->output_parked)
                motion_record_replay_on_output_change(record_replay, pipeline);
            result = agent_pipeline_output_result_name(pipeline);
#ifdef ROBOT_STEREO_RIGHT
            if (!pipeline->output_enabled) (void)stereo_board_set_async_test(0);
#endif
        } else if (event == CNN_APP_EVENT_PWM_ENABLE) {
#ifdef ROBOT_STEREO_LEFT
            result = "rejected_left_sender_only";
#else
            if (motion_record_replay_mode(record_replay) != MOTION_RR_LIVE ||
                record_replay->gripper_catchup != 0U) {
                result = "rejected_non_LIVE_or_gripper_catchup";
            } else {
                unsigned was_enabled = pipeline->output_enabled;
                if (agent_pipeline_set_output_enabled(pipeline, 1) && !was_enabled)
                    motion_record_replay_on_output_change(record_replay, pipeline);
                result = agent_pipeline_output_result_name(pipeline);
            }
#endif
        }
        cnn_app_report_pwm_result((unsigned)pipeline->output_enabled, result,
                   motion_record_replay_mode_name(motion_record_replay_mode(record_replay)));
        return;
    }

    if (!pipeline->output_enabled && pipeline->output_parked &&
        (event == CNN_APP_EVENT_RECORD_TOGGLE || event == CNN_APP_EVENT_PLAY_TOGGLE)) {
        accepted = 0;
        record_replay->reason = MOTION_RR_REASON_BUSY;
    } else if (event == CNN_APP_EVENT_RECORD_TOGGLE) {
        accepted = motion_record_replay_on_record_button_pulse(record_replay);
    } else if (event == CNN_APP_EVENT_PLAY_TOGGLE) {
        accepted = motion_record_replay_on_play_button_pulse(
            record_replay, pipeline, platform_tick_overrun_count());
    }
    cnn_app_report_control_result(
        event, accepted,
        motion_record_replay_mode_name(motion_record_replay_mode(record_replay)),
        motion_record_replay_reason_name(motion_record_replay_reason(record_replay)),
        (unsigned long)motion_record_replay_record_count(record_replay),
        (unsigned long)motion_record_replay_replay_count(record_replay),
        motion_record_replay_source_name(
            motion_record_replay_record_source(record_replay)),
        motion_record_replay_source_name(
            motion_record_replay_replay_source(record_replay)));
}
