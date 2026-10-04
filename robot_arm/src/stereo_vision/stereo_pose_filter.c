#include "stereo_vision/stereo_pose_filter.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

void stereo_pose_filter_init(StereoPoseFilter *state)
{
    unsigned index;
    if (state == NULL) return;
    memset(state, 0, sizeof(*state));
    for (index = 0; index < STEREO_POSE_FILTER_POINT_COUNT; ++index)
        stereo_one_euro_init(&state->euro[index], stereo_one_euro_defaults());
}

static float separation(Point3D first, Point3D second)
{
    return hypotf(hypotf(first.x - second.x, first.y - second.y), first.z - second.z);
}

static void reset_point(StereoPoseFilter *state, unsigned index)
{
    stereo_one_euro_init(&state->euro[index], state->euro[index].config);
}

static void reset_all(StereoPoseFilter *state)
{
    unsigned index;
    for (index = 0; index < STEREO_POSE_FILTER_POINT_COUNT; ++index) reset_point(state, index);
    state->time_us = 0;
}

void stereo_pose_filter_observe_invalid(StereoPoseFilter *state, const HumanPose3D *pose)
{
    const Point3D *points[STEREO_POSE_FILTER_POINT_COUNT];
    unsigned index;
    if (state == NULL || pose == NULL) return;
    points[0] = &pose->elbow;
    points[1] = &pose->wrist;
    points[2] = &pose->finger1;
    points[3] = &pose->finger2;
    for (index = 0; index < STEREO_POSE_FILTER_POINT_COUNT; ++index)
        if (!pose->valid || !points[index]->valid || !isfinite(points[index]->x) ||
            !isfinite(points[index]->y) || !isfinite(points[index]->z)) reset_point(state, index);
}

int stereo_pose_filter_apply(StereoPoseFilter *state, HumanPose3D *pose, float dt_sec)
{
    Point3D *points[STEREO_POSE_FILTER_POINT_COUNT];
    unsigned index;
    uint64_t elapsed;
    if (state == NULL) return 0;
    if (pose == NULL) {
        reset_all(state);
        return 0;
    }
    if (!pose->valid || !isfinite(dt_sec) || dt_sec <= 0.0f || dt_sec > 3600.0f) {
        reset_all(state);
        memset(pose, 0, sizeof(*pose));
        return 0;
    }
    elapsed = (uint64_t)((double)dt_sec * 1000000.0 + 0.5);
    if (!elapsed || UINT64_MAX - state->time_us < elapsed) {
        reset_all(state);
        memset(pose, 0, sizeof(*pose));
        return 0;
    }
    state->time_us += elapsed;
    points[0] = &pose->elbow;
    points[1] = &pose->wrist;
    points[2] = &pose->finger1;
    points[3] = &pose->finger2;
    for (index = 0; index < STEREO_POSE_FILTER_POINT_COUNT; ++index) {
        float input[3] = {points[index]->x, points[index]->y, points[index]->z};
        float output[3] = {0.0f, 0.0f, 0.0f};
        int valid = stereo_one_euro_step(&state->euro[index], input, points[index]->valid,
                                        state->time_us, output);
        *points[index] = (Point3D){output[0], output[1], output[2], (uint8_t)valid};
    }
    pose->valid = pose->elbow.valid && pose->wrist.valid;
    if (pose->valid && (pose->elbow.z < 300.0f || pose->elbow.z > 3000.0f ||
        pose->wrist.z < 300.0f || pose->wrist.z > 3000.0f ||
        separation(pose->elbow, pose->wrist) < 80.0f ||
        separation(pose->elbow, pose->wrist) > 600.0f)) pose->valid = 0U;
    if (!pose->valid) {
        for (index = 0; index < STEREO_POSE_FILTER_POINT_COUNT; ++index) reset_point(state, index);
        return 0;
    }
    if (!pose->finger1.valid || !pose->finger2.valid ||
        separation(pose->wrist, pose->finger1) > 250.0f ||
        separation(pose->wrist, pose->finger2) > 250.0f ||
        separation(pose->finger1, pose->finger2) < 10.0f ||
        separation(pose->finger1, pose->finger2) > 250.0f) {
        memset(&pose->finger1, 0, sizeof(pose->finger1));
        memset(&pose->finger2, 0, sizeof(pose->finger2));
        reset_point(state, 2U);
        reset_point(state, 3U);
    }
    return 1;
}
