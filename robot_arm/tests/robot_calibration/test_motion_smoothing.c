#include <assert.h>
#include <math.h>

#include "robot_calibration/motion_smoothing.h"

#define EPS 0.0001f

static int nearly_equal(float actual, float expected)
{
    return fabsf(actual - expected) <= EPS;
}

int main(void)
{
    assert(nearly_equal(motion_smoothing_ease(0.0f), 0.0f));
    assert(nearly_equal(motion_smoothing_ease(1.0f), 1.0f));
    assert(nearly_equal(motion_smoothing_ease(0.5f), 0.5f));
    assert(nearly_equal(motion_smoothing_ease(-1.0f), 0.0f));
    assert(nearly_equal(motion_smoothing_ease(2.0f), 1.0f));

    /* Ease-in/ease-out: first-half average slope must be gentler than the
     * midpoint slope, i.e. progress at t=0.25 is less than a linear ramp's. */
    assert(motion_smoothing_ease(0.25f) < 0.25f);
    assert(motion_smoothing_ease(0.75f) > 0.75f);

    /* Monotonically increasing. */
    assert(motion_smoothing_ease(0.2f) < motion_smoothing_ease(0.4f));
    assert(motion_smoothing_ease(0.4f) < motion_smoothing_ease(0.6f));
    assert(motion_smoothing_ease(0.6f) < motion_smoothing_ease(0.8f));

    assert(motion_smoothing_stretch_ticks(0) == 0);
    assert(motion_smoothing_stretch_ticks(-5) == 0);
    assert(motion_smoothing_stretch_ticks(4) == 6);
    assert(motion_smoothing_stretch_ticks(1) == 2);
    assert(motion_smoothing_stretch_ticks(10) == 15);

    return 0;
}
