#ifndef INTEGRATION_HARNESS_H
#define INTEGRATION_HARNESS_H
#include "integration/agent_pipeline.h"
#include "integration/cnn_app.h"
#include "integration/stereo_board.h"
#include "record_replay/motion_record_replay.h"

typedef struct {
    int local_follow, remote_follow, foreign_request, board_ready;
    int frame_ready, target_ready, gripper_ready, gripper_new_session;
    int stationary, wrist_fresh;
    uint32_t epoch;
    HumanPose2D image, gripper;
    HumanPose3D measured;
    HumanForearmTarget target, sent_target;
    unsigned set_local_calls, set_remote_calls, target_sends, gripper_sends;
    unsigned target_takes, gripper_takes, stereo_takes, mono_takes;
    unsigned a1_calls, a2_calls, gripper_calls, reset_calls;
    uint32_t sent_epoch;
    int sent_stationary, sent_wrist_fresh;
    char last_result[128];
} HarnessMock;
extern HarnessMock mock;
extern AgentPipelineContext *active_pipeline;
extern MotionRecordReplay *active_controller;
void harness_mock_reset(void);
void harness_make_frame(uint32_t frame_id, int fingers);
void harness_iteration_begin(void);
void harness_iteration_end(void);
int harness_storage_write_allowed(void);
#endif
