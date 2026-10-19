#include "app/uart/control_commands.h"
#include "app/board/board_input.h"
#include "app/board/stereo_transport.h"
#include "app/cnn/cnn_app.h"
#include "app/robot/vision/input_pose.h"
#include "drivers/system/platform.h"
#include "app/uart/telemetry.h"
#include "app/robot/control/robot_pipeline.h"
#include "app/robot/record_replay/motion_library.h"
#include "app/robot/record_replay/motion_sd.h"
#include "app/robot/record_replay/motion_record_replay.h"
#include "cnn/cnn_weights.h"
#include "xil_printf.h"

int main(void)
{
    RobotPipeline pipeline;
    MotionRecordReplay record_replay;
    CnnAppEvent control_event;

    if (platform_init() != 0) return -1;
    input_pose_init();
    if (robot_motion_init(&pipeline) != 0) return -1;
    motion_record_replay_init(&record_replay);
    pipeline.gripper_independent = 1U;
    /* 2026-09-29 사용자 승인 시험값: gripper 전 범위 2초, ALIGN 최대 10초. */
    if (!motion_record_replay_configure_align(&record_replay, 0.01f, 500U)) {
        return -1;
    }
    motion_record_replay_set_repeat(&record_replay, 1);
    telemetry_init(); /* [TRACE] 컬럼 정의(# 줄)와 BOOT 이벤트 */
    if (stereo_transport_init() != 0) return -1;
    if (cnn_app_init() != 0) return -1;
    {
        if (cnn_sd_mount() == CNN_OK) motion_library_init();
        else xil_printf("[REC] SD mount failed; no automatic playback\r\n");
    }

    for (;;) {
        control_commands_service_virtual(&pipeline, &record_replay);
        /* 1. CNN 완료 결과 처리 + UART R/P에 따른 녹화/재생 모드 전환. */
        cnn_app_service();
        stereo_transport_service();
        /* CNN/storage service can delay the loop; check the wall-clock lease again. */
        control_commands_service_virtual(&pipeline, &record_replay);
        if (cnn_app_take_control_event(&control_event)) {
            control_commands_handle(&record_replay, &pipeline, control_event);
        }
        if (!motion_library_input_allowed() && !motion_library_record_preparing()) board_input_stop_follow();

        board_input_update(&pipeline, &record_replay);

        /* 3. 제어 틱 경로(20 ms): 아래 호출 내부에서 모드별로 출력한다.
         * LIVE/RECORDING: robot_motion_step -> 검증 -> servo_output_apply -> 녹화
         * ALIGNING:      robot_motion_step -> gripper 제한/검증 -> servo_output_apply
         * PLAYING:       저장 샘플 검증 -> servo_output_apply
         * HOLDING:       1회 재생 완료/오류 후 마지막 성공 명령 -> servo_output_apply
         * 녹화는 RECORDING에서 서보 출력 적용 성공 후에만 확정한다.
         * servo_output_update을 여기서 추가 호출하면 한 틱에 출력이 중복된다. */
        if (platform_tick_due()) {
            telemetry_mark(); /* [TRACE] 실행시간 측정 시작 */
            motion_record_replay_control_tick(
                &record_replay, &pipeline, platform_tick_overrun_count());
            telemetry_tick(&pipeline); /* [TRACE] TK: 이번 제어 틱의 출력 처리 뒤 기록. */
            motion_library_tick(&record_replay, &pipeline);
        }
        if (motion_library_service(&record_replay, &pipeline, platform_tick_overrun_count())) {
            (void)platform_tick_due();
            record_replay.observed_tick_overruns = platform_tick_overrun_count();
            board_input_stop_follow();
            pipeline.arm_tracking_started = 0U;
        }
        motion_library_report_state(&record_replay, &pipeline, 0);

        /* 4. 진단 로그 배출과 UART 송신 서비스. */
        if (cnn_app_settings_service(!pipeline.output_enabled && record_replay.mode == MOTION_RR_LIVE
            && motion_library_input_allowed()
            )) {
            (void)platform_tick_due();
            record_replay.observed_tick_overruns = platform_tick_overrun_count();
        }
        telemetry_poll(&pipeline); /* [TRACE] 링버퍼를 UART로 비운다(논블로킹). 1초마다 SM 줄 */
        platform_uart_service();
    }
}
