#include "app/uart/control_commands.h"
#include "app/board/board_input.h"
#include "app/board/stereo_transport.h"
#include "drivers/system/platform.h"
#include "drivers/pwm/servo_hal.h"
#include "app/robot/record_replay/motion_library.h"
#include "../cnn/cnn_app_event.h"
#include "xil_printf.h"

static void report_virtual_state(const RobotPipeline *pipeline, const char *result,
                                 uint32_t request_nonce)
{
    const char *side = board_input_profile()->side;
    unsigned follow = (unsigned)board_input_follow_enabled();
    xil_printf("[VC_STATE] schema=1 contract=3B3_VC1 build=UNKNOWN side=%s request_nonce=%lu nonce=%lu last_nonce=%lu calc=%u virtual_compute=%u real_permit=%u pwm=%u follow=%u fault=%u t_us=%lu result=%s physical=UNVERIFIED\r\n",
        side, (unsigned long)request_nonce, (unsigned long)pipeline->virtual_session,
        (unsigned long)pipeline->virtual_last_session, (unsigned)robot_motion_calculation_enabled(pipeline),
        (unsigned)pipeline->virtual_compute,
        (unsigned)servo_hal_output_permitted(), (unsigned)pipeline->output_enabled, follow,
        (unsigned)pipeline->output_faulted, (unsigned long)platform_monotonic_us(), result);
}

void control_commands_service_virtual(RobotPipeline *pipeline, MotionRecordReplay *controller)
{
    static uint32_t report_us;
    uint32_t now = platform_monotonic_us();
    if (robot_motion_virtual_service(pipeline, now) ||
        (!pipeline->virtual_compute && stereo_transport_virtual_compute_enabled()) ||
        (pipeline->virtual_compute &&
         !motion_library_virtual_allowed(controller))) {
        if (pipeline->virtual_compute)
            (void)robot_motion_virtual_request(pipeline, 0U, pipeline->virtual_session, now);
        (void)stereo_transport_set_virtual_compute(0);
        report_virtual_state(pipeline, "ended_or_expired", 0U);
    }
    if ((uint32_t)(now-report_us) >= 1000000U) {
        report_us = now;
        report_virtual_state(pipeline, "status", 0U);
    }
}

void control_commands_handle(MotionRecordReplay *record_replay,
                                       RobotPipeline *pipeline,
                                       CnnAppEvent event)
{
    int accepted = 0;

    if (event == CNN_APP_EVENT_VIRTUAL_START || event == CNN_APP_EVENT_VIRTUAL_RENEW ||
        event == CNN_APP_EVENT_VIRTUAL_STOP || event == CNN_APP_EVENT_VIRTUAL_STATUS) {
        uint32_t nonce = cnn_app_virtual_request_session();
        unsigned op = event == CNN_APP_EVENT_VIRTUAL_START ? 1U :
            event == CNN_APP_EVENT_VIRTUAL_RENEW ? 2U : 0U;
        if (event == CNN_APP_EVENT_VIRTUAL_STATUS) {
            report_virtual_state(pipeline, "status", nonce);
            return;
        }
        if (op == 0U || (op == 2U && pipeline->virtual_compute && motion_library_virtual_allowed(record_replay)) || (record_replay->mode == MOTION_RR_LIVE &&
            !record_replay->gripper_catchup && motion_library_input_allowed())) {
            accepted = robot_motion_virtual_request(pipeline, op, nonce, platform_monotonic_us());
            if (accepted && op != 2U && !stereo_transport_set_virtual_compute(pipeline->virtual_compute)) {
                if (pipeline->virtual_compute)
                    (void)robot_motion_virtual_request(pipeline, 0U, nonce, platform_monotonic_us());
                accepted = 0;
            }
        }
        if (!pipeline->virtual_compute && stereo_transport_virtual_compute_enabled())
            (void)stereo_transport_set_virtual_compute(0);
        report_virtual_state(pipeline, accepted ? "accepted" : "rejected", nonce);
        return;
    }
    if (pipeline->virtual_compute && event == CNN_APP_EVENT_PLAY_TOGGLE) {
        cnn_app_report_control_result(event, 0, motion_record_replay_mode_name(record_replay->mode),
            "virtual_compute_active", 0, 0, "none", "none");
        return;
    }

    if (event == CNN_APP_EVENT_GRIP_OPEN || event == CNN_APP_EVENT_GRIP_AUTO) {
        const char *result = "rejected_not_RIGHT_LIVE_PWM_ON";
        if (pipeline->output_enabled && motion_record_replay_mode(record_replay) == MOTION_RR_LIVE &&
            record_replay->gripper_catchup == 0U) {
            accepted = gripper_control_set_manual(pipeline, event == CNN_APP_EVENT_GRIP_OPEN);
            result = accepted ? (event == CNN_APP_EVENT_GRIP_OPEN ? "manual_open_until_H" : "automatic_restored")
                : "rejected_safety";
        }
        cnn_app_report_pwm_result((unsigned)pipeline->output_enabled, result,
            motion_record_replay_mode_name(motion_record_replay_mode(record_replay)));
        return;
    }

    if (pipeline->gripper_motion_hold.manual_open && event == CNN_APP_EVENT_PLAY_TOGGLE) {
        board_input_stop_follow();
        cnn_app_report_pwm_result((unsigned)pipeline->output_enabled, "rejected_manual_grip_open_use_H_first",
            motion_record_replay_mode_name(motion_record_replay_mode(record_replay)));
        return;
    }
    if (motion_library_event(record_replay, pipeline, event)) {
        if (event == CNN_APP_EVENT_PLAY_TOGGLE ||
            (!motion_library_input_allowed() && !motion_library_record_preparing()))
            board_input_stop_follow();
        motion_library_report_state(record_replay, pipeline, 0);
        return;
    }

    if ((event == CNN_APP_EVENT_ASYNC_ENABLE || event == CNN_APP_EVENT_ASYNC_DISABLE ||
         event == CNN_APP_EVENT_RECORD_TOGGLE || event == CNN_APP_EVENT_PLAY_TOGGLE) &&
        (event != CNN_APP_EVENT_ASYNC_ENABLE || motion_library_input_allowed()))
        gripper_control_reset(pipeline);

    if (event == CNN_APP_EVENT_ASYNC_ENABLE ||
        event == CNN_APP_EVENT_ASYNC_DISABLE || event == CNN_APP_EVENT_ASYNC_STATUS) {
        const BoardInputProfile *profile = board_input_profile();
        const char *result = profile->follow_status;
        unsigned enabled;
        if (event == CNN_APP_EVENT_ASYNC_ENABLE) {
            if (!pipeline->output_enabled ||
                motion_record_replay_mode(record_replay) != MOTION_RR_LIVE ||
                record_replay->gripper_catchup != 0U || !motion_library_input_allowed()) {
                result = "rejected_enable_PWM_first_and_use_LIVE";
            } else {
                result = board_input_set_follow(1) ? profile->follow_enabled
                    : "rejected_board_not_ready";
            }
        } else if (event == CNN_APP_EVENT_ASYNC_DISABLE) {
            result = board_input_set_follow(0) ? profile->follow_disabled
                : "rejected_board_not_ready";
        }
        enabled = (unsigned)board_input_follow_enabled();
        cnn_app_report_async_result(enabled, (unsigned)pipeline->output_enabled, result,
            motion_record_replay_mode_name(motion_record_replay_mode(record_replay)));
        return;
    }

    if (event == CNN_APP_EVENT_PWM_ENABLE ||
        event == CNN_APP_EVENT_PWM_DISABLE ||
        event == CNN_APP_EVENT_PWM_STATUS) {
        const char *result = "status";
        if (event == CNN_APP_EVENT_PWM_DISABLE) {
            if (pipeline->virtual_compute) {
                (void)robot_motion_virtual_request(pipeline, 0U, pipeline->virtual_session, platform_monotonic_us());
                (void)stereo_transport_set_virtual_compute(0);
            }
            (void)servo_output_set_enabled(pipeline, 0);
            if (!pipeline->output_enabled && pipeline->output_parked)
                motion_record_replay_on_output_change(record_replay, pipeline);
            result = servo_output_result_name(pipeline);
            if (!pipeline->output_enabled) board_input_stop_follow();
        } else if (event == CNN_APP_EVENT_PWM_ENABLE) {
            if (motion_record_replay_mode(record_replay) != MOTION_RR_LIVE ||
                record_replay->gripper_catchup != 0U) {
                result = "rejected_non_LIVE_or_gripper_catchup";
            } else {
                unsigned was_enabled = pipeline->output_enabled;
                if (servo_output_set_enabled(pipeline, 1) && !was_enabled)
                    motion_record_replay_on_output_change(record_replay, pipeline);
                result = servo_output_result_name(pipeline);
            }
        }
        cnn_app_report_pwm_result((unsigned)pipeline->output_enabled, result,
                   motion_record_replay_mode_name(motion_record_replay_mode(record_replay)));
        if (event == CNN_APP_EVENT_PWM_STATUS)
            xil_printf("[RR] replay_samples=%lu repeat=%u storage=SD_library\r\n",
                (unsigned long)motion_record_replay_replay_count(record_replay),
                (unsigned int)record_replay->repeat_play);
        if (event == CNN_APP_EVENT_PWM_STATUS) {
            motion_library_status();
            motion_library_report_state(record_replay, pipeline, 1);
        }
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
    if (accepted && event == CNN_APP_EVENT_PLAY_TOGGLE) {
        pipeline->arm_tracking_started = 0U;
        board_input_stop_follow();
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
