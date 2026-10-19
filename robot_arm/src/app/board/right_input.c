#include "app/board/board_input.h"
#include "app/board/stereo_transport.h"
#include "app/robot/vision/input_pose.h"
#include "app/uart/telemetry.h"
#include "app/robot/record_replay/motion_library.h"
#include "robot_board_config.h"
#include "../robot/vision/input_pose_cnn.h"

static const BoardInputProfile profile = {
    .uart_role = 2U, .forearm_mm = 160U, .tip_mm = 200U,
    .gripper_open_min_us = 0U, .gripper_close_max_us = 0U,
    .side = "r", .arm_name = "ROBOT0_JB_CH0_CH4",
    .follow_status = "status", .follow_enabled = "enabled_UNSYNCHRONIZED_test", .follow_disabled = "disabled_new_async_targets",
    .follow_help = "  A : enable EXPERIMENTAL asynchronous binocular control (RIGHT, PWM ON, LIVE)\r\n"
};

const BoardInputProfile *board_input_profile(void) { return &profile; }
int board_input_set_follow(int enabled) { return stereo_transport_set_async_test(enabled); }
int board_input_follow_enabled(void) { return stereo_transport_async_test_enabled(); }
int board_input_remote_requested(void) { return stereo_transport_remote_requested(); }
void board_input_stop_follow(void) { (void)board_input_set_follow(0); }

void board_input_update(RobotPipeline *pipeline, MotionRecordReplay *record_replay)
{
    HumanPose2D pose;
    HumanPose3D measured_pose;
    StereoDepthResult gripper_depth;
    int gripper_new_session;
    float dt_sec;

    if (input_pose_ready() && input_pose_cnn_take_stereo(&pose, &measured_pose, &dt_sec)) {
        telemetry_input(&pose);
        telemetry_mark(); /* [TRACE] 실행시간 측정 시작 */
        human_target_update_stereo(pipeline, &pose, &measured_pose, dt_sec);
        robot_motion_sync_input(pipeline, input_pose_cnn_arm_stationary(),
            input_pose_cnn_filter_epoch());
        if (pipeline->target_ready && pipeline->target.frame_id == pose.frame_id)
            (void)stereo_transport_send_target(&pipeline->target, pipeline->arm_stationary,
                pipeline->wrist_observation_fresh, pipeline->arm_input_epoch);
        telemetry_pose_target(pipeline); /* [TRACE] A1, P3: A2 처리 전 입력을 기록한다. */
        if (motion_record_replay_live_target_allowed(record_replay)
            && motion_library_input_allowed()
            && (stereo_transport_async_test_enabled() || pipeline->virtual_compute)
            ) {
            robot_motion_set_target(pipeline);
            telemetry_robot_target(pipeline); /* [TRACE] A2 */
        }
    }

    if (input_pose_cnn_take_gripper(&gripper_depth, &gripper_new_session)) {
        (void)stereo_transport_send_gripper(&gripper_depth.image_pose);
        if (motion_record_replay_live_target_allowed(record_replay) &&
            motion_library_input_allowed() && robot_motion_calculation_enabled(pipeline)
            && (stereo_transport_async_test_enabled() || pipeline->virtual_compute)
            ) {
            int result;
            if (gripper_new_session) gripper_control_reset(pipeline);
            result = gripper_control_update(pipeline, &gripper_depth.image_pose);
            telemetry_gripper(pipeline, &gripper_depth, result);
        }
    }
}
