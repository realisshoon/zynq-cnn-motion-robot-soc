#ifndef OUTPUT_CONTROLLER_SERVO_HAL_H
#define OUTPUT_CONTROLLER_SERVO_HAL_H
#include "output_controller/servo_control.h"
#include "output_controller/dual_arm_follower.h"
void servo_hal_init(void);
int servo_hal_enable(void);
int servo_hal_disable(void);
int servo_hal_faulted(void);
/* Validate all five PWM values before writing any shadow register.
 * Write CH0..CH4 then UPDATE. Return 1 success, 0 failure.
 * Driver failures may leave partial shadow writes; no rollback.
 * Split mode disables both banks and latches HAL_FAULT on driver failure. */
int servo_hal_apply(const ServoPwmCommand *cmd);
int servo_hal_apply_joint_command(const ForearmJointCommand *command,
                                 const ServoPwmCommand *pwm, int control_tick);
const DualArmFollower *servo_hal_dual_status(void);
/* Five config center writes -> UPDATE -> ENABLE.
 * NOT VERIFIED FOR NEW 5-AXIS MECHANISM. CH5 is never written;
 * global enable behavior on that unused physical output is unchanged.
 * Split mode disables both banks, prepares only the selected bank, and leaves
 * output OFF until servo_hal_enable(). A successful disable clears its fault. */
int servo_hal_startup(void);
#endif
