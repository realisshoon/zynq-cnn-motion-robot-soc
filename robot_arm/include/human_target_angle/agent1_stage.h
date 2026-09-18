#ifndef AGENT1_STAGE_H
#define AGENT1_STAGE_H

#include "common/robot_types.h"
#include "human_target_angle/pose_mapping.h"

#ifdef __cplusplus
extern "C" {
#endif

int agent1_stage_init(void);

int agent1_stage_run(
    const HumanPose2D *pose,
    PoseArmSide active_arm,
    float dt_sec
);

const HumanJointTarget *agent1_stage_output(void);
int agent1_stage_output_valid(void);

#ifdef __cplusplus
}
#endif

#endif
