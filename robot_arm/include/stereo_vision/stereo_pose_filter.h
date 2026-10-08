#ifndef STEREO_VISION_POSE_FILTER_H
#define STEREO_VISION_POSE_FILTER_H

#include "common/robot_types.h"
#include "stereo_vision/stereo_one_euro.h"
#include "stereo_vision/g_kalman3d.h"

#define STEREO_POSE_FILTER_POINT_COUNT 4U

typedef struct {
    StereoKalman3D kalman[STEREO_POSE_FILTER_POINT_COUNT];
    uint64_t time_us;
} StereoPoseFilter;

void stereo_pose_filter_init(StereoPoseFilter *state);
int stereo_pose_filter_configure(StereoPoseFilter *state, StereoOneEuroConfig config);
int stereo_pose_filter_kalman_configure(StereoPoseFilter *state, StereoKalman3DConfig config);
int stereo_pose_filter_apply_at(StereoPoseFilter *state, HumanPose3D *pose, uint64_t time_us);
int stereo_pose_filter_apply(StereoPoseFilter *state, HumanPose3D *pose, float dt_sec);

#endif
