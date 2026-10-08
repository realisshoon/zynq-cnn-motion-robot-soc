#include "human_target_angle/agent1_forearm_stage.h"
#include "pose_mapping_internal.h"
#include <string.h>
static ForearmMappingContext g_forearm;
static HumanForearmTarget g_output;
static PoseMappingContext g_gripper;
int agent1_forearm_stage_init(void)
{
    memset(&g_output, 0, sizeof(g_output));
    agent1_forearm_stage_reset_gripper();
    return forearm_mapping_init(&g_forearm);
}
int agent1_forearm_stage_run(const HumanPose2D *pose, PoseArmSide side, float dt)
{
    return forearm_mapping_update(&g_forearm, pose, side, dt, &g_output);
}
const HumanForearmTarget *agent1_forearm_stage_output(void) { return &g_output; }
float agent1_forearm_stage_gripper_distance_px(void)
{
    return g_gripper.gripper_ratio_used * g_gripper.gripper_hand_span_px;
}

void agent1_forearm_stage_reset_gripper(void)
{
    memset(&g_gripper, 0, sizeof(g_gripper));
}
int agent1_forearm_stage_update_gripper(const HumanPose2D *pose, float *norm)
{
    if (pose == NULL || norm == NULL) return 0;
    if (g_gripper.last_frame_id_valid && g_gripper.last_frame_id == pose->frame_id) return 0;
    g_gripper.last_frame_id = pose->frame_id;
    g_gripper.last_frame_id_valid = 1U;
    g_gripper.gripper_input = pose->gripper_2d;
    if (pose->gripper_2d.source != 1U && pose->gripper_2d.source != 2U) return 0;
    return pm_update_gripper_from_2d(&g_gripper, PM_MIN_SHOULDER_WIDTH_PX, norm) == 0;
}
int agent1_forearm_stage_gripper_fresh(void)
{
    const PoseMappingContext *pose = &g_forearm.pose;
    return !pose->gripper_last_hold && pose->last_frame_id_valid &&
        pose->gripper_last_update_frame_valid &&
        pose->gripper_last_update_frame_id == pose->last_frame_id;
}
int agent1_forearm_stage_run_stereo(const HumanPose2D *image_pose,
                                   const HumanPose3D *measured_pose, PoseArmSide side, float dt)
{
    return forearm_mapping_update_stereo(&g_forearm, image_pose, measured_pose, side, dt, &g_output);
}
int agent1_forearm_stage_start_roll_zero_calibration(void)
{
    return pose_mapping_start_roll_zero_calibration(&g_forearm.pose);
}
void agent1_forearm_stage_clear_roll_zero(void)
{
    pose_mapping_clear_roll_zero(&g_forearm.pose);
}
#ifdef ROBOT_TRACE
const ForearmMappingContext *agent1_forearm_stage_debug_context(void) { return &g_forearm; }
#endif
