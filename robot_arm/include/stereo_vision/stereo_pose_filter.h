#ifndef STEREO_VISION_POSE_FILTER_H
#define STEREO_VISION_POSE_FILTER_H

#include "common/robot_types.h"
#include "stereo_vision/stereo_one_euro.h"

#define STEREO_POSE_FILTER_POINT_COUNT 4U

typedef struct {
    StereoOneEuro euro[STEREO_POSE_FILTER_POINT_COUNT];
    uint64_t time_us;
} StereoPoseFilter;

void stereo_pose_filter_init(StereoPoseFilter *state);
void stereo_pose_filter_observe_invalid(StereoPoseFilter *state, const HumanPose3D *pose);
int stereo_pose_filter_apply(StereoPoseFilter *state, HumanPose3D *pose, float dt_sec);

#endif
