#ifndef ROBOT_CONTROL_COMMANDS_H
#define ROBOT_CONTROL_COMMANDS_H

#include "app/cnn/cnn_app.h"
#include "app/robot/control/robot_pipeline.h"
#include "app/robot/record_replay/motion_record_replay.h"

void control_commands_service_virtual(RobotPipeline *pipeline, MotionRecordReplay *record_replay);
void control_commands_handle(MotionRecordReplay *record_replay, RobotPipeline *pipeline, CnnAppEvent event);

#endif
