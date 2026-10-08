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
        stereo_kalman3d_init(&state->kalman[index], stereo_kalman3d_defaults());
}

int stereo_pose_filter_configure(StereoPoseFilter *state, StereoOneEuroConfig config)
{
    (void)state;
    (void)config;
    return 0;
}

static float separation(Point3D first, Point3D second)
{
    return hypotf(hypotf(first.x - second.x, first.y - second.y), first.z - second.z);
}

int stereo_pose_filter_kalman_configure(StereoPoseFilter *state, StereoKalman3DConfig config)
{
    unsigned axis, point;
    if (state == NULL) return 0;
    for (axis = 0U; axis < 3U; ++axis)
        if (!isfinite(config.measurement_std_mm[axis]) ||
            config.measurement_std_mm[axis] < 0.1f ||
            config.measurement_std_mm[axis] > 1000.0f) return 0;
    if (!isfinite(config.acceleration_std_mm_s2) ||
        config.acceleration_std_mm_s2 < 0.0f || config.acceleration_std_mm_s2 > 5000.0f ||
        !isfinite(config.initial_velocity_std_mm_s) ||
        config.initial_velocity_std_mm_s < 0.0f || config.initial_velocity_std_mm_s > 5000.0f)
        return 0;
    for (point = 0U; point < STEREO_POSE_FILTER_POINT_COUNT; ++point)
        state->kalman[point].config = config;
    return 1;
}

static void reset_point(StereoPoseFilter *state, unsigned index)
{
    stereo_kalman3d_init(&state->kalman[index], state->kalman[index].config);
}

static void reset_all(StereoPoseFilter *state)
{
    unsigned index;
    for (index = 0; index < STEREO_POSE_FILTER_POINT_COUNT; ++index) reset_point(state, index);
    state->time_us = 0;
}

static int point_valid(Point3D point)
{
    return point.valid && isfinite(point.x) && isfinite(point.y) && isfinite(point.z);
}

static int major_geometry_valid(const HumanPose3D *pose)
{
    float length = separation(pose->elbow, pose->wrist);
    return pose->valid && point_valid(pose->elbow) && point_valid(pose->wrist) &&
        pose->elbow.z >= 300.0f && pose->elbow.z <= 3000.0f &&
        pose->wrist.z >= 300.0f && pose->wrist.z <= 3000.0f &&
        isfinite(length) && length >= 80.0f && length <= 600.0f;
}

static int hand_geometry_valid(const HumanPose3D *pose)
{
    float span = separation(pose->finger1, pose->finger2);
    return point_valid(pose->finger1) && point_valid(pose->finger2) &&
        separation(pose->wrist, pose->finger1) <= 250.0f &&
        separation(pose->wrist, pose->finger2) <= 250.0f &&
        span >= 10.0f && span <= 250.0f;
}

static void expire_point(StereoPoseFilter *state, unsigned index, uint64_t time_us)
{
    StereoKalman3D *point = &state->kalman[index];
    if (point->have_time && (time_us <= point->last_time_us ||
        time_us - point->last_time_us > G_KALMAN_RESET_GAP_US)) reset_point(state, index);
}

int stereo_pose_filter_apply_at(StereoPoseFilter *state, HumanPose3D *pose, uint64_t time_us)
{
    StereoPoseFilter candidate;
    Point3D *points[STEREO_POSE_FILTER_POINT_COUNT];
    unsigned index;
    int hand_valid;
    if (state == NULL) return 0;
    if (pose == NULL) {
        reset_all(state);
        return 0;
    }
    if (state->time_us && time_us <= state->time_us) {
        reset_all(state);
        memset(pose, 0, sizeof(*pose));
        return 0;
    }
    state->time_us = time_us;
    if (!major_geometry_valid(pose)) {
        for (index = 0U; index < STEREO_POSE_FILTER_POINT_COUNT; ++index)
            expire_point(state, index, time_us);
        memset(pose, 0, sizeof(*pose));
        return 0;
    }
    candidate = *state;
    hand_valid = hand_geometry_valid(pose);
    points[0] = &pose->elbow;
    points[1] = &pose->wrist;
    points[2] = &pose->finger1;
    points[3] = &pose->finger2;
    for (index = 0; index < STEREO_POSE_FILTER_POINT_COUNT; ++index) {
        float input[3] = {points[index]->x, points[index]->y, points[index]->z};
        float output[3] = {0.0f, 0.0f, 0.0f};
        int valid;
        if (index >= 2U && !hand_valid) {
            memset(points[index], 0, sizeof(*points[index]));
            expire_point(&candidate, index, time_us);
            continue;
        }
        valid = stereo_kalman3d_step(&candidate.kalman[index], input, 1, time_us, output);
        *points[index] = (Point3D){output[0], output[1], output[2], (uint8_t)valid};
    }
    if (!major_geometry_valid(pose)) {
        for (index = 0U; index < STEREO_POSE_FILTER_POINT_COUNT; ++index)
            expire_point(state, index, time_us);
        memset(pose, 0, sizeof(*pose));
        return 0;
    }
    if (!hand_geometry_valid(pose)) {
        memset(&pose->finger1, 0, sizeof(pose->finger1));
        memset(&pose->finger2, 0, sizeof(pose->finger2));
        for (index = 2U; index < STEREO_POSE_FILTER_POINT_COUNT; ++index) {
            candidate.kalman[index] = state->kalman[index];
            expire_point(&candidate, index, time_us);
        }
    }
    *state = candidate;
    return 1;
}

int stereo_pose_filter_apply(StereoPoseFilter *state, HumanPose3D *pose, float dt_sec)
{
    uint64_t elapsed;
    if (state == NULL) return 0;
    if (!isfinite(dt_sec) || dt_sec <= 0.0f || dt_sec > 3600.0f) {
        reset_all(state);
        if (pose != NULL) memset(pose, 0, sizeof(*pose));
        return 0;
    }
    elapsed = (uint64_t)((double)dt_sec * 1000000.0 + 0.5);
    if (!elapsed || UINT64_MAX - state->time_us < elapsed) {
        reset_all(state);
        if (pose != NULL) memset(pose, 0, sizeof(*pose));
        return 0;
    }
    return stereo_pose_filter_apply_at(state, pose, state->time_us + elapsed);
}
