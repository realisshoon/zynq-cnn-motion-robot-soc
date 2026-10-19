#ifndef STEREO_VISION_GEOMETRY_H
#define STEREO_VISION_GEOMETRY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double fx, fy, cx, cy;
    double distortion[5];
} StereoCameraCalibration;

typedef struct {
    StereoCameraCalibration left;
    StereoCameraCalibration right;
    double rotation[9];
    double translation_mm[3];
    uint32_t image_width, image_height;
} StereoCalibration;

typedef struct {
    double max_reprojection_error_px;
    double min_ray_sine;
} StereoGeometryOptions;

typedef struct {
    StereoCalibration calibration;
    StereoGeometryOptions options;
    uint32_t initialized;
} StereoGeometryContext;

typedef struct {
    double x_mm, y_mm, z_mm;
    double left_reprojection_error_px;
    double right_reprojection_error_px;
} StereoPoint3D;

typedef enum {
    STEREO_OK = 0,
    STEREO_INVALID_ARGUMENT = -1,
    STEREO_INVALID_CALIBRATION = -2,
    STEREO_UNDISTORT_FAILED = -3,
    STEREO_DEGENERATE = -4,
    STEREO_BEHIND_CAMERA = -5,
    STEREO_LOW_QUALITY = -6,
    STEREO_NUMERIC_FAILURE = -7
} StereoStatus;

StereoGeometryOptions stereo_geometry_default_options(void);
StereoStatus stereo_geometry_init(StereoGeometryContext *context,
                                  const StereoCalibration *calibration,
                                  const StereoGeometryOptions *options);
StereoStatus stereo_reconstruct_point(const StereoGeometryContext *context,
                                      double left_x, double left_y,
                                      double right_x, double right_y,
                                      StereoPoint3D *output);

#ifdef __cplusplus
}
#endif

#endif
