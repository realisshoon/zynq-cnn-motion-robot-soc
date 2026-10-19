#include "app/board/board_input.h"
#include "app/board/stereo_transport.h"
#include "app/robot/vision/input_pose.h"
#include "app/uart/telemetry.h"
#include "app/robot/record_replay/motion_library.h"
#include "robot_board_config.h"
#include "../robot/vision/input_pose_cnn.h"

static const BoardInputProfile profile = {
    .uart_role = 1U, .forearm_mm = 135U, .tip_mm = 190U,
    .gripper_open_min_us = ROBOT_LEFT_GRIPPER_OPEN_MIN_US, .gripper_close_max_us = ROBOT_LEFT_GRIPPER_CLOSE_MAX_US,
    .side = "l", .arm_name = "ROBOT1_JC_CH0_CH4",
    .follow_status = "status_remote_stereo_follow", .follow_enabled = "enabled_remote_UNSYNCHRONIZED_test", .follow_disabled = "disabled_new_remote_targets",
    .follow_help = "  A : request stereo HUMAN-angle follow from RIGHT (local PWM ON, LIVE)\r\n"
};

const BoardInputProfile *board_input_profile(void) { return &profile; }
int board_input_set_follow(int enabled) { return stereo_transport_set_remote_follow(enabled); }
int board_input_follow_enabled(void) { return stereo_transport_remote_follow_enabled(); }
int board_input_remote_requested(void) { return 0; }
void board_input_stop_follow(void) { (void)board_input_set_follow(0); }

void board_input_update(RobotPipeline *pipeline, MotionRecordReplay *record_replay)
{
    HumanForearmTarget remote_target;
    HumanPose2D remote_gripper;
    uint32_t remote_epoch;
    int remote_stationary, remote_wrist_fresh, remote_gripper_session;

    if (!pipeline->output_enabled || !motion_record_replay_live_target_allowed(record_replay) ||
        (!motion_library_input_allowed() && !motion_library_record_preparing()))
        (void)stereo_transport_set_remote_follow(0);
    if (stereo_transport_take_target(&remote_target, &remote_stationary, &remote_wrist_fresh,
            &remote_epoch) && (stereo_transport_remote_follow_enabled() || pipeline->virtual_compute) &&
        robot_motion_calculation_enabled(pipeline) && motion_record_replay_live_target_allowed(record_replay) &&
        motion_library_input_allowed()) {
        pipeline->target = remote_target;
        pipeline->target_ready = remote_target.valid;
        pipeline->pose.frame_id = remote_target.frame_id;
        pipeline->wrist_observation_fresh = remote_wrist_fresh ? 1U : 0U;
        pipeline->frames_in++;
        robot_motion_sync_input(pipeline, remote_stationary, remote_epoch);
        if (pipeline->target_ready) pipeline->targets_valid++;
        (void)robot_motion_set_target(pipeline);
        telemetry_robot_target(pipeline);
    }
    if (stereo_transport_take_gripper(&remote_gripper, &remote_gripper_session) &&
        (stereo_transport_remote_follow_enabled() || pipeline->virtual_compute) && robot_motion_calculation_enabled(pipeline) &&
        motion_record_replay_live_target_allowed(record_replay) && motion_library_input_allowed()) {
        if (remote_gripper_session) gripper_control_reset(pipeline);
        (void)gripper_control_update(pipeline, &remote_gripper);
    }
}
