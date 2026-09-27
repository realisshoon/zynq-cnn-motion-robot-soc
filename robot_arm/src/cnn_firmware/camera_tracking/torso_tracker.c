#include "torso_tracker.h"

#include "xil_printf.h"

static s32 abs_s32(s32 value)
{
    return value < 0 ? -value : value;
}

static s32 clamp_s32(s32 value, s32 low, s32 high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

static void hold_current_position(torso_tracker_t *tracker)
{
    camera_gimbal_pwm_set_target(tracker->gimbal, CAMERA_GIMBAL_PAN,
        tracker->gimbal->current_pulse_us[CAMERA_GIMBAL_PAN]);
    camera_gimbal_pwm_set_target(tracker->gimbal, CAMERA_GIMBAL_TILT,
        tracker->gimbal->current_pulse_us[CAMERA_GIMBAL_TILT]);
}

static int joint_is_usable(const torso_tracker_t *tracker,
                           const cnn_joint_t *joint)
{
    return joint->valid && joint->score >= tracker->config.minimum_score &&
           joint->x < tracker->config.frame_width &&
           joint->y < tracker->config.frame_height;
}

static s32 pair_average(u16 a, u16 b)
{
    return (s32)(((u32)a + (u32)b + 1U) / 2U);
}

static int derive_torso_center(torso_tracker_t *tracker,
                               const cnn_joint_t *joints[4],
                               const int usable[4],
                               s32 *center_x, s32 *center_y,
                               torso_observation_t *observation)
{
    unsigned int count = (unsigned int)usable[0] +
                         (unsigned int)usable[1] +
                         (unsigned int)usable[2] +
                         (unsigned int)usable[3];
    int shoulder_pair = usable[0] && usable[1];
    int hip_pair = usable[2] && usable[3];
    s32 shoulder_x = 0;
    s32 shoulder_y = 0;
    s32 hip_x = 0;
    s32 hip_y = 0;

    if (shoulder_pair) {
        shoulder_x = pair_average(joints[0]->x, joints[1]->x);
        shoulder_y = pair_average(joints[0]->y, joints[1]->y);
    }
    if (hip_pair) {
        hip_x = pair_average(joints[2]->x, joints[3]->x);
        hip_y = pair_average(joints[2]->y, joints[3]->y);
    }

    if (count == 4U) {
        *center_x = (shoulder_x + hip_x + 1) / 2;
        *center_y = (shoulder_y + hip_y + 1) / 2;
        tracker->hip_to_torso_x = *center_x - hip_x;
        tracker->hip_to_torso_y = *center_y - hip_y;
        tracker->shoulder_to_torso_x = *center_x - shoulder_x;
        tracker->shoulder_to_torso_y = *center_y - shoulder_y;
        tracker->hip_fallback_valid = 1U;
        tracker->shoulder_fallback_valid = 1U;
        *observation = TORSO_OBSERVATION_FULL_4;
    }
    else if (count == 3U && hip_pair) {
        const cnn_joint_t *shoulder = usable[0] ? joints[0] : joints[1];
        *center_x = hip_x;
        *center_y = ((s32)shoulder->y + hip_y + 1) / 2;
        tracker->hip_to_torso_x = *center_x - hip_x;
        tracker->hip_to_torso_y = *center_y - hip_y;
        tracker->hip_fallback_valid = 1U;
        *observation = TORSO_OBSERVATION_THREE;
    }
    else if (count == 3U && shoulder_pair) {
        const cnn_joint_t *hip = usable[2] ? joints[2] : joints[3];
        *center_x = shoulder_x;
        *center_y = (shoulder_y + (s32)hip->y + 1) / 2;
        tracker->shoulder_to_torso_x = *center_x - shoulder_x;
        tracker->shoulder_to_torso_y = *center_y - shoulder_y;
        tracker->shoulder_fallback_valid = 1U;
        *observation = TORSO_OBSERVATION_THREE;
    }
    else if (hip_pair && tracker->hip_fallback_valid) {
        *center_x = hip_x + tracker->hip_to_torso_x;
        *center_y = hip_y + tracker->hip_to_torso_y;
        *observation = TORSO_OBSERVATION_HIP_PAIR;
    }
    else if (shoulder_pair && tracker->shoulder_fallback_valid) {
        *center_x = shoulder_x + tracker->shoulder_to_torso_x;
        *center_y = shoulder_y + tracker->shoulder_to_torso_y;
        *observation = TORSO_OBSERVATION_SHOULDER_PAIR;
    }
    else {
        *observation = TORSO_OBSERVATION_NONE;
        return 0;
    }

    *center_x = clamp_s32(*center_x, 0, (s32)tracker->config.frame_width - 1);
    *center_y = clamp_s32(*center_y, 0, (s32)tracker->config.frame_height - 1);
    return 1;
}

static int derive_face_center(torso_tracker_t *tracker,
                              const cnn_result_t *result,
                              s32 *center_x, s32 *center_y,
                              torso_observation_t *observation)
{
    const u8 indices[5] = {
        tracker->config.nose,
        tracker->config.left_eye,
        tracker->config.right_eye,
        tracker->config.left_ear,
        tracker->config.right_ear
    };
    u32 sum_x = 0U;
    u32 sum_y = 0U;
    unsigned int count = 0U;
    unsigned int i;
    int nose_valid;

    nose_valid = joint_is_usable(tracker, &result->joint[indices[0]]);
    for (i = 0U; i < 5U; ++i) {
        const cnn_joint_t *joint = &result->joint[indices[i]];
        if (joint_is_usable(tracker, joint)) {
            sum_x += joint->x;
            sum_y += joint->y;
            count++;
        }
    }

    /* A nose is a safe single-point face center. Without it, require a pair. */
    if (count == 0U || (!nose_valid && count < 2U)) {
        *observation = TORSO_OBSERVATION_NONE;
        return 0;
    }

    *center_x = (s32)((sum_x + count / 2U) / count);
    *center_y = (s32)((sum_y + count / 2U) / count);
    *center_x = clamp_s32(*center_x, 0, (s32)tracker->config.frame_width - 1);
    *center_y = clamp_s32(*center_y, 0, (s32)tracker->config.frame_height - 1);
    if (count == 5U)
        *observation = TORSO_OBSERVATION_FACE_ALL;
    else if (count == 1U)
        *observation = TORSO_OBSERVATION_FACE_NOSE;
    else
        *observation = TORSO_OBSERVATION_FACE_PARTIAL;
    return 1;
}

static s32 control_delta(s32 error, u16 deadband, u16 gain_div,
                         u16 max_delta, int invert)
{
    s32 delta;

    if (abs_s32(error) <= (s32)deadband)
        return 0;
    if (gain_div == 0U)
        gain_div = 1U;
    delta = error / (s32)gain_div;
    if (delta == 0)
        delta = error < 0 ? -1 : 1;
    delta = clamp_s32(delta, -(s32)max_delta, (s32)max_delta);
    return invert ? -delta : delta;
}

static torso_tracker_result_t search_from_body_pair(
    torso_tracker_t *tracker, s32 pair_x, s32 pair_y,
    torso_observation_t observation, int search_up)
{
    s32 delta_pan;
    s32 delta_tilt;
    s32 pan_target;
    s32 tilt_target;

    tracker->raw_x = pair_x;
    tracker->raw_y = pair_y;
    tracker->filtered_x = pair_x;
    tracker->filtered_y = pair_y;
    tracker->error_x = pair_x - (s32)tracker->config.target_x;
    tracker->error_y = 0;
    tracker->filter_valid = 0U;
    tracker->consecutive_lost = 0U;
    tracker->last_observation = observation;

    if (tracker->pending_search != observation) {
        tracker->pending_search = observation;
        tracker->pending_search_frames = 1U;
    }
    else if (tracker->pending_search_frames != 0xFFU) {
        tracker->pending_search_frames++;
    }

    if (tracker->pending_search_frames < tracker->config.search_confirm_frames) {
        hold_current_position(tracker);
        return TORSO_TRACK_HELD;
    }

    delta_pan = control_delta(tracker->error_x, tracker->config.deadband_x,
                              tracker->config.gain_div_x,
                              tracker->config.max_command_delta_us,
                              tracker->config.pan_invert);
    delta_tilt = search_up ? -(s32)tracker->config.search_step_us
                           :  (s32)tracker->config.search_step_us;
    if (tracker->config.tilt_invert)
        delta_tilt = -delta_tilt;

    pan_target = (s32)tracker->gimbal->target_pulse_us[CAMERA_GIMBAL_PAN] +
                 delta_pan;
    tilt_target = (s32)tracker->gimbal->target_pulse_us[CAMERA_GIMBAL_TILT] +
                  delta_tilt;
    camera_gimbal_pwm_set_target(tracker->gimbal, CAMERA_GIMBAL_PAN,
        (u16)clamp_s32(pan_target,
            tracker->gimbal->min_pulse_us[CAMERA_GIMBAL_PAN],
            tracker->gimbal->max_pulse_us[CAMERA_GIMBAL_PAN]));
    camera_gimbal_pwm_set_target(tracker->gimbal, CAMERA_GIMBAL_TILT,
        (u16)clamp_s32(tilt_target,
            tracker->gimbal->min_pulse_us[CAMERA_GIMBAL_TILT],
            tracker->gimbal->max_pulse_us[CAMERA_GIMBAL_TILT]));
    camera_gimbal_pwm_service(tracker->gimbal);
    tracker->fallback_frames++;
    return TORSO_TRACK_UPDATED;
}

void torso_tracker_default_config(torso_tracker_config_t *config)
{
    if (config == 0)
        return;
    config->target_mode = (u8)TORSO_TRACK_TARGET_TORSO;
    config->frame_width = 1280U;
    config->frame_height = 720U;
    config->target_x = 640U;
    config->target_y = 360U;
    config->deadband_x = 50U;
    config->deadband_y = 40U;
    config->jump_limit_px = 220U;
    config->gain_div_x = 16U;
    config->gain_div_y = 16U;
    config->max_command_delta_us = 12U;
    config->search_step_us = 5U;
    config->filter_shift = 2U;       /* IIR alpha = 1/4. */
    config->lost_hold_frames = 15U;
    config->search_confirm_frames = 3U;
    config->pan_invert = 0U;
    /* This camera bracket increases elevation when the tilt pulse increases. */
    config->tilt_invert = 0U;
    config->minimum_score = 0;
    config->left_shoulder = (u8)CNN_JOINT_LEFT_SHOULDER;
    config->right_shoulder = (u8)CNN_JOINT_RIGHT_SHOULDER;
    config->left_hip = (u8)CNN_JOINT_LEFT_HIP;
    config->right_hip = (u8)CNN_JOINT_RIGHT_HIP;
    config->nose = (u8)CNN_JOINT_NOSE;
    config->left_eye = (u8)CNN_JOINT_LEFT_EYE;
    config->right_eye = (u8)CNN_JOINT_RIGHT_EYE;
    config->left_ear = (u8)CNN_JOINT_LEFT_EAR;
    config->right_ear = (u8)CNN_JOINT_RIGHT_EAR;
}

void torso_tracker_init(torso_tracker_t *tracker, camera_gimbal_pwm_t *gimbal,
                        const torso_tracker_config_t *config)
{
    if (tracker == 0)
        return;
    tracker->gimbal = gimbal;
    tracker->config = *config;
    tracker->filtered_x = 0;
    tracker->filtered_y = 0;
    tracker->raw_x = 0;
    tracker->raw_y = 0;
    tracker->error_x = 0;
    tracker->error_y = 0;
    tracker->hip_to_torso_x = 0;
    tracker->hip_to_torso_y = 0;
    tracker->shoulder_to_torso_x = 0;
    tracker->shoulder_to_torso_y = 0;
    tracker->last_frame_id = 0U;
    tracker->accepted_frames = 0U;
    tracker->full_frames = 0U;
    tracker->fallback_frames = 0U;
    tracker->rejected_frames = 0U;
    tracker->lost_frames_total = 0U;
    tracker->last_observation = TORSO_OBSERVATION_NONE;
    tracker->consecutive_lost = 0U;
    tracker->filter_valid = 0U;
    tracker->hip_fallback_valid = 0U;
    tracker->shoulder_fallback_valid = 0U;
    tracker->pending_search = TORSO_OBSERVATION_NONE;
    tracker->pending_search_frames = 0U;
    tracker->enabled = 0U;
}

void torso_tracker_set_enable(torso_tracker_t *tracker, int enabled)
{
    if (tracker == 0)
        return;
    tracker->enabled = enabled ? 1U : 0U;
    if (!tracker->enabled) {
        tracker->filter_valid = 0U;
        tracker->consecutive_lost = 0U;
        tracker->hip_fallback_valid = 0U;
        tracker->shoulder_fallback_valid = 0U;
        tracker->pending_search = TORSO_OBSERVATION_NONE;
        tracker->pending_search_frames = 0U;
        tracker->last_observation = TORSO_OBSERVATION_NONE;
    }
}

torso_tracker_result_t torso_tracker_process(torso_tracker_t *tracker,
                                             const cnn_result_t *result)
{
    const cnn_joint_t *joints[4];
    int usable[4];
    torso_observation_t observation;
    s32 delta_pan;
    s32 delta_tilt;
    s32 pan_target;
    s32 tilt_target;
    unsigned int i;

    if (tracker == 0 || result == 0 || tracker->gimbal == 0)
        return TORSO_TRACK_LOST;
    if (!tracker->enabled)
        return TORSO_TRACK_DISABLED;
    if (tracker->config.target_mode > (u8)TORSO_TRACK_TARGET_TORSO ||
        tracker->config.filter_shift > 8U)
        return TORSO_TRACK_LOST;
    if (tracker->config.target_mode == (u8)TORSO_TRACK_TARGET_FACE &&
        (tracker->config.nose >= CNN_JOINT_COUNT ||
         tracker->config.left_eye >= CNN_JOINT_COUNT ||
         tracker->config.right_eye >= CNN_JOINT_COUNT ||
         tracker->config.left_ear >= CNN_JOINT_COUNT ||
         tracker->config.right_ear >= CNN_JOINT_COUNT))
        return TORSO_TRACK_LOST;
    if (tracker->config.target_mode == (u8)TORSO_TRACK_TARGET_TORSO &&
        (tracker->config.left_shoulder >= CNN_JOINT_COUNT ||
         tracker->config.right_shoulder >= CNN_JOINT_COUNT ||
         tracker->config.left_hip >= CNN_JOINT_COUNT ||
         tracker->config.right_hip >= CNN_JOINT_COUNT))
        return TORSO_TRACK_LOST;
    if (tracker->last_frame_id == result->frame_id && tracker->accepted_frames != 0U)
        return TORSO_TRACK_DUPLICATE;
    tracker->last_frame_id = result->frame_id;

    if (tracker->config.target_mode == (u8)TORSO_TRACK_TARGET_FACE) {
        if (!derive_face_center(tracker, result,
                                &tracker->raw_x, &tracker->raw_y,
                                &observation))
            observation = TORSO_OBSERVATION_NONE;
    }
    else {
        int shoulder_pair;
        int hip_pair;
        joints[0] = &result->joint[tracker->config.left_shoulder];
        joints[1] = &result->joint[tracker->config.right_shoulder];
        joints[2] = &result->joint[tracker->config.left_hip];
        joints[3] = &result->joint[tracker->config.right_hip];
        for (i = 0U; i < 4U; ++i)
            usable[i] = joint_is_usable(tracker, joints[i]);
        shoulder_pair = usable[0] && usable[1];
        hip_pair = usable[2] && usable[3];
        if (!derive_torso_center(tracker, joints, usable,
                                 &tracker->raw_x, &tracker->raw_y,
                                 &observation)) {
            if (hip_pair && !shoulder_pair) {
                return search_from_body_pair(
                    tracker,
                    pair_average(joints[2]->x, joints[3]->x),
                    pair_average(joints[2]->y, joints[3]->y),
                    TORSO_OBSERVATION_HIP_SEARCH, 1);
            }
            if (shoulder_pair && !hip_pair) {
                return search_from_body_pair(
                    tracker,
                    pair_average(joints[0]->x, joints[1]->x),
                    pair_average(joints[0]->y, joints[1]->y),
                    TORSO_OBSERVATION_SHOULDER_SEARCH, 0);
            }
            observation = TORSO_OBSERVATION_NONE;
        }
    }

    if (observation == TORSO_OBSERVATION_NONE) {
        tracker->last_observation = TORSO_OBSERVATION_NONE;
        tracker->pending_search = TORSO_OBSERVATION_NONE;
        tracker->pending_search_frames = 0U;
        if (tracker->consecutive_lost != 0xFFU)
            tracker->consecutive_lost++;
        tracker->lost_frames_total++;
        if (tracker->consecutive_lost <= tracker->config.lost_hold_frames) {
            hold_current_position(tracker);
            return TORSO_TRACK_HELD;
        }

        /*
         * Once the tracked body has been absent for several frames, return
         * toward the calibrated center.  Drop the old filter state so a body
         * found far from the stale point can be reacquired.
         */
        tracker->filter_valid = 0U;
        camera_gimbal_pwm_set_target(tracker->gimbal, CAMERA_GIMBAL_PAN,
            tracker->gimbal->center_pulse_us[CAMERA_GIMBAL_PAN]);
        camera_gimbal_pwm_set_target(tracker->gimbal, CAMERA_GIMBAL_TILT,
            tracker->gimbal->center_pulse_us[CAMERA_GIMBAL_TILT]);
        camera_gimbal_pwm_service(tracker->gimbal);
        return TORSO_TRACK_LOST;
    }
    tracker->last_observation = observation;
    tracker->pending_search = TORSO_OBSERVATION_NONE;
    tracker->pending_search_frames = 0U;

    if (tracker->filter_valid &&
        (abs_s32(tracker->raw_x - tracker->filtered_x) >
             (s32)tracker->config.jump_limit_px ||
         abs_s32(tracker->raw_y - tracker->filtered_y) >
             (s32)tracker->config.jump_limit_px)) {
        tracker->rejected_frames++;
        if (tracker->consecutive_lost != 0xFFU)
            tracker->consecutive_lost++;
        hold_current_position(tracker);
        return TORSO_TRACK_HELD;
    }

    if (!tracker->filter_valid) {
        tracker->filtered_x = tracker->raw_x;
        tracker->filtered_y = tracker->raw_y;
        tracker->filter_valid = 1U;
    }
    else {
        u32 divisor = 1U << tracker->config.filter_shift;
        tracker->filtered_x += (tracker->raw_x - tracker->filtered_x) /
                               (s32)divisor;
        tracker->filtered_y += (tracker->raw_y - tracker->filtered_y) /
                               (s32)divisor;
    }

    tracker->consecutive_lost = 0U;
    tracker->accepted_frames++;
    if (observation == TORSO_OBSERVATION_FULL_4 ||
        observation == TORSO_OBSERVATION_FACE_ALL)
        tracker->full_frames++;
    else
        tracker->fallback_frames++;
    tracker->error_x = tracker->filtered_x - (s32)tracker->config.target_x;
    tracker->error_y = tracker->filtered_y - (s32)tracker->config.target_y;

    delta_pan = control_delta(tracker->error_x, tracker->config.deadband_x,
                              tracker->config.gain_div_x,
                              tracker->config.max_command_delta_us,
                              tracker->config.pan_invert);
    delta_tilt = control_delta(tracker->error_y, tracker->config.deadband_y,
                               tracker->config.gain_div_y,
                               tracker->config.max_command_delta_us,
                               tracker->config.tilt_invert);

    pan_target = (s32)tracker->gimbal->target_pulse_us[CAMERA_GIMBAL_PAN] +
                 delta_pan;
    tilt_target = (s32)tracker->gimbal->target_pulse_us[CAMERA_GIMBAL_TILT] +
                  delta_tilt;
    camera_gimbal_pwm_set_target(tracker->gimbal, CAMERA_GIMBAL_PAN,
        (u16)clamp_s32(pan_target,
            tracker->gimbal->min_pulse_us[CAMERA_GIMBAL_PAN],
            tracker->gimbal->max_pulse_us[CAMERA_GIMBAL_PAN]));
    camera_gimbal_pwm_set_target(tracker->gimbal, CAMERA_GIMBAL_TILT,
        (u16)clamp_s32(tilt_target,
            tracker->gimbal->min_pulse_us[CAMERA_GIMBAL_TILT],
            tracker->gimbal->max_pulse_us[CAMERA_GIMBAL_TILT]));
    camera_gimbal_pwm_service(tracker->gimbal);

    return delta_pan == 0 && delta_tilt == 0
        ? TORSO_TRACK_DEADBAND : TORSO_TRACK_UPDATED;
}

const char *torso_tracker_result_string(torso_tracker_result_t result)
{
    switch (result) {
    case TORSO_TRACK_DISABLED: return "disabled";
    case TORSO_TRACK_UPDATED: return "updated";
    case TORSO_TRACK_DEADBAND: return "deadband";
    case TORSO_TRACK_HELD: return "held";
    case TORSO_TRACK_LOST: return "lost";
    case TORSO_TRACK_DUPLICATE: return "duplicate";
    default: return "unknown";
    }
}

const char *torso_tracker_observation_string(torso_observation_t observation)
{
    switch (observation) {
    case TORSO_OBSERVATION_FULL_4: return "full4";
    case TORSO_OBSERVATION_THREE: return "three";
    case TORSO_OBSERVATION_HIP_PAIR: return "hip-pair";
    case TORSO_OBSERVATION_SHOULDER_PAIR: return "shoulder-pair";
    case TORSO_OBSERVATION_HIP_SEARCH: return "search-up-from-hips";
    case TORSO_OBSERVATION_SHOULDER_SEARCH: return "search-down-from-shoulders";
    case TORSO_OBSERVATION_FACE_ALL: return "face-all";
    case TORSO_OBSERVATION_FACE_PARTIAL: return "face-partial";
    case TORSO_OBSERVATION_FACE_NOSE: return "face-nose";
    case TORSO_OBSERVATION_NONE: return "none";
    default: return "unknown";
    }
}

void torso_tracker_print(const torso_tracker_t *tracker)
{
    if (tracker == 0)
        return;
    xil_printf("camera tracker mode=%s enabled=%u\r\n",
               tracker->config.target_mode == (u8)TORSO_TRACK_TARGET_FACE
                   ? "face" : "torso",
               tracker->enabled);
    if (tracker->config.target_mode == (u8)TORSO_TRACK_TARGET_FACE)
        xil_printf("  joints N/LE/RE/LEAR/REAR=%u/%u/%u/%u/%u\r\n",
                   tracker->config.nose, tracker->config.left_eye,
                   tracker->config.right_eye, tracker->config.left_ear,
                   tracker->config.right_ear);
    else
        xil_printf("  joints LS/RS/LH/RH=%u/%u/%u/%u\r\n",
                   tracker->config.left_shoulder,
                   tracker->config.right_shoulder, tracker->config.left_hip,
                   tracker->config.right_hip);
    xil_printf("  raw=%ld,%ld filtered=%ld,%ld target=%u,%u error=%ld,%ld\r\n",
               (long)tracker->raw_x, (long)tracker->raw_y,
               (long)tracker->filtered_x, (long)tracker->filtered_y,
               tracker->config.target_x, tracker->config.target_y,
               (long)tracker->error_x, (long)tracker->error_y);
    xil_printf("  deadband=%u/%u filter=1/%u jump=%u gain=%u/%u max-delta=%u\r\n",
               tracker->config.deadband_x, tracker->config.deadband_y,
               1U << tracker->config.filter_shift,
               tracker->config.jump_limit_px, tracker->config.gain_div_x,
               tracker->config.gain_div_y,
               tracker->config.max_command_delta_us);
    xil_printf("  pair search step/confirm=%u us/%u frames\r\n",
               tracker->config.search_step_us,
               tracker->config.search_confirm_frames);
    xil_printf("  direction invert pan/tilt=%u/%u\r\n",
               tracker->config.pan_invert, tracker->config.tilt_invert);
    xil_printf("  frames accepted/rejected/lost=%lu/%lu/%lu consecutive-lost=%u\r\n",
               (unsigned long)tracker->accepted_frames,
               (unsigned long)tracker->rejected_frames,
               (unsigned long)tracker->lost_frames_total,
               tracker->consecutive_lost);
    xil_printf("  observation=%s full/fallback=%lu/%lu pair-cache H/S=%u/%u\r\n",
               torso_tracker_observation_string(tracker->last_observation),
               (unsigned long)tracker->full_frames,
               (unsigned long)tracker->fallback_frames,
               tracker->hip_fallback_valid,
               tracker->shoulder_fallback_valid);
}
