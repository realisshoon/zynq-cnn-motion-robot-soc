#include "harness.h"
#include <stdio.h>
#include <string.h>
#include "dual_arm_config.h"
#include "integration/platform.h"
#include "integration/input_pose.h"
#include "input_pose_cnn.h"
#include "cnn_app_event.h"
#include "output_controller/servo_hal.h"
#include "drivers/servo_pwm_driver.h"
#include "../cnn_firmware/cnn/cnn_weights.h"

HarnessMock mock;
AgentPipelineContext *active_pipeline;
MotionRecordReplay *active_controller;

void harness_mock_reset(void)
{
    memset(&mock, 0, sizeof(mock));
    mock.board_ready = 1;
    mock.epoch = 0x12345678U;
    cnn_app_control_event_reset();
    active_pipeline = NULL;
    active_controller = NULL;
}

void harness_make_frame(uint32_t frame_id, int fingers)
{
    memset(&mock.image, 0, sizeof(mock.image));
    memset(&mock.measured, 0, sizeof(mock.measured));
    mock.image.frame_id = mock.measured.frame_id = frame_id;
    mock.image.valid = mock.measured.valid = 1U;
    mock.image.elbow = (Point2D){500.0f, 500.0f, 1U};
    mock.image.wrist = (Point2D){500.0f, 400.0f, 1U};
    mock.measured.elbow = (Point3D){0.0f, 0.0f, 1000.0f, 1U};
    mock.measured.wrist = (Point3D){0.0f, 100.0f, 900.0f, 1U};
    if (fingers) {
        mock.image.finger1 = (Point2D){480.0f, 300.0f, 1U};
        mock.image.finger2 = (Point2D){540.0f, 300.0f, 1U};
        mock.measured.finger1 = (Point3D){-20.0f, 200.0f, 850.0f, 1U};
        mock.measured.finger2 = (Point3D){40.0f, 200.0f, 850.0f, 1U};
    }
    mock.gripper = mock.image;
    mock.gripper.gripper_2d.source = 1U;
    mock.gripper.gripper_2d.reference_span_px = 100.0f;
    mock.gripper.gripper_2d.wrist = mock.image.wrist;
    mock.gripper.gripper_2d.finger1 = (Point2D){480.0f, 300.0f, 1U};
    mock.gripper.gripper_2d.finger2 = (Point2D){540.0f, 300.0f, 1U};
    mock.frame_ready = 1;
}

int platform_init(void)
{
    servo_pwm_driver_mock_reset();
    servo_hal_init();
    return 0;
}
int platform_tick_due(void) { return 1; }
uint32_t platform_tick_overrun_count(void) { return 0U; }
void platform_uart_service(void) { harness_iteration_end(); }
void input_pose_init(void) {}
int input_pose_ready(void)
{
    return mock.frame_ready && (mock.local_follow || mock.foreign_request);
}
int input_pose_take(HumanPose2D *pose, float *dt_sec)
{
    ++mock.mono_takes;
    *pose = mock.image;
    *dt_sec = .02f;
    mock.frame_ready = 0;
    return 1;
}
int input_pose_cnn_take_stereo(HumanPose2D *image, HumanPose3D *measured, float *dt_sec)
{
    ++mock.stereo_takes;
    *image = mock.image;
    *measured = mock.measured;
    *dt_sec = .02f;
    mock.frame_ready = 0;
    return 1;
}
int input_pose_cnn_arm_stationary(void) { return mock.stationary; }
uint32_t input_pose_cnn_filter_epoch(void) { return mock.epoch; }
int input_pose_cnn_take_gripper(StereoDepthResult *depth, int *new_session)
{
    if (!mock.gripper_ready) return 0;
    ++mock.gripper_takes;
    memset(depth, 0, sizeof(*depth));
    depth->image_pose = mock.gripper;
    *new_session = mock.gripper_new_session;
    mock.gripper_ready = 0;
    return 1;
}
int cnn_app_init(void) { cnn_app_control_event_reset(); return 0; }
void cnn_app_service(void) { harness_iteration_begin(); }
int cnn_app_settings_service(int storage_safe) { (void)storage_safe; return 0; }
cnn_error_t cnn_sd_mount(void) { return CNN_OK; }

void cnn_app_report_pwm_result(unsigned enabled, const char *result, const char *mode)
{
    (void)enabled;
    (void)mode;
    (void)snprintf(mock.last_result, sizeof(mock.last_result), "%s", result);
}
void cnn_app_report_async_result(unsigned enabled, unsigned pwm_enabled, const char *result, const char *mode)
{
    (void)pwm_enabled;
    cnn_app_report_pwm_result(enabled, result, mode);
}
void cnn_app_report_control_result(CnnAppEvent event, int accepted, const char *mode,
    const char *reason, unsigned long record_count, unsigned long replay_count,
    const char *record_source, const char *replay_source)
{
    (void)event;
    (void)accepted;
    (void)record_count;
    (void)replay_count;
    (void)record_source;
    (void)replay_source;
    cnn_app_report_pwm_result(0U, reason, mode);
}

int stereo_board_init(void) { return 0; }
void stereo_board_service(void) {}
int stereo_board_set_async_test(int enabled)
{
    ++mock.set_local_calls;
    if (!mock.board_ready) return 0;
    mock.local_follow = enabled != 0;
    return 1;
}
int stereo_board_async_test_enabled(void) { return mock.local_follow; }
int stereo_board_set_remote_follow(int enabled)
{
    ++mock.set_remote_calls;
    if (!mock.board_ready) return 0;
    mock.remote_follow = enabled != 0;
    return 1;
}
int stereo_board_remote_follow_enabled(void) { return mock.remote_follow; }
int stereo_board_remote_requested(void) { return mock.foreign_request; }
int stereo_board_send_target(const HumanForearmTarget *target, int stationary,
    int wrist_fresh, uint32_t epoch)
{
    ++mock.target_sends;
    mock.sent_target = *target;
    mock.sent_stationary = stationary;
    mock.sent_wrist_fresh = wrist_fresh;
    mock.sent_epoch = epoch;
    return 1;
}
int stereo_board_take_target(HumanForearmTarget *target, int *stationary,
    int *wrist_fresh, uint32_t *epoch)
{
    if (!mock.target_ready) return 0;
    ++mock.target_takes;
    *target = mock.target;
    *stationary = mock.stationary;
    *wrist_fresh = mock.wrist_fresh;
    *epoch = mock.epoch;
    mock.target_ready = 0;
    return 1;
}
int stereo_board_send_gripper(const HumanPose2D *pose)
{
    (void)pose;
    ++mock.gripper_sends;
    return 1;
}
int stereo_board_take_gripper(HumanPose2D *pose, int *new_session)
{
    if (!mock.gripper_ready) return 0;
    ++mock.gripper_takes;
    *pose = mock.gripper;
    *new_session = mock.gripper_new_session;
    mock.gripper_ready = 0;
    return 1;
}
