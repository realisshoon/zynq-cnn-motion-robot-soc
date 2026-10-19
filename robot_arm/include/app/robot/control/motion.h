#ifndef MOTION_H
#define MOTION_H

/* Standalone scalar COMMAND generator. No UART/PWM/robot dependency.
 * Fixed dt=0.020 s (prototype scope). v is the first difference of commanded q,
 * not measured velocity. Supported angle envelope is [-360,360] degrees.
 * Limits must stay constant after init; API calls require a valid initialized state.
 * SPEED_ACCEL enforces first/second command differences, not continuous motor
 * acceleration or jerk. Four independent copies cover the four rotary joints.
 */
enum { SPEED_ONLY = 0, SPEED_ACCEL = 1 };
enum { MOTION_OK = 0, MOTION_CLAMPED = 1, MOTION_INVALID = -1, MOTION_INFEASIBLE = -2 };
typedef struct {
    double q, v, target;
    double lower, upper, vmax, amax, dt;
    int mode;
    unsigned emergency_holds;
} Motion;

int motion_init(Motion *m, double initial, double lower, double upper,
                double vmax, double amax, double dt, int mode);
int motion_set_target(Motion *m, double target);
int motion_step(Motion *m);
/* Explicit exceptional command freeze: overrides acceleration; does not disable PWM.
 * Caller can run step on a COPY, check multi-axis geometry, commit all or hold all.
 * Initial position must already be approved; no physical homing is performed here.
 */
void motion_emergency_hold(Motion *m);
#endif
