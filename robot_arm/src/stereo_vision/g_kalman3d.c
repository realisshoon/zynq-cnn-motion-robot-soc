#include "stereo_vision/g_kalman3d.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

static int config_valid(StereoKalman3DConfig config)
{
    unsigned axis;
    for (axis = 0U; axis < 3U; ++axis)
        if (!isfinite(config.measurement_std_mm[axis]) ||
            config.measurement_std_mm[axis] <= 0.0f) return 0;
    return isfinite(config.acceleration_std_mm_s2) &&
        config.acceleration_std_mm_s2 >= 0.0f &&
        isfinite(config.initial_velocity_std_mm_s) &&
        config.initial_velocity_std_mm_s >= 0.0f;
}

static void reset(StereoKalman3D *state)
{
    memset(state->axis, 0, sizeof(state->axis));
    state->last_time_us = 0U;
    state->have_time = 0U;
}

static int axis_valid(const StereoKalmanAxis *axis)
{
    double bound;
    if (!isfinite(axis->position_mm) || !isfinite(axis->velocity_mm_s) ||
        fabs(axis->position_mm) > FLT_MAX || fabs(axis->velocity_mm_s) > FLT_MAX ||
        !isfinite(axis->position_variance) || axis->position_variance < 0.0 ||
        !isfinite(axis->velocity_variance) || axis->velocity_variance < 0.0 ||
        !isfinite(axis->position_velocity_covariance)) return 0;
    bound = sqrt(axis->position_variance) * sqrt(axis->velocity_variance);
    return fabs(axis->position_velocity_covariance) <=
        bound * (1.0 + 32.0 * DBL_EPSILON);
}

StereoKalman3DConfig stereo_kalman3d_defaults(void)
{
    StereoKalman3DConfig config = {
        {G_KALMAN_SIGMA_XY_MM, G_KALMAN_SIGMA_XY_MM, G_KALMAN_SIGMA_Z_MM},
        G_KALMAN_ACCEL_STD_MM_S2, G_KALMAN_INITIAL_VELOCITY_STD_MM_S
    };
    return config;
}

void stereo_kalman3d_init(StereoKalman3D *state, StereoKalman3DConfig config)
{
    if (state == NULL) return;
    memset(state, 0, sizeof(*state));
    state->config = config_valid(config) ? config : stereo_kalman3d_defaults();
}

int stereo_kalman3d_configure(StereoKalman3D *state, StereoKalman3DConfig config)
{
    if (state == NULL || !config_valid(config)) return 0;
    stereo_kalman3d_init(state, config);
    return 1;
}

static int update_axis(const StereoKalmanAxis *previous, double measurement,
    double dt, double measurement_variance, double acceleration_variance,
    StereoKalmanAxis *next)
{
    double dt2 = dt * dt;
    double predicted_position = previous->position_mm + dt * previous->velocity_mm_s;
    double position_variance = previous->position_variance +
        2.0 * dt * previous->position_velocity_covariance +
        dt2 * previous->velocity_variance + 0.25 * dt2 * dt2 * acceleration_variance;
    double cross_covariance = previous->position_velocity_covariance +
        dt * previous->velocity_variance + 0.5 * dt2 * dt * acceleration_variance;
    double velocity_variance = previous->velocity_variance + dt2 * acceleration_variance;
    double innovation_variance = position_variance + measurement_variance;
    double gain_position, gain_velocity, residual, remaining;
    double root_position, projected_velocity, independent_velocity, corrected_velocity;
    if (!isfinite(innovation_variance) || innovation_variance <= 0.0 ||
        !isfinite(position_variance) || position_variance <= 0.0 ||
        !isfinite(cross_covariance) || !isfinite(velocity_variance)) return 0;
    gain_position = position_variance / innovation_variance;
    gain_velocity = cross_covariance / innovation_variance;
    residual = measurement - predicted_position;
    remaining = 1.0 - gain_position;
    root_position = sqrt(position_variance);
    projected_velocity = cross_covariance / root_position;
    independent_velocity = velocity_variance - projected_velocity * projected_velocity;
    if (independent_velocity < -64.0 * DBL_EPSILON * velocity_variance) return 0;
    independent_velocity = fmax(0.0, independent_velocity);
    corrected_velocity = projected_velocity - gain_velocity * root_position;
    next->position_mm = predicted_position + gain_position * residual;
    next->velocity_mm_s = previous->velocity_mm_s + gain_velocity * residual;
    next->position_variance = remaining * remaining * position_variance +
        gain_position * gain_position * measurement_variance;
    next->position_velocity_covariance = remaining * root_position * corrected_velocity +
        gain_position * gain_velocity * measurement_variance;
    next->velocity_variance = corrected_velocity * corrected_velocity +
        independent_velocity + gain_velocity * gain_velocity * measurement_variance;
    return axis_valid(next);
}

int stereo_kalman3d_step(StereoKalman3D *state, const float input[3], int valid,
                         uint64_t time_us, float output[3])
{
    StereoKalmanAxis next[3];
    float measurement[3];
    unsigned axis;
    int seed;
    double dt = 0.0;
    double acceleration_variance;
    if (state == NULL) return 0;
    if (input == NULL || output == NULL || !valid || !config_valid(state->config)) {
        reset(state);
        return 0;
    }
    for (axis = 0U; axis < 3U; ++axis) {
        if (!isfinite(input[axis])) {
            reset(state);
            return 0;
        }
        measurement[axis] = input[axis];
    }
    seed = !state->have_time || time_us <= state->last_time_us ||
        time_us - state->last_time_us > G_KALMAN_RESET_GAP_US;
    if (!seed) dt = (double)(time_us - state->last_time_us) * 1.0e-6;
    acceleration_variance = (double)state->config.acceleration_std_mm_s2 *
        state->config.acceleration_std_mm_s2;
    for (axis = 0U; axis < 3U; ++axis) {
        double measurement_variance = (double)state->config.measurement_std_mm[axis] *
            state->config.measurement_std_mm[axis];
        if (seed) {
            next[axis] = (StereoKalmanAxis){measurement[axis], 0.0,
                measurement_variance, 0.0,
                (double)state->config.initial_velocity_std_mm_s *
                    state->config.initial_velocity_std_mm_s};
        } else if (!axis_valid(&state->axis[axis]) ||
            !update_axis(&state->axis[axis], measurement[axis], dt,
                measurement_variance, acceleration_variance, &next[axis])) {
            reset(state);
            return 0;
        }
    }
    memcpy(state->axis, next, sizeof(next));
    state->last_time_us = time_us;
    state->have_time = 1U;
    for (axis = 0U; axis < 3U; ++axis) output[axis] = (float)next[axis].position_mm;
    return 1;
}
