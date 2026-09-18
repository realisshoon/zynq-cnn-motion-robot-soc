#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>

#include "robot_calibration/motion_limits.h"

#define FLOAT_TOLERANCE 0.00001f

static int nearly_equal(float actual, float expected)
{
    return fabsf(actual - expected) <= FLOAT_TOLERANCE;
}

static void test_step_toward(void)
{
    assert(nearly_equal(motion_limits_step_toward(10.0f, 20.0f, 3.0f), 13.0f));
    assert(nearly_equal(motion_limits_step_toward(20.0f, 10.0f, 3.0f), 17.0f));
    assert(nearly_equal(motion_limits_step_toward(10.0f, 12.0f, 3.0f), 12.0f));
    assert(nearly_equal(motion_limits_step_toward(10.0f, 13.0f, 3.0f), 13.0f));
    assert(nearly_equal(motion_limits_step_toward(10.0f, 10.0f, 3.0f), 10.0f));

    assert(nearly_equal(motion_limits_step_toward(10.0f, 20.0f, 0.0f), 10.0f));
    assert(nearly_equal(motion_limits_step_toward(10.0f, 20.0f, -1.0f), 10.0f));
}

static void test_ticks_to_target(void)
{
    assert(motion_limits_ticks_to_target(10.0f, 10.0f, 0.0f) == 0);
    assert(motion_limits_ticks_to_target(0.0f, 12.0f, 3.0f) == 4);
    assert(motion_limits_ticks_to_target(12.0f, 0.0f, 3.0f) == 4);
    assert(motion_limits_ticks_to_target(0.0f, 10.0f, 3.0f) == 4);
    assert(motion_limits_ticks_to_target(0.0f, 1.0f, 0.0f) == -1);
    assert(motion_limits_ticks_to_target(0.0f, 1.0f, -1.0f) == -1);
    assert(motion_limits_ticks_to_target(NAN, 1.0f, 1.0f) == -1);
    assert(motion_limits_ticks_to_target(0.0f, 1.0f, INFINITY) == -1);
    assert(motion_limits_ticks_to_target(0.0f, 1.0f, 1.0f / (float)INT_MAX) == -1);
}

int main(void)
{
    test_step_toward();
    test_ticks_to_target();

    puts("test_motion_limits: all tests passed");
    return 0;
}
