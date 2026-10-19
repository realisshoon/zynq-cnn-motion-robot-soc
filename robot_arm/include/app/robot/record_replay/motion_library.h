#ifndef RECORD_REPLAY_MOTION_LIBRARY_H
#define RECORD_REPLAY_MOTION_LIBRARY_H

#include <stdint.h>
#include "app/cnn/cnn_app.h"
#include "app/robot/record_replay/motion_record_replay.h"

#define MOTION_LIBRARY_SLOTS 32U
#define MOTION_LIBRARY_NAME_MAX 24U

void motion_library_init(void);
int motion_library_feed(uint8_t byte, int menu_active);
int motion_library_input_active(void);
void motion_library_abort_input(void);
int motion_library_event(MotionRecordReplay *controller, RobotPipeline *pipeline,
                         CnnAppEvent event);
int motion_library_input_allowed(void);
int motion_library_record_preparing(void);
int motion_library_virtual_allowed(const MotionRecordReplay *controller);
void motion_library_tick(MotionRecordReplay *controller, RobotPipeline *pipeline);
int motion_library_service(MotionRecordReplay *controller, RobotPipeline *pipeline,
                           uint32_t tick_overruns);
void motion_library_status(void);
void motion_library_report_state(const MotionRecordReplay *controller,
                                 const RobotPipeline *pipeline, int force);

#endif
