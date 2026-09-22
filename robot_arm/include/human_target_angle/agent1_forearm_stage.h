#ifndef AGENT1_FOREARM_STAGE_H
#define AGENT1_FOREARM_STAGE_H
#include "human_target_angle/forearm_mapping.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Explicit five-axis integration entry point. Legacy agent1_stage_* remains
 * six-axis until Agent2/3 migrate; there is no implicit semantic adapter. */
int agent1_forearm_stage_init(Point3D up, Point3D forward, uint8_t calibrated);
int agent1_forearm_stage_run(const HumanPose2D *pose, PoseArmSide side, float dt);
const HumanForearmTarget *agent1_forearm_stage_output(void);
int agent1_forearm_stage_start_roll_zero_calibration(void);
void agent1_forearm_stage_clear_roll_zero(void);
#ifdef ROBOT_TRACE
const ForearmMappingContext *agent1_forearm_stage_debug_context(void);
#endif
#ifdef __cplusplus
}
#endif
#endif
