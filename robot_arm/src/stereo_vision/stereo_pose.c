#include "stereo_vision/stereo_pose.h"

#include <float.h>
#include <math.h>
#include <string.h>

static StereoPoseStatus reconstruct_geometry(const StereoGeometryContext *geometry,
    const HumanPose2D *left, const HumanPose2D *right,
    HumanPose3D *output, StereoPoseDiagnostics *diagnostics)
{
    const Point2D *left_points[6], *right_points[6];
    Point3D *output_points[6];
    HumanPose3D candidate;
    unsigned index;
    if (output == NULL || diagnostics == NULL) return STEREO_POSE_ARGUMENT;
    memset(output, 0, sizeof(*output));
    memset(diagnostics, 0, sizeof(*diagnostics));
    for (index = 0; index < 6; ++index)
        diagnostics->point_status[index] = STEREO_INVALID_ARGUMENT;
    if (geometry == NULL || left == NULL || right == NULL)
        return STEREO_POSE_ARGUMENT;
    if (!left->valid || !right->valid) return STEREO_POSE_MAJOR_INVALID;
    memset(&candidate, 0, sizeof(candidate));
    candidate.frame_id = left->frame_id;
    left_points[0] = &left->shoulder_l; left_points[1] = &left->shoulder_r;
    left_points[2] = &left->elbow; left_points[3] = &left->wrist;
    left_points[4] = &left->finger1; left_points[5] = &left->finger2;
    right_points[0] = &right->shoulder_l; right_points[1] = &right->shoulder_r;
    right_points[2] = &right->elbow; right_points[3] = &right->wrist;
    right_points[4] = &right->finger1; right_points[5] = &right->finger2;
    output_points[0] = &candidate.shoulder_l; output_points[1] = &candidate.shoulder_r;
    output_points[2] = &candidate.elbow; output_points[3] = &candidate.wrist;
    output_points[4] = &candidate.finger1; output_points[5] = &candidate.finger2;
    for (index = 0; index < 6; ++index) {
        StereoPoint3D measured;
        if (left_points[index]->valid && right_points[index]->valid)
            diagnostics->point_status[index] = stereo_reconstruct_point(geometry,
                left_points[index]->x, left_points[index]->y,
                right_points[index]->x, right_points[index]->y, &measured);
        if (diagnostics->point_status[index] == STEREO_OK) {
            if (fabs(measured.x_mm) > FLT_MAX || fabs(measured.y_mm) > FLT_MAX ||
                measured.z_mm > FLT_MAX) return STEREO_POSE_GEOMETRY;
            *output_points[index] = (Point3D){(float)measured.x_mm,
                (float)-measured.y_mm, (float)measured.z_mm, 1U};
        } else if (index == 2 || index == 3) {
            return STEREO_POSE_GEOMETRY;
        }
    }
    diagnostics->hand_valid = candidate.finger1.valid && candidate.finger2.valid;
    if (!diagnostics->hand_valid) {
        memset(&candidate.finger1, 0, sizeof(candidate.finger1));
        memset(&candidate.finger2, 0, sizeof(candidate.finger2));
    }
    candidate.valid = 1U;
    *output = candidate;
    return STEREO_POSE_OK;
}

StereoPoseStatus stereo_pose_reconstruct(const StereoGeometryContext *geometry,
    const HumanPose2D *left, const HumanPose2D *right,
    const StereoFrameMetadata *left_metadata, const StereoFrameMetadata *right_metadata,
    uint64_t max_exposure_skew_us, HumanPose3D *output, StereoPoseDiagnostics *diagnostics)
{
    uint64_t skew;
    StereoPoseStatus status;
    unsigned index;
    if (output == NULL || diagnostics == NULL) return STEREO_POSE_ARGUMENT;
    memset(output, 0, sizeof(*output));
    memset(diagnostics, 0, sizeof(*diagnostics));
    for (index = 0; index < 6; ++index)
        diagnostics->point_status[index] = STEREO_INVALID_ARGUMENT;
    if (geometry == NULL || left == NULL || right == NULL || left_metadata == NULL ||
        right_metadata == NULL || max_exposure_skew_us == 0) return STEREO_POSE_ARGUMENT;
    if (!left_metadata->exposure_time_verified || !right_metadata->exposure_time_verified ||
        !left_metadata->fixed_geometry_verified || !right_metadata->fixed_geometry_verified ||
        !left_metadata->localization_quality_verified || !right_metadata->localization_quality_verified ||
        !left_metadata->shared_clock_epoch ||
        left_metadata->shared_clock_epoch != right_metadata->shared_clock_epoch)
        return STEREO_POSE_UNVERIFIED;
    skew = left_metadata->exposure_time_us > right_metadata->exposure_time_us
        ? left_metadata->exposure_time_us - right_metadata->exposure_time_us
        : right_metadata->exposure_time_us - left_metadata->exposure_time_us;
    diagnostics->exposure_skew_us = skew;
    if (skew > max_exposure_skew_us) return STEREO_POSE_TIME_MISMATCH;
    status = reconstruct_geometry(geometry, left, right, output, diagnostics);
    diagnostics->exposure_skew_us = skew;
    return status;
}

static float point_distance(Point3D first, Point3D second)
{
    float delta_x = first.x - second.x;
    float delta_y = first.y - second.y;
    float delta_z = first.z - second.z;
    return sqrtf(delta_x * delta_x + delta_y * delta_y + delta_z * delta_z);
}

StereoPoseStatus stereo_pose_reconstruct_async_test(const StereoGeometryContext *geometry,
    const HumanPose2D *left, const HumanPose2D *right,
    HumanPose3D *output, StereoPoseDiagnostics *diagnostics)
{
    StereoPoseStatus status = reconstruct_geometry(geometry, left, right, output, diagnostics);
    const Point3D *major[2];
    float forearm;
    unsigned index;
    if (status != STEREO_POSE_OK) return status;
    major[0] = &output->elbow; major[1] = &output->wrist;
    for (index = 0; index < 2; ++index) {
        if (!isfinite(major[index]->x) || !isfinite(major[index]->y) ||
            !isfinite(major[index]->z) || major[index]->z < 300.0f || major[index]->z > 3000.0f) {
            memset(output, 0, sizeof(*output));
            return STEREO_POSE_GEOMETRY;
        }
    }
    forearm = point_distance(output->elbow, output->wrist);
    if (forearm < 80.0f || forearm > 600.0f) {
        memset(output, 0, sizeof(*output));
        return STEREO_POSE_GEOMETRY;
    }
    if (diagnostics->hand_valid &&
        (point_distance(output->wrist, output->finger1) > 250.0f ||
         point_distance(output->wrist, output->finger2) > 250.0f ||
         point_distance(output->finger1, output->finger2) < 10.0f ||
         point_distance(output->finger1, output->finger2) > 250.0f)) {
        memset(&output->finger1, 0, sizeof(output->finger1));
        memset(&output->finger2, 0, sizeof(output->finger2));
        diagnostics->hand_valid = 0U;
    }
    return STEREO_POSE_OK;
}
