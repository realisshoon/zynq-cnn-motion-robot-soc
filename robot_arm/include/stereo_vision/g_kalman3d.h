#ifndef G_KALMAN3D_H
#define G_KALMAN3D_H

#include <stdint.h>
#include "g_variant_config.h"

typedef struct {
    float measurement_std_mm[3];
    float acceleration_std_mm_s2;
    float initial_velocity_std_mm_s;
} StereoKalman3DConfig;

typedef struct {
    double position_mm;
    double velocity_mm_s;
    double position_variance;
    double position_velocity_covariance;
    double velocity_variance;
} StereoKalmanAxis;

typedef struct {
    StereoKalmanAxis axis[3];
    uint64_t last_time_us;
    unsigned have_time;
    StereoKalman3DConfig config;
} StereoKalman3D;

StereoKalman3DConfig stereo_kalman3d_defaults(void);
void stereo_kalman3d_init(StereoKalman3D *state, StereoKalman3DConfig config);
int stereo_kalman3d_configure(StereoKalman3D *state, StereoKalman3DConfig config);
int stereo_kalman3d_step(StereoKalman3D *state, const float input[3], int valid,
                         uint64_t time_us, float output[3]);
#endif
