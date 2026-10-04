#include "stereo_vision/stereo_one_euro.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>

static double expected_coefficient(double cutoff_hz, double elapsed_seconds)
{
    double time_constant = 1.0 / (6.283185307179586476925286766559 * cutoff_hz);
    return elapsed_seconds / (time_constant + elapsed_seconds);
}

static void assert_close(float actual, double expected)
{
    double tolerance = 2.0e-6 * fmax(1.0, fabs(expected));
    assert(isfinite(actual));
    assert(fabs((double)actual - expected) <= tolerance);
}

static void assert_reset(const StereoOneEuro *state)
{
    unsigned axis;
    assert(state->have_time == 0U);
    assert(state->last_time_us == 0U);
    for (axis = 0U; axis < 3U; ++axis) {
        assert(state->previous_raw[axis] == 0.0f);
        assert(state->filtered[axis] == 0.0f);
        assert(state->derivative[axis] == 0.0f);
    }
}

static void assert_untouched(const float output[3])
{
    assert(output[0] == 123.0f);
    assert(output[1] == 456.0f);
    assert(output[2] == 789.0f);
}

static void test_defaults_and_configuration(void)
{
    StereoOneEuro state;
    StereoOneEuroConfig defaults = stereo_one_euro_defaults();
    StereoOneEuroConfig config;
    const float input[3] = {1.0f, 2.0f, 3.0f};
    float output[3] = {123.0f, 456.0f, 789.0f};
    const float invalid_values[] = {0.0f, -1.0f, NAN, INFINITY, -INFINITY};
    unsigned index;
    unsigned field;

    assert(STEREO_ONE_EURO_RESET_GAP_US == UINT64_C(500000));
    assert(defaults.minimum_cutoff_hz == 1.0f);
    assert(defaults.beta_per_mm == 0.01f);
    assert(defaults.derivative_cutoff_hz == 1.0f);
    stereo_one_euro_init(NULL, defaults);
    for (field = 0U; field < 3U; ++field) {
        for (index = 0U; index < sizeof(invalid_values) / sizeof(invalid_values[0]); ++index) {
            config = defaults;
            if (field == 0U) {
                config.minimum_cutoff_hz = invalid_values[index];
            } else if (field == 1U) {
                if (index == 0U) continue;
                config.beta_per_mm = invalid_values[index];
            } else {
                config.derivative_cutoff_hz = invalid_values[index];
            }
            stereo_one_euro_init(&state, config);
            assert_reset(&state);
            assert(state.config.minimum_cutoff_hz == defaults.minimum_cutoff_hz);
            assert(state.config.beta_per_mm == defaults.beta_per_mm);
            assert(state.config.derivative_cutoff_hz == defaults.derivative_cutoff_hz);
            state.config = config;
            assert(!stereo_one_euro_step(&state, input, 1, 1U, output));
            assert_reset(&state);
            assert_untouched(output);
        }
    }
    config = (StereoOneEuroConfig){2.0f, 0.0f, 3.0f};
    stereo_one_euro_init(&state, config);
    assert(state.config.minimum_cutoff_hz == 2.0f);
    assert(state.config.beta_per_mm == 0.0f);
    assert(state.config.derivative_cutoff_hz == 3.0f);
    assert(stereo_one_euro_step(&state, input, 1, 1U, output));
    stereo_one_euro_init(&state, defaults);
    assert_reset(&state);
}

static void test_constant_and_fixed_cutoff(void)
{
    StereoOneEuro state;
    StereoOneEuroConfig config = stereo_one_euro_defaults();
    float input[3] = {15.0f, -25.0f, 35.0f};
    float output[3];
    double expected_position = 0.0;
    double coefficient = expected_coefficient(1.0, 0.02);
    unsigned sample;
    unsigned axis;

    stereo_one_euro_init(&state, config);
    for (sample = 0U; sample < 100U; ++sample) {
        assert(stereo_one_euro_step(&state, input, 1, (uint64_t)sample * 20000U, output));
        for (axis = 0U; axis < 3U; ++axis) {
            assert(output[axis] == input[axis]);
            assert(state.derivative[axis] == 0.0f);
        }
    }
    config.beta_per_mm = 0.0f;
    stereo_one_euro_init(&state, config);
    input[0] = input[1] = input[2] = 0.0f;
    assert(stereo_one_euro_step(&state, input, 1, 0U, output));
    input[0] = 100.0f;
    for (sample = 1U; sample <= 30U; ++sample) {
        expected_position += coefficient * (100.0 - expected_position);
        assert(stereo_one_euro_step(&state, input, 1, (uint64_t)sample * 20000U, output));
        assert_close(output[0], expected_position);
        assert(output[1] == 0.0f && output[2] == 0.0f);
    }
}

static void test_vector_speed_and_filtered_derivative(void)
{
    StereoOneEuro state;
    StereoOneEuroConfig config = stereo_one_euro_defaults();
    const float samples[][3] = {{0.0f, 0.0f, 0.0f}, {6.0f, 8.0f, 0.0f},
                               {6.0f, 8.0f, 0.0f}, {-3.0f, 12.0f, 4.0f}};
    const uint64_t times[] = {0U, 20000U, 40000U, 70000U};
    float previous_position[3] = {0.0f, 0.0f, 0.0f};
    float previous_derivative[3] = {0.0f, 0.0f, 0.0f};
    float output[3];
    unsigned sample;
    unsigned axis;

    stereo_one_euro_init(&state, config);
    assert(stereo_one_euro_step(&state, samples[0], 1, times[0], output));
    for (sample = 1U; sample < sizeof(times) / sizeof(times[0]); ++sample) {
        double elapsed = (double)(times[sample] - times[sample - 1U]) * 1.0e-6;
        double derivative_alpha = expected_coefficient(config.derivative_cutoff_hz, elapsed);
        double derivative[3];
        double squared_speed = 0.0;
        double position_alpha;

        for (axis = 0U; axis < 3U; ++axis) {
            derivative[axis] = derivative_alpha *
                ((double)samples[sample][axis] - previous_position[axis]) / elapsed +
                (1.0 - derivative_alpha) * previous_derivative[axis];
            squared_speed += derivative[axis] * derivative[axis];
        }
        position_alpha = expected_coefficient((double)config.minimum_cutoff_hz +
            (double)config.beta_per_mm * sqrt(squared_speed), elapsed);
        assert(stereo_one_euro_step(&state, samples[sample], 1, times[sample], output));
        for (axis = 0U; axis < 3U; ++axis) {
            assert_close(state.derivative[axis], derivative[axis]);
            assert_close(output[axis], previous_position[axis] + position_alpha *
                ((double)samples[sample][axis] - previous_position[axis]));
            if (sample == 2U && axis < 2U)
                assert(state.derivative[axis] > previous_derivative[axis]);
            previous_position[axis] = output[axis];
            previous_derivative[axis] = state.derivative[axis];
        }
    }
}

static void test_motion_adaptation(void)
{
    StereoOneEuro slow_state;
    StereoOneEuro fast_state;
    StereoOneEuro fixed_state;
    StereoOneEuroConfig config = stereo_one_euro_defaults();
    float slow_input[3] = {0.0f, 0.0f, 0.0f};
    float fast_input[3] = {0.0f, 0.0f, 0.0f};
    float slow_output[3], fast_output[3], fixed_output[3];
    unsigned sample;

    stereo_one_euro_init(&slow_state, config);
    stereo_one_euro_init(&fast_state, config);
    config.beta_per_mm = 0.0f;
    stereo_one_euro_init(&fixed_state, config);
    for (sample = 0U; sample <= 100U; ++sample) {
        uint64_t time_us = (uint64_t)sample * 20000U;
        slow_input[0] = (float)sample * 0.2f;
        fast_input[0] = (float)sample * 20.0f;
        assert(stereo_one_euro_step(&slow_state, slow_input, 1, time_us, slow_output));
        assert(stereo_one_euro_step(&fast_state, fast_input, 1, time_us, fast_output));
        assert(stereo_one_euro_step(&fixed_state, fast_input, 1, time_us, fixed_output));
        assert(slow_output[0] <= slow_input[0] && fast_output[0] <= fast_input[0]);
        if (sample > 0U) {
            assert(fast_output[0] > fixed_output[0]);
            assert(fast_output[0] / fast_input[0] > slow_output[0] / slow_input[0]);
        }
    }
    assert(fast_state.derivative[0] > slow_state.derivative[0]);
}

static void test_temporal_boundaries(void)
{
    StereoOneEuro state;
    float input[3] = {0.0f, 0.0f, 0.0f};
    float output[3];
    const uint64_t reset_times[] = {1000U, 999U, 501001U};
    unsigned index;

    for (index = 0U; index < sizeof(reset_times) / sizeof(reset_times[0]); ++index) {
        stereo_one_euro_init(&state, stereo_one_euro_defaults());
        input[0] = 0.0f;
        assert(stereo_one_euro_step(&state, input, 1, 1000U, output));
        input[0] = 100.0f;
        assert(stereo_one_euro_step(&state, input, 1, reset_times[index], output));
        assert(output[0] == 100.0f && state.derivative[0] == 0.0f);
        assert(state.last_time_us == reset_times[index]);
    }
    for (index = 0U; index < 3U; ++index) {
        uint64_t elapsed = STEREO_ONE_EURO_RESET_GAP_US - 1U + index;
        stereo_one_euro_init(&state, stereo_one_euro_defaults());
        input[0] = 0.0f;
        assert(stereo_one_euro_step(&state, input, 1, 0U, output));
        input[0] = 100.0f;
        assert(stereo_one_euro_step(&state, input, 1, elapsed, output));
        if (elapsed <= STEREO_ONE_EURO_RESET_GAP_US) {
            assert(output[0] > 0.0f && output[0] < 100.0f);
            assert(state.derivative[0] > 0.0f);
        } else {
            assert(output[0] == 100.0f && state.derivative[0] == 0.0f);
        }
    }
    stereo_one_euro_init(&state, stereo_one_euro_defaults());
    input[0] = 0.0f;
    assert(stereo_one_euro_step(&state, input, 1, UINT64_MAX - 20000U, output));
    input[0] = 100.0f;
    assert(stereo_one_euro_step(&state, input, 1, UINT64_MAX, output));
    assert(output[0] > 0.0f && output[0] < 100.0f);
    input[0] = 200.0f;
    assert(stereo_one_euro_step(&state, input, 1, 0U, output));
    assert(output[0] == 200.0f && state.derivative[0] == 0.0f);
    assert(stereo_one_euro_step(&state, input, 1, UINT64_MAX, output));
    assert(state.last_time_us == UINT64_MAX && state.derivative[0] == 0.0f);
}

static void test_invalid_inputs_and_nulls(void)
{
    StereoOneEuro state;
    const float input[3] = {1.0f, 2.0f, 3.0f};
    float output[3] = {123.0f, 456.0f, 789.0f};
    const float invalid_values[] = {NAN, INFINITY, -INFINITY};
    unsigned axis;
    unsigned index;

    assert(!stereo_one_euro_step(NULL, input, 1, 0U, output));
    assert_untouched(output);
    for (axis = 0U; axis < 3U; ++axis) {
        for (index = 0U; index < sizeof(invalid_values) / sizeof(invalid_values[0]); ++index) {
            float invalid_input[3] = {1.0f, 2.0f, 3.0f};
            float scratch[3];
            stereo_one_euro_init(&state, stereo_one_euro_defaults());
            assert(stereo_one_euro_step(&state, input, 1, 0U, scratch));
            invalid_input[axis] = invalid_values[index];
            assert(!stereo_one_euro_step(&state, invalid_input, 1, 20000U, output));
            assert_reset(&state);
            assert_untouched(output);
            assert(stereo_one_euro_step(&state, input, 1, 40000U, scratch));
            assert(scratch[axis] == input[axis] && state.derivative[axis] == 0.0f);
        }
    }
    for (index = 0U; index < 3U; ++index) {
        float scratch[3];
        stereo_one_euro_init(&state, stereo_one_euro_defaults());
        assert(stereo_one_euro_step(&state, input, 1, 0U, scratch));
        if (index == 0U)
            assert(!stereo_one_euro_step(&state, input, 0, 20000U, output));
        else if (index == 1U)
            assert(!stereo_one_euro_step(&state, NULL, 1, 20000U, output));
        else
            assert(!stereo_one_euro_step(&state, input, 1, 20000U, NULL));
        assert_reset(&state);
        assert_untouched(output);
        assert(stereo_one_euro_step(&state, input, 1, 40000U, scratch));
        assert(scratch[0] == input[0] && state.derivative[0] == 0.0f);
    }
}

static void test_numeric_limits_and_aliasing(void)
{
    StereoOneEuro state;
    StereoOneEuroConfig config = stereo_one_euro_defaults();
    float input[3] = {-FLT_MAX, 0.0f, 0.0f};
    float output[3] = {123.0f, 456.0f, 789.0f};
    float scratch[3];
    unsigned field;
    unsigned axis;

    stereo_one_euro_init(&state, config);
    assert(stereo_one_euro_step(&state, input, 1, 0U, scratch));
    input[0] = FLT_MAX;
    assert(!stereo_one_euro_step(&state, input, 1, 1U, output));
    assert_reset(&state);
    assert_untouched(output);
    input[0] = 10.0f;
    assert(stereo_one_euro_step(&state, input, 1, 2U, input));
    input[0] = 20.0f;
    assert(stereo_one_euro_step(&state, input, 1, 20002U, input));
    assert(input[0] > 10.0f && input[0] < 20.0f);
    assert(state.previous_raw[0] == 20.0f);
    config = (StereoOneEuroConfig){FLT_MAX, FLT_MAX, FLT_MAX};
    stereo_one_euro_init(&state, config);
    input[0] = 0.0f;
    assert(stereo_one_euro_step(&state, input, 1, 0U, scratch));
    input[0] = 1.0f;
    assert(stereo_one_euro_step(&state, input, 1, 1U, scratch));
    assert(isfinite(scratch[0]));
    config = (StereoOneEuroConfig){nextafterf(0.0f, 1.0f), 0.0f,
                                 nextafterf(0.0f, 1.0f)};
    stereo_one_euro_init(&state, config);
    input[0] = 0.0f;
    assert(stereo_one_euro_step(&state, input, 1, 0U, scratch));
    input[0] = 1.0f;
    assert(stereo_one_euro_step(&state, input, 1, 1U, scratch));
    assert(scratch[0] >= 0.0f && scratch[0] <= 1.0f);
    for (field = 0U; field < 3U; ++field) {
        for (axis = 0U; axis < 3U; ++axis) {
            stereo_one_euro_init(&state, stereo_one_euro_defaults());
            assert(stereo_one_euro_step(&state, input, 1, 0U, scratch));
            if (field == 0U) state.previous_raw[axis] = NAN;
            else if (field == 1U) state.filtered[axis] = INFINITY;
            else state.derivative[axis] = NAN;
            assert(!stereo_one_euro_step(&state, input, 1, 20000U, output));
            assert_reset(&state);
            assert_untouched(output);
        }
    }
}

int main(void)
{
    test_defaults_and_configuration();
    test_constant_and_fixed_cutoff();
    test_vector_speed_and_filtered_derivative();
    test_motion_adaptation();
    test_temporal_boundaries();
    test_invalid_inputs_and_nulls();
    test_numeric_limits_and_aliasing();
    puts("test_stereo_one_euro: PASS (7 test groups)");
    return 0;
}
