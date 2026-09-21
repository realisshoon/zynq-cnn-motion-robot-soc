#include "robot_calibration/motion_smoothing.h"

#include <math.h>

#define SMOOTHSTEP_PEAK_TO_AVERAGE_RATIO 1.5f

float motion_smoothing_ease(float t)
{
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

int motion_smoothing_stretch_ticks(int base_ticks)
{
    if (base_ticks <= 0) return 0;

    return (int)ceilf((float)base_ticks * SMOOTHSTEP_PEAK_TO_AVERAGE_RATIO);
}
