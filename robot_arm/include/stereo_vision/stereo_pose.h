#ifndef STEREO_VISION_POSE_H
#define STEREO_VISION_POSE_H

#include "common/robot_types.h"
#include "stereo_vision/stereo_geometry.h"

typedef struct {
    uint64_t exposure_time_us;
    uint32_t shared_clock_epoch;
    uint8_t exposure_time_verified;
    uint8_t fixed_geometry_verified;
    uint8_t localization_quality_verified;
} StereoFrameMetadata;

typedef enum {
    STEREO_POSE_OK = 0,
    STEREO_POSE_ARGUMENT = -1,
    STEREO_POSE_UNVERIFIED = -2,
    STEREO_POSE_TIME_MISMATCH = -3,
    STEREO_POSE_MAJOR_INVALID = -4,
    STEREO_POSE_GEOMETRY = -5
} StereoPoseStatus;

typedef struct {
    StereoStatus point_status[6];
    uint64_t exposure_skew_us;
    uint8_t hand_valid;
} StereoPoseDiagnostics;

extern const StereoCalibration stereo_calibration_current;

StereoPoseStatus stereo_pose_reconstruct(const StereoGeometryContext *geometry,
    const HumanPose2D *left, const HumanPose2D *right,
    const StereoFrameMetadata *left_metadata, const StereoFrameMetadata *right_metadata,
    uint64_t max_exposure_skew_us, HumanPose3D *output, StereoPoseDiagnostics *diagnostics);
StereoPoseStatus stereo_pose_reconstruct_async_test(const StereoGeometryContext *geometry,
    const HumanPose2D *left, const HumanPose2D *right,
    HumanPose3D *output, StereoPoseDiagnostics *diagnostics);

#endif
