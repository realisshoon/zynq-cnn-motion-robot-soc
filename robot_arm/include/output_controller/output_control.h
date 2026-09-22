#ifndef OUTPUT_CONTROLLER_OUTPUT_CONTROL_H
#define OUTPUT_CONTROLLER_OUTPUT_CONTROL_H
#include "output_controller/servo_control.h"
void output_control_init(void);
/* BLOCKED BY AGENT2 INTERFACE: pending real ForearmJointCommand.
 * Legacy signature remains, but every call returns 0 without modifying PWM.
 * Integration must not apply PWM on failure. */
uint8_t output_control_update(const JointCommand *joint_cmd,
                              ServoPwmCommand *pwm_cmd);
#endif
