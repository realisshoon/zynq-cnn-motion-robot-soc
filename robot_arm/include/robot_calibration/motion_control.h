#ifndef ROBOT_CALIBRATION_MOTION_CONTROL_H
#define ROBOT_CALIBRATION_MOTION_CONTROL_H

#include "common/robot_types.h"

int motion_control_validate_target(const HumanJointTarget *target);
void motion_control_map_target(const HumanJointTarget *input, JointCommand *output);
void motion_control_apply_limits(JointCommand *command);
#endif /* ROBOT_CALIBRATION_MOTION_CONTROL_H */
