#include "human_target_angle/agent1_forearm_stage.h"
#include <string.h>
static ForearmMappingContext g_forearm;
static HumanForearmTarget g_output;
int agent1_forearm_stage_init(Point3D up, Point3D forward, uint8_t calibrated)
{
    memset(&g_output, 0, sizeof(g_output));
    forearm_mapping_init(&g_forearm);
    return forearm_mapping_set_table(&g_forearm, up, forward, calibrated);
}
int agent1_forearm_stage_run(const HumanPose2D *pose, PoseArmSide side, float dt)
{
    return forearm_mapping_update(&g_forearm, pose, side, dt, &g_output);
}
const HumanForearmTarget *agent1_forearm_stage_output(void) { return &g_output; }
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
