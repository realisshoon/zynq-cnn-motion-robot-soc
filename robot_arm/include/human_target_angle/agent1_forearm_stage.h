#ifndef AGENT1_FOREARM_STAGE_H
#define AGENT1_FOREARM_STAGE_H
#include "human_target_angle/forearm_mapping.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Explicit five-axis integration entry point. Legacy agent1_stage_* remains
 * six-axis until Agent2/3 migrate; there is no implicit semantic adapter. */
int agent1_forearm_stage_init(void);
int agent1_forearm_stage_run(const HumanPose2D *pose, PoseArmSide side, float dt);
int agent1_forearm_stage_run_stereo(const HumanPose2D *image_pose,
                                   const HumanPose3D *measured_pose, PoseArmSide side, float dt);
const HumanForearmTarget *agent1_forearm_stage_output(void);
int agent1_forearm_stage_gripper_fresh(void);
void agent1_forearm_stage_reset_gripper(void);
float agent1_forearm_stage_gripper_distance_px(void);
int agent1_forearm_stage_update_gripper(const HumanPose2D *pose, float *norm);
int agent1_forearm_stage_start_roll_zero_calibration(void);
void agent1_forearm_stage_clear_roll_zero(void);
#ifdef ROBOT_TRACE
const ForearmMappingContext *agent1_forearm_stage_debug_context(void);
#endif
#ifdef __cplusplus
}
#endif
#endif
