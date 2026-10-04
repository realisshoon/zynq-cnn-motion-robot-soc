#include "stereo_vision/stereo_one_euro.h"

#include <float.h>
#include <math.h>
#include <stddef.h>

static int stereo_one_euro_config_valid(StereoOneEuroConfig config)
{
    return isfinite(config.minimum_cutoff_hz) &&
           config.minimum_cutoff_hz > 0.0f &&
           isfinite(config.beta_per_mm) && config.beta_per_mm >= 0.0f &&
           isfinite(config.derivative_cutoff_hz) &&
           config.derivative_cutoff_hz > 0.0f;
}

static void stereo_one_euro_reset(StereoOneEuro *state)
{
    unsigned axis;

    for (axis = 0U; axis < 3U; ++axis) {
        state->previous_raw[axis] = 0.0f;
        state->filtered[axis] = 0.0f;
        state->derivative[axis] = 0.0f;
    }
    state->last_time_us = 0U;
    state->have_time = 0U;
}

static double stereo_one_euro_coefficient(double cutoff_hz, double elapsed_seconds)
{
    const double two_pi = 6.283185307179586476925286766559;
    double time_constant = 1.0 / (two_pi * cutoff_hz);

    return elapsed_seconds / (time_constant + elapsed_seconds);
}

StereoOneEuroConfig stereo_one_euro_defaults(void)
{
    StereoOneEuroConfig config;

    config.minimum_cutoff_hz = 1.0f;
    config.beta_per_mm = 0.01f;
    config.derivative_cutoff_hz = 1.0f;
    return config;
}

void stereo_one_euro_init(StereoOneEuro *state, StereoOneEuroConfig config)
{
    if (state == NULL) {
        return;
    }
    state->config = stereo_one_euro_config_valid(config) ? config : stereo_one_euro_defaults();
    stereo_one_euro_reset(state);
}

int stereo_one_euro_step(StereoOneEuro *state, const float input[3], int valid,
                         uint64_t time_us, float output[3])
{
    float current_raw[3];
    float next_filtered[3];
    float next_derivative[3];
    double derivative_values[3];
    double elapsed_seconds;
    double derivative_coefficient;
    double speed_mm_per_second;
    double cutoff_hz;
    double position_coefficient;
    unsigned axis;

    if (state == NULL) {
        return 0;
    }
    if (input == NULL || output == NULL || !valid ||
        !stereo_one_euro_config_valid(state->config)) {
        stereo_one_euro_reset(state);
        return 0;
    }
    for (axis = 0U; axis < 3U; ++axis) {
        if (!isfinite(input[axis])) {
            stereo_one_euro_reset(state);
            return 0;
        }
        current_raw[axis] = input[axis];
    }
    if (!state->have_time || time_us <= state->last_time_us ||
        time_us - state->last_time_us > STEREO_ONE_EURO_RESET_GAP_US) {
        stereo_one_euro_reset(state);
        for (axis = 0U; axis < 3U; ++axis) {
            state->previous_raw[axis] = current_raw[axis];
            state->filtered[axis] = current_raw[axis];
            output[axis] = current_raw[axis];
        }
        state->last_time_us = time_us;
        state->have_time = 1U;
        return 1;
    }
    elapsed_seconds = (double)(time_us - state->last_time_us) * 1.0e-6;
    derivative_coefficient = stereo_one_euro_coefficient(
        state->config.derivative_cutoff_hz, elapsed_seconds);
    for (axis = 0U; axis < 3U; ++axis) {
        double estimated_derivative;

        if (!isfinite(state->previous_raw[axis]) ||
            !isfinite(state->filtered[axis]) ||
            !isfinite(state->derivative[axis])) {
            stereo_one_euro_reset(state);
            return 0;
        }
        estimated_derivative = ((double)current_raw[axis] -
                                (double)state->filtered[axis]) / elapsed_seconds;
        derivative_values[axis] = derivative_coefficient * estimated_derivative +
            (1.0 - derivative_coefficient) * (double)state->derivative[axis];
        if (!isfinite(derivative_values[axis]) ||
            fabs(derivative_values[axis]) > (double)FLT_MAX) {
            stereo_one_euro_reset(state);
            return 0;
        }
        next_derivative[axis] = (float)derivative_values[axis];
    }
    speed_mm_per_second = sqrt(
        derivative_values[0] * derivative_values[0] +
        derivative_values[1] * derivative_values[1] +
        derivative_values[2] * derivative_values[2]);
    cutoff_hz = (double)state->config.minimum_cutoff_hz +
                (double)state->config.beta_per_mm * speed_mm_per_second;
    position_coefficient = stereo_one_euro_coefficient(cutoff_hz, elapsed_seconds);
    for (axis = 0U; axis < 3U; ++axis) {
        double filtered_value = (double)state->filtered[axis] +
            position_coefficient * ((double)current_raw[axis] -
                                    (double)state->filtered[axis]);

        if (!isfinite(filtered_value) || fabs(filtered_value) > (double)FLT_MAX) {
            stereo_one_euro_reset(state);
            return 0;
        }
        next_filtered[axis] = (float)filtered_value;
    }
    for (axis = 0U; axis < 3U; ++axis) {
        state->previous_raw[axis] = current_raw[axis];
        state->filtered[axis] = next_filtered[axis];
        state->derivative[axis] = next_derivative[axis];
        output[axis] = next_filtered[axis];
    }
    state->last_time_us = time_us;
    state->have_time = 1U;
    return 1;
}
