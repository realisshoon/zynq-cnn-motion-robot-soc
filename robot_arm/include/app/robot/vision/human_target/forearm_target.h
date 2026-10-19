#ifndef FOREARM_TARGET_H
#define FOREARM_TARGET_H
#include "app/robot/vision/human_target/forearm_mapping.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Explicit five-axis integration entry point. Legacy human_target_stage_* remains
 * six-axis until Agent2/3 migrate; there is no implicit semantic adapter. */
int forearm_target_init(void);
int forearm_target_run(const HumanPose2D *pose, PoseArmSide side, float dt);
int forearm_target_run_stereo(const HumanPose2D *image_pose,
                                   const HumanPose3D *measured_pose, PoseArmSide side, float dt);
const HumanForearmTarget *forearm_target_output(void);
int forearm_target_gripper_fresh(void);
void forearm_target_reset_gripper(void);
float forearm_target_gripper_distance_px(void);
int forearm_target_update_gripper(const HumanPose2D *pose, float *norm);
int forearm_target_start_roll_zero_calibration(void);
void forearm_target_clear_roll_zero(void);
const ForearmMappingContext *forearm_target_debug_context(void);
#ifdef __cplusplus
}
#endif
#endif
