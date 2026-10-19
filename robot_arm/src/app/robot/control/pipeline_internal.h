#ifndef ROBOT_PIPELINE_INTERNAL_H
#define ROBOT_PIPELINE_INTERNAL_H

#include "app/robot/control/robot_pipeline.h"

extern const ForearmJointCommand robot_home_pose;

int robot_pipeline_same_command(const ForearmJointCommand *first, const ForearmJointCommand *second);
uint32_t robot_pipeline_time_us(const RobotPipeline *context);
float gripper_latch_update(GripperLatchState *latch, float desired,
                          float held, int fresh, uint32_t frame_id,
                          uint8_t source, uint32_t now_us);
void gripper_latch_applied(GripperLatchState *latch, float command, uint32_t now_us);
void gripper_motion_sample(GripperMotionHoldState *state,
                           const ForearmJointCommand *command, uint32_t now_us);

#endif
