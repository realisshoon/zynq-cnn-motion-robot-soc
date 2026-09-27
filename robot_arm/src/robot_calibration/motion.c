#include "robot_calibration/motion.h"
#include <math.h>
#include <stddef.h>

static double clamp(double x, double lo, double hi)
{
    return fmax(lo, fmin(hi, x));
}

/* Travel INCLUDING the next tick at speed u, then brake by d=a*dt per tick:
 * dt*(u + max(u-d,0) + max(u-2d,0) + ...).
 * This is discrete command-space braking, NOT the continuous v*v/(2*a).
 */
static double stopping_travel(double u, double d, double dt)
{
    double n;
    if (u <= 0.0) return 0.0;
    n = ceil(u / d);
    return dt * (n*u - 0.5*d*n*(n-1.0));
}

static double reachable_speed(const Motion *m, double distance)
{
    double lo = 0.0, hi = m->vmax;
    double d = m->amax * m->dt;
    int i;
    if (distance <= 0.0) return 0.0;
    /* One-tick travel can stop on the following tick; avoids a search-grid
     * floor for sub-ulp-of-vmax distances near a zero-valued position. */
    if (distance <= d*m->dt) return fmin(m->vmax, distance/m->dt);
    if (stopping_travel(hi, d, m->dt) <= distance) return hi;
    /* Monotone inversion. Return the feasible side, never the midpoint. */
    for (i = 0; i < 52; ++i) {
        double mid = 0.5*(lo+hi);
        if (stopping_travel(mid, d, m->dt) <= distance) lo = mid;
        else hi = mid;
    }
    return lo;
}

int motion_init(Motion *m, double initial, double lower, double upper,
                double vmax, double amax, double dt, int mode)
{
    Motion next;
    if (!m || !isfinite(initial) || !isfinite(lower) || !isfinite(upper) ||
        !isfinite(vmax) || !isfinite(amax) || !isfinite(dt) || lower >= upper ||
        initial < lower || initial > upper || vmax <= 0 || amax <= 0 || dt <= 0 ||
        (mode != SPEED_ONLY && mode != SPEED_ACCEL)) return MOTION_INVALID;
    /* Keep numeric envelope explicit; prototype is intended for servo degrees. */
    if (fabs(lower)>360 || fabs(upper)>360 || vmax>360 || amax>10000 ||
        amax<0.1 || fabs(dt-0.020)>1e-12 || vmax/(amax*dt)>1e7)
        return MOTION_INVALID;
    next.q = initial; next.v = 0; next.target = initial;
    next.lower = lower; next.upper = upper;
    next.vmax = vmax; next.amax = amax; next.dt = dt;
    next.mode = mode; next.emergency_holds = 0;
    *m = next;
    return MOTION_OK;
}

int motion_set_target(Motion *m, double target)
{
    if (!m || !isfinite(target)) return MOTION_INVALID;
    m->target = clamp(target, m->lower, m->upper);
    return m->target == target ? MOTION_OK : MOTION_CLAMPED;
}

int motion_step(Motion *m)
{
    double e, vnext, qnext;
    if (!m) return MOTION_INVALID;
    e = m->target - m->q;
    if (m->mode == SPEED_ONLY) {
        vnext = clamp(e/m->dt, -m->vmax, m->vmax);
    } else {
        double d = m->amax*m->dt;
        double desired = copysign(reachable_speed(m, fabs(e)), e);
        double lo = fmax(m->v-d, -reachable_speed(m, m->q-m->lower));
        double hi = fmin(m->v+d, reachable_speed(m, m->upper-m->q));
        if (lo > hi) {
            /* Roundoff only: choose closest feasible hard-limit speed. */
            if (lo-hi > 1e-8) return MOTION_INFEASIBLE;
            vnext = fabs(lo) < fabs(hi) ? lo : hi;
        } else {
            vnext = clamp(desired, lo, hi);
        }
    }
    qnext = m->q + vnext*m->dt;
    if (!isfinite(qnext) || qnext < m->lower-1e-9 || qnext > m->upper+1e-9)
        return MOTION_INFEASIBLE;
    /* Remove position rounding at exact bounds; record actual commanded delta. */
    qnext = clamp(qnext, m->lower, m->upper);
    m->v = (qnext-m->q)/m->dt;
    m->q = qnext;
    return MOTION_OK;
}

void motion_emergency_hold(Motion *m)
{
    if (!m) return;
    m->target = m->q;
    m->v = 0;
    ++m->emergency_holds;
}
