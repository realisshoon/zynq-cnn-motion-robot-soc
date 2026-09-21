#include "robot_calibration/motion_limits.h"

#include <limits.h>
#include <math.h>

float motion_limits_step_toward(float current,
                                float target,
                                float max_delta_per_tick)
{
    double distance;

    if (!isfinite(current) || !isfinite(target) ||
        !isfinite(max_delta_per_tick) || max_delta_per_tick <= 0.0f) {
        return current;
    }

    distance = fabs((double)target - (double)current);
    if (distance <= (double)max_delta_per_tick) {
        return target;
    }

    if (target > current) {
        return current + max_delta_per_tick;
    }
    return current - max_delta_per_tick;
}

int motion_limits_ticks_to_target(float current,
                                  float target,
                                  float max_delta_per_tick)
{
    double distance;
    double ticks;

    if (!isfinite(current) || !isfinite(target) ||
        !isfinite(max_delta_per_tick)) {
        return -1;
    }

    distance = fabs((double)target - (double)current);
    if (distance == 0.0) {
        return 0;
    }
    if (max_delta_per_tick <= 0.0f) {
        return -1;
    }

    ticks = ceil(distance / (double)max_delta_per_tick);
    if (ticks > (double)INT_MAX) {
        return -1;
    }
    return (int)ticks;
}
