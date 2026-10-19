#ifndef ROBOT_BOARD_INPUT_H
#define ROBOT_BOARD_INPUT_H

#include <stdint.h>
#include "app/robot/control/robot_pipeline.h"
#include "app/robot/record_replay/motion_record_replay.h"

#if defined(ROBOT_STEREO_LEFT) == defined(ROBOT_STEREO_RIGHT)
#error "Select exactly one LEFT or RIGHT firmware role."
#endif

typedef struct {
    unsigned uart_role, forearm_mm, tip_mm;
    uint16_t gripper_open_min_us, gripper_close_max_us;
    const char *side, *arm_name;
    const char *follow_status, *follow_enabled, *follow_disabled, *follow_help;
} BoardInputProfile;

const BoardInputProfile *board_input_profile(void);
int board_input_set_follow(int enabled);
int board_input_follow_enabled(void);
int board_input_remote_requested(void);
void board_input_stop_follow(void);
void board_input_update(RobotPipeline *pipeline, MotionRecordReplay *record_replay);

#endif
