#ifndef RECORD_REPLAY_MOTION_LIBRARY_H
#define RECORD_REPLAY_MOTION_LIBRARY_H

#include <stdint.h>
#include "integration/cnn_app.h"
#include "record_replay/motion_record_replay.h"

#define MOTION_LIBRARY_SLOTS 32U
#define MOTION_LIBRARY_NAME_MAX 24U

void motion_library_init(void);
int motion_library_feed(uint8_t byte, int menu_active);
int motion_library_input_active(void);
void motion_library_abort_input(void);
int motion_library_event(MotionRecordReplay *controller, AgentPipelineContext *pipeline,
                         CnnAppEvent event);
int motion_library_input_allowed(void);
int motion_library_record_preparing(void);
void motion_library_tick(MotionRecordReplay *controller, AgentPipelineContext *pipeline);
int motion_library_service(MotionRecordReplay *controller, AgentPipelineContext *pipeline,
                           uint32_t tick_overruns);
void motion_library_status(void);
void motion_library_report_state(const MotionRecordReplay *controller,
                                 const AgentPipelineContext *pipeline, int force);

#endif
