#include "stereo_vision/stereo_geometry.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

#define STEREO_CONTEXT_READY 0x53544552U
#define STEREO_UNDISTORT_ITERATIONS 5U
#define STEREO_JACOBI_SWEEPS 64U

static int finite_array(const double *values, unsigned count)
{
    unsigned index;
    for (index = 0; index < count; ++index)
        if (!isfinite(values[index])) return 0;
    return 1;
}

static double dot3(const double *first, const double *second)
{
    return first[0] * second[0] + first[1] * second[1] + first[2] * second[2];
}

static int valid_camera(const StereoCameraCalibration *camera)
{
    return isfinite(camera->fx) && camera->fx > 0.0 &&
           isfinite(camera->fy) && camera->fy > 0.0 &&
           isfinite(camera->cx) && isfinite(camera->cy) &&
           finite_array(camera->distortion, 5);
}

StereoGeometryOptions stereo_geometry_default_options(void)
{
    StereoGeometryOptions options = {2.0, 1e-6};
    return options;
}

StereoStatus stereo_geometry_init(StereoGeometryContext *context,
                                  const StereoCalibration *calibration,
                                  const StereoGeometryOptions *options)
{
    StereoGeometryOptions selected;
    StereoCalibration copied;
    const double *rotation;
    double determinant, baseline;
    unsigned row, column, inner;
    if (context == NULL) return STEREO_INVALID_ARGUMENT;
    if (calibration == NULL) {
        memset(context, 0, sizeof(*context));
        return STEREO_INVALID_ARGUMENT;
    }
    copied = *calibration;
    calibration = &copied;
    selected = options != NULL ? *options : stereo_geometry_default_options();
    memset(context, 0, sizeof(*context));
    if (!isfinite(selected.max_reprojection_error_px) || selected.max_reprojection_error_px <= 0.0 ||
        !isfinite(selected.min_ray_sine) || selected.min_ray_sine <= 0.0 || selected.min_ray_sine >= 1.0)
        return STEREO_INVALID_ARGUMENT;
    if (!valid_camera(&calibration->left) || !valid_camera(&calibration->right) ||
        calibration->image_width == 0 || calibration->image_height == 0 ||
        !finite_array(calibration->rotation, 9) || !finite_array(calibration->translation_mm, 3))
        return STEREO_INVALID_CALIBRATION;
    baseline = sqrt(dot3(calibration->translation_mm, calibration->translation_mm));
    if (!isfinite(baseline) || baseline <= 1e-9) return STEREO_INVALID_CALIBRATION;
    rotation = calibration->rotation;
    for (row = 0; row < 3; ++row) {
        for (column = 0; column < 3; ++column) {
            double product = 0.0;
            for (inner = 0; inner < 3; ++inner)
                product += rotation[3 * inner + row] * rotation[3 * inner + column];
            if (!isfinite(product) || fabs(product - (row == column ? 1.0 : 0.0)) > 1e-6)
                return STEREO_INVALID_CALIBRATION;
        }
    }
    determinant = rotation[0] * (rotation[4] * rotation[8] - rotation[5] * rotation[7])
                - rotation[1] * (rotation[3] * rotation[8] - rotation[5] * rotation[6])
                + rotation[2] * (rotation[3] * rotation[7] - rotation[4] * rotation[6]);
    if (fabs(determinant - 1.0) > 1e-6) return STEREO_INVALID_CALIBRATION;
    context->calibration = *calibration;
    context->options = selected;
    context->initialized = STEREO_CONTEXT_READY;
    return STEREO_OK;
}

static int undistort(const StereoCameraCalibration *camera, double pixel_x,
                     double pixel_y, double ray[3])
{
    const double *distortion = camera->distortion;
    double original_x = (pixel_x - camera->cx) / camera->fx;
    double original_y = (pixel_y - camera->cy) / camera->fy;
    double normalized_x = original_x, normalized_y = original_y;
    unsigned iteration;
    for (iteration = 0; iteration < STEREO_UNDISTORT_ITERATIONS; ++iteration) {
        double radius2 = normalized_x * normalized_x + normalized_y * normalized_y;
        double radial = 1.0 + radius2 * (distortion[0] + radius2 *
                        (distortion[1] + radius2 * distortion[4]));
        double tangent_x = 2.0 * distortion[2] * normalized_x * normalized_y +
                           distortion[3] * (radius2 + 2.0 * normalized_x * normalized_x);
        double tangent_y = distortion[2] * (radius2 + 2.0 * normalized_y * normalized_y) +
                           2.0 * distortion[3] * normalized_x * normalized_y;
        if (!isfinite(radial) || radial <= DBL_EPSILON) return 0;
        normalized_x = (original_x - tangent_x) / radial;
        normalized_y = (original_y - tangent_y) / radial;
        if (!isfinite(normalized_x) || !isfinite(normalized_y)) return 0;
    }
    ray[0] = normalized_x;
    ray[1] = normalized_y;
    ray[2] = 1.0;
    return 1;
}

static int null_vector(double matrix[4][4], double vector[4])
{
    double right_vectors[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0},
                                 {0, 0, 1, 0}, {0, 0, 0, 1}};
    double smallest = DBL_MAX, largest = 0.0, second_smallest = DBL_MAX;
    double negligible_norm = 0.0;
    unsigned sweep, first, second, row, selected = 0;
    int converged = 0;
    for (row = 0; row < 4; ++row)
        for (first = 0; first < 4; ++first)
            negligible_norm += matrix[row][first] * matrix[row][first];
    negligible_norm *= DBL_EPSILON * DBL_EPSILON;
    for (sweep = 0; sweep < STEREO_JACOBI_SWEEPS; ++sweep) {
        int changed = 0;
        for (first = 0; first < 3; ++first) {
            for (second = first + 1; second < 4; ++second) {
                double first_norm = 0.0, second_norm = 0.0, product = 0.0;
                double difference, tangent, cosine, sine;
                for (row = 0; row < 4; ++row) {
                    first_norm += matrix[row][first] * matrix[row][first];
                    second_norm += matrix[row][second] * matrix[row][second];
                    product += matrix[row][first] * matrix[row][second];
                }
                if (!isfinite(first_norm) || !isfinite(second_norm) || !isfinite(product)) return 0;
                if (first_norm <= negligible_norm || second_norm <= negligible_norm) continue;
                if (fabs(product) <= 8.0 * DBL_EPSILON * sqrt(first_norm) * sqrt(second_norm)) continue;
                difference = (second_norm - first_norm) / (2.0 * product);
                tangent = copysign(1.0 / (fabs(difference) + hypot(1.0, difference)), difference);
                cosine = 1.0 / sqrt(1.0 + tangent * tangent);
                sine = cosine * tangent;
                for (row = 0; row < 4; ++row) {
                    double old_first = matrix[row][first], old_second = matrix[row][second];
                    double vector_first = right_vectors[row][first], vector_second = right_vectors[row][second];
                    matrix[row][first] = cosine * old_first - sine * old_second;
                    matrix[row][second] = sine * old_first + cosine * old_second;
                    right_vectors[row][first] = cosine * vector_first - sine * vector_second;
                    right_vectors[row][second] = sine * vector_first + cosine * vector_second;
                }
                changed = 1;
            }
        }
        if (!changed) {
            converged = 1;
            break;
        }
    }
    if (!converged) return 0;
    for (first = 0; first < 4; ++first) {
        double norm = 0.0;
        for (row = 0; row < 4; ++row) norm = hypot(norm, matrix[row][first]);
        if (!isfinite(norm)) return 0;
        if (norm < smallest) {
            second_smallest = smallest;
            smallest = norm;
            selected = first;
        } else if (norm < second_smallest) second_smallest = norm;
        if (norm > largest) largest = norm;
    }
    if (second_smallest <= largest * 1e-12) return 0;
    for (row = 0; row < 4; ++row) vector[row] = right_vectors[row][selected];
    return finite_array(vector, 4);
}

static double reprojection_error(const StereoCameraCalibration *camera,
                                 const double point[3], double pixel_x, double pixel_y)
{
    const double *distortion = camera->distortion;
    double normalized_x = point[0] / point[2], normalized_y = point[1] / point[2];
    double radius2 = normalized_x * normalized_x + normalized_y * normalized_y;
    double radial = 1.0 + radius2 * (distortion[0] + radius2 *
                    (distortion[1] + radius2 * distortion[4]));
    double distorted_x = normalized_x * radial + 2.0 * distortion[2] * normalized_x * normalized_y +
                         distortion[3] * (radius2 + 2.0 * normalized_x * normalized_x);
    double distorted_y = normalized_y * radial + distortion[2] * (radius2 + 2.0 * normalized_y * normalized_y) +
                         2.0 * distortion[3] * normalized_x * normalized_y;
    return hypot(camera->fx * distorted_x + camera->cx - pixel_x,
                 camera->fy * distorted_y + camera->cy - pixel_y);
}

StereoStatus stereo_reconstruct_point(const StereoGeometryContext *context,
                                      double left_x, double left_y,
                                      double right_x, double right_y,
                                      StereoPoint3D *output)
{
    const StereoCalibration *calibration;
    double left_ray[3], right_ray[3], right_in_left[3], cross[3];
    double matrix[4][4], homogeneous[4], point[3], right_point[3];
    double ray_sine;
    StereoPoint3D candidate;
    unsigned row, column;
    if (output == NULL) return STEREO_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    if (context == NULL || context->initialized != STEREO_CONTEXT_READY)
        return STEREO_INVALID_ARGUMENT;
    calibration = &context->calibration;
    if (!isfinite(left_x) || !isfinite(left_y) || !isfinite(right_x) || !isfinite(right_y) ||
        left_x < 0 || left_y < 0 || right_x < 0 || right_y < 0 ||
        left_x >= calibration->image_width || right_x >= calibration->image_width ||
        left_y >= calibration->image_height || right_y >= calibration->image_height)
        return STEREO_INVALID_ARGUMENT;
    if (!undistort(&calibration->left, left_x, left_y, left_ray) ||
        !undistort(&calibration->right, right_x, right_y, right_ray))
        return STEREO_UNDISTORT_FAILED;
    for (column = 0; column < 3; ++column) {
        right_in_left[column] = 0.0;
        for (row = 0; row < 3; ++row)
            right_in_left[column] += calibration->rotation[3 * row + column] * right_ray[row];
    }
    cross[0] = left_ray[1] * right_in_left[2] - left_ray[2] * right_in_left[1];
    cross[1] = left_ray[2] * right_in_left[0] - left_ray[0] * right_in_left[2];
    cross[2] = left_ray[0] * right_in_left[1] - left_ray[1] * right_in_left[0];
    ray_sine = sqrt(dot3(cross, cross)) / sqrt(dot3(left_ray, left_ray)) / sqrt(dot3(right_in_left, right_in_left));
    if (!isfinite(ray_sine)) return STEREO_NUMERIC_FAILURE;
    if (ray_sine < context->options.min_ray_sine) return STEREO_DEGENERATE;
    for (column = 0; column < 3; ++column) {
        matrix[0][column] = left_ray[0] * (column == 2) - (column == 0);
        matrix[1][column] = left_ray[1] * (column == 2) - (column == 1);
        matrix[2][column] = right_ray[0] * calibration->rotation[6 + column] - calibration->rotation[column];
        matrix[3][column] = right_ray[1] * calibration->rotation[6 + column] - calibration->rotation[3 + column];
    }
    matrix[0][3] = matrix[1][3] = 0.0;
    matrix[2][3] = right_ray[0] * calibration->translation_mm[2] - calibration->translation_mm[0];
    matrix[3][3] = right_ray[1] * calibration->translation_mm[2] - calibration->translation_mm[1];
    if (!null_vector(matrix, homogeneous)) return STEREO_NUMERIC_FAILURE;
    if (fabs(homogeneous[3]) < 1e-12) return STEREO_DEGENERATE;
    for (row = 0; row < 3; ++row) point[row] = homogeneous[row] / homogeneous[3];
    for (row = 0; row < 3; ++row)
        right_point[row] = dot3(&calibration->rotation[3 * row], point) + calibration->translation_mm[row];
    if (!finite_array(point, 3) || !finite_array(right_point, 3)) return STEREO_NUMERIC_FAILURE;
    if (point[2] <= 0.0 || right_point[2] <= 0.0) return STEREO_BEHIND_CAMERA;
    candidate.x_mm = point[0];
    candidate.y_mm = point[1];
    candidate.z_mm = point[2];
    candidate.left_reprojection_error_px = reprojection_error(&calibration->left, point, left_x, left_y);
    candidate.right_reprojection_error_px = reprojection_error(&calibration->right, right_point, right_x, right_y);
    if (!isfinite(candidate.left_reprojection_error_px) || !isfinite(candidate.right_reprojection_error_px))
        return STEREO_NUMERIC_FAILURE;
    *output = candidate;
    if (candidate.left_reprojection_error_px > context->options.max_reprojection_error_px ||
        candidate.right_reprojection_error_px > context->options.max_reprojection_error_px)
        return STEREO_LOW_QUALITY;
    return STEREO_OK;
}
