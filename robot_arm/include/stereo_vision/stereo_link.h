#ifndef STEREO_LINK_H
#define STEREO_LINK_H

#include "stereo_vision/stereo_uart_protocol.h"

#define STEREO_LINK_QUEUE 8U
#define STEREO_LINK_POINTS (STEREO_UART_JOINTS + STEREO_UART_MARKERS)
#define STEREO_LINK_MAX_AGE_US 250000U
#define STEREO_LINK_MAX_SKEW_US 1000U
#define STEREO_LINK_ASYNC_MAX_GAP_US 50000U
#define STEREO_LINK_ASYNC_MAX_AGE_US 100000U

typedef struct {
    StereoCoordinateFrame frame;
    uint64_t received_us;
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
} StereoDepthResult;

typedef struct {
    StereoGeometryContext geometry;
    StereoLinkEntry queue[2][STEREO_LINK_QUEUE];
    unsigned count[2];
    uint32_t session[2], sequence[2];
    uint64_t last_exposure[2];
    uint32_t clock_epoch[2];
    uint8_t have_sequence[2], have_exposure[2];
    StereoDepthResult latest;
    uint32_t pairs, unsynchronized_pairs, rejected_frames, expired_frames, overwritten_results;
    uint8_t ready;
    uint8_t async_test_enabled;
} StereoLink;

int stereo_link_init(StereoLink *link, const StereoCalibration *calibration);
void stereo_link_set_async_test(StereoLink *link, int enabled);
HumanPose2D stereo_coordinate_pose(const StereoCoordinateFrame *frame);
int stereo_link_push(StereoLink *link, unsigned side,
                     const StereoCoordinateFrame *frame, uint64_t received_us);
int stereo_link_take(StereoLink *link, StereoDepthResult *result);

#endif
