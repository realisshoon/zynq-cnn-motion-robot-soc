#ifndef ROBOT_CALIBRATION_MOTION_SMOOTHING_H
#define ROBOT_CALIBRATION_MOTION_SMOOTHING_H

/*
 * Cubic smoothstep ease-in/ease-out. t is a progress ratio in [0, 1] (clamped
 * if outside that range); the return value is the eased progress in [0, 1],
 * with zero slope at both t=0 and t=1 (removes the velocity discontinuity a
 * linear ramp has at motion start/stop).
 */
float motion_smoothing_ease(float t);

/*
 * A cubic smoothstep's peak instantaneous velocity is 1.5x its average
 * velocity (at t=0.5). If a motion planned to take base_ticks under a hard
 * per-tick velocity limit is instead driven by motion_smoothing_ease(), the
 * duration must be stretched by that same 1.5x factor so the curve's peak
 * velocity still fits under the original limit. Returns 0 when base_ticks
 * <= 0 (already at target / no motion needed). See docs/agent2_design_log.md,
 * "Motion smoothing approach" entry, for the derivation and caveats.
 */
int motion_smoothing_stretch_ticks(int base_ticks);

#endif /* ROBOT_CALIBRATION_MOTION_SMOOTHING_H */
