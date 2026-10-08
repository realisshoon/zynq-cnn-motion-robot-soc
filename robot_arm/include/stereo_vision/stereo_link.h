#ifndef STEREO_LINK_H
#define STEREO_LINK_H

#include "stereo_vision/stereo_uart_protocol.h"

#define STEREO_LINK_QUEUE 8U
#define STEREO_LINK_POINTS (STEREO_UART_JOINTS + STEREO_UART_MARKERS)
#define STEREO_LINK_MAX_AGE_US 250000U
#define STEREO_LINK_MAX_RECEIVE_GAP_US 100000U
#define STEREO_LINK_MAX_SKEW_US 1000U
#define STEREO_LINK_PIXEL_TAU_US 100000U
#define STEREO_LINK_FILTER_RESET_US 500000U
#define STEREO_LINK_PIXEL_RESET_PX 30.0f
#define STEREO_LINK_REACQUIRE_SAMPLES 3U
#define STEREO_LINK_REACQUIRE_CLUSTER_PX 30.0f
#define STEREO_LINK_WRIST_MEDIAN_SAMPLES 3U
#define STEREO_LINK_MOTION_HISTORY 32U
#define STEREO_LINK_MOTION_WINDOW_US 500000U
#define STEREO_LINK_MOTION_MIN_SPAN_US 400000U
#define STEREO_LINK_STILL_RMS_PX 6.0f
#define STEREO_LINK_STILL_DRIFT_PX 8.0f
#define STEREO_LINK_MOVE_RMS_PX 9.0f
#define STEREO_LINK_MOVE_DRIFT_PX 12.0f
#define STEREO_LINK_MOVE_ANCHOR_PX 10.0f
#define STEREO_LINK_MOVE_IMMEDIATE_PX 20.0f
#define STEREO_LINK_MOTION_CONFIRM_SAMPLES 2U
#define STEREO_LINK_MOTION_TREND_MIN_PX_S 1.0f
#define STEREO_LINK_MOTION_TREND_COHERENCE 0.8f
#ifndef STEREO_LINK_RED_STILL
#define STEREO_LINK_RED_STILL 1
#endif
#define STEREO_LINK_RED_ENTER_PX 12.0f
#define STEREO_LINK_RED_EXIT_PX 20.0f
#define STEREO_LINK_RED_CLUSTER_PX 8.0f
#define STEREO_LINK_RED_CANDIDATES 3U
#define STEREO_LINK_RED_MIN_SPAN_US 150000U
#define STEREO_LINK_RED_MOTION_DISTANCE_PX 35.0f
#define STEREO_LINK_RED_MOTION_NET_PX 15.0f
#define STEREO_LINK_RED_MOTION_COHERENCE 0.85f
#define STEREO_LINK_RED_MOTION_STEP_PX 30.0f
#ifndef STEREO_LINK_MARKER_SPAN_FLOOR_PX
#define STEREO_LINK_MARKER_SPAN_FLOOR_PX 80.0f
#endif
#ifndef STEREO_LINK_MARKER_MAX_SPAN_RATIO
#define STEREO_LINK_MARKER_MAX_SPAN_RATIO 2.0f
#endif

typedef struct {
    Point2D anchor, previous;
    uint64_t received_us;
    uint32_t sequence;
    uint8_t count;
} StereoLinkCandidate;

typedef struct {
    StereoCoordinateFrame frame;
    uint64_t received_us;
    Point2D filtered[STEREO_LINK_POINTS];
} StereoLinkEntry;

typedef struct {
    uint32_t left_frame_id, right_frame_id;
    StereoPoint3D point[STEREO_LINK_POINTS];
    StereoStatus point_status[STEREO_LINK_POINTS];
    StereoPoseStatus control_status;
    StereoPoseDiagnostics diagnostics;
    HumanPose2D image_pose;
    HumanPose3D measured_pose;
    StereoFrameMetadata left_metadata, right_metadata;
    uint8_t time_verified;
    uint8_t async_test;
    StereoPoseStatus async_status;
    StereoPoseDiagnostics async_diagnostics;
    HumanPose3D async_measured_pose;
    uint64_t left_received_us, right_received_us;
    uint32_t left_session_id, right_session_id, left_sequence, right_sequence;
    uint8_t arm_stationary, arm_motion_count, arm_motion_trend;
    float arm_motion_rms_px, arm_motion_drift_px, arm_motion_anchor_px;
} StereoDepthResult;

typedef struct {
    StereoGeometryContext geometry;
    StereoLinkEntry queue[2][STEREO_LINK_QUEUE];
    unsigned count[2];
    uint32_t session[2], sequence[2];
    uint64_t last_exposure[2];
    uint32_t clock_epoch[2];
    uint8_t have_sequence[2], have_exposure[2];
    Point2D filtered[2][STEREO_LINK_POINTS];
    Point2D accepted_raw[2][STEREO_LINK_POINTS];
    uint64_t accepted_time[2][STEREO_LINK_POINTS];
    uint64_t accepted_received_us[2][STEREO_LINK_POINTS];
    uint8_t accepted_verified[2][STEREO_LINK_POINTS];
    StereoLinkCandidate candidate[2][2];
    Point2D wrist_history[2][STEREO_LINK_WRIST_MEDIAN_SAMPLES];
    uint64_t wrist_received_us[2];
    uint8_t wrist_history_count[2];
    Point2D arm_motion_history[STEREO_LINK_MOTION_HISTORY][4];
    Point2D arm_motion_anchor[4];
    uint64_t arm_motion_times[STEREO_LINK_MOTION_HISTORY];
    uint8_t arm_motion_count, arm_stationary, arm_quiet_count, arm_move_count;
    Point2D red_exit_points[STEREO_LINK_RED_CANDIDATES][2];
    uint64_t red_exit_times[STEREO_LINK_RED_CANDIDATES];
    uint32_t red_sessions[2], red_sequences[2];
    uint8_t red_exit_count;
    uint8_t reacquire[2][2];
    uint64_t filter_time[2];
    uint32_t pixel_tau_us;
    uint64_t last_observed_us;
    uint8_t have_filter_time[2], filter_verified[2];
    StereoDepthResult latest;
    uint32_t pairs, unsynchronized_pairs, rejected_frames, expired_frames, overwritten_results;
    uint32_t queue_overflows;
    uint32_t stale_frames, receive_gap_rejects, skipped_frames;
    uint32_t marker_rejects, jump_rejects, reacquired_points;
    uint8_t ready;
    uint8_t async_test_enabled;
    uint8_t gripper_source;
} StereoLink;

int stereo_link_init(StereoLink *link, const StereoCalibration *calibration);
int stereo_link_set_pixel_tau_us(StereoLink *link, uint32_t tau_us);
void stereo_link_set_async_test(StereoLink *link, int enabled);
HumanPose2D stereo_coordinate_pose(const StereoCoordinateFrame *frame);
int stereo_link_push(StereoLink *link, unsigned side,
                     const StereoCoordinateFrame *frame, uint64_t received_us);
int stereo_link_take(StereoLink *link, StereoDepthResult *result);
int stereo_link_take_at(StereoLink *link, StereoDepthResult *result, uint64_t now_us);

#endif
