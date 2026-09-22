#ifndef OUTPUT_CONTROLLER_OUTPUT_CONTROL_H
#define OUTPUT_CONTROLLER_OUTPUT_CONTROL_H
#include "output_controller/servo_control.h"
void output_control_init(void);
/* Convert the final ForearmJointCommand through servo_control_convert().
 * Returns 1 on success, 0 on failure with PWM unchanged.
 * The caller must not apply PWM on failure. */
uint8_t output_control_update(const ForearmJointCommand *joint_cmd,
                              ServoPwmCommand *pwm_cmd);
#endif
