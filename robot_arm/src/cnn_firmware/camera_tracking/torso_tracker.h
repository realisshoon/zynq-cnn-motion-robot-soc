#ifndef TORSO_TRACKER_H
#define TORSO_TRACKER_H

#include "camera_gimbal_pwm.h"
#include "../cnn/cnn_types.h"

typedef struct {
    u8 target_mode;
    u16 frame_width;
    u16 frame_height;
    u16 target_x;
    u16 target_y;
    u16 deadband_x;
    u16 deadband_y;
    u16 jump_limit_px;
    u16 gain_div_x;
    u16 gain_div_y;
    u16 max_command_delta_us;
    u16 search_step_us;
    u8 filter_shift;
    u8 lost_hold_frames;
    u8 search_confirm_frames;
    u8 pan_invert;
    u8 tilt_invert;
    s8 minimum_score;
    u8 left_shoulder;
    u8 right_shoulder;
    u8 left_hip;
    u8 right_hip;
    u8 nose;
    u8 left_eye;
    u8 right_eye;
    u8 left_ear;
    u8 right_ear;
} torso_tracker_config_t;

typedef enum {
    TORSO_TRACK_TARGET_FACE = 0,
    TORSO_TRACK_TARGET_TORSO = 1
} torso_tracker_target_t;

typedef enum {
    TORSO_TRACK_DISABLED = 0,
    TORSO_TRACK_UPDATED,
    TORSO_TRACK_DEADBAND,
    TORSO_TRACK_HELD,
    TORSO_TRACK_LOST,
    TORSO_TRACK_DUPLICATE
} torso_tracker_result_t;

typedef enum {
    TORSO_OBSERVATION_NONE = 0,
    TORSO_OBSERVATION_FULL_4,
    TORSO_OBSERVATION_THREE,
    TORSO_OBSERVATION_HIP_PAIR,
    TORSO_OBSERVATION_SHOULDER_PAIR,
    TORSO_OBSERVATION_HIP_SEARCH,
    TORSO_OBSERVATION_SHOULDER_SEARCH,
    TORSO_OBSERVATION_FACE_ALL,
    TORSO_OBSERVATION_FACE_PARTIAL,
    TORSO_OBSERVATION_FACE_NOSE
} torso_observation_t;

typedef struct {
    torso_tracker_config_t config;
    camera_gimbal_pwm_t *gimbal;
    s32 filtered_x;
    s32 filtered_y;
    s32 raw_x;
    s32 raw_y;
    s32 error_x;
    s32 error_y;
    s32 hip_to_torso_x;
    s32 hip_to_torso_y;
    s32 shoulder_to_torso_x;
    s32 shoulder_to_torso_y;
    u32 last_frame_id;
    u32 accepted_frames;
    u32 full_frames;
    u32 fallback_frames;
    u32 rejected_frames;
    u32 lost_frames_total;
    torso_observation_t last_observation;
    u8 consecutive_lost;
    u8 filter_valid;
    u8 hip_fallback_valid;
    u8 shoulder_fallback_valid;
    torso_observation_t pending_search;
    u8 pending_search_frames;
    u8 enabled;
} torso_tracker_t;

void torso_tracker_default_config(torso_tracker_config_t *config);
void torso_tracker_init(torso_tracker_t *tracker, camera_gimbal_pwm_t *gimbal,
                        const torso_tracker_config_t *config);
void torso_tracker_set_enable(torso_tracker_t *tracker, int enabled);
torso_tracker_result_t torso_tracker_process(torso_tracker_t *tracker,
                                             const cnn_result_t *result);
const char *torso_tracker_result_string(torso_tracker_result_t result);
const char *torso_tracker_observation_string(torso_observation_t observation);
void torso_tracker_print(const torso_tracker_t *tracker);

#endif
