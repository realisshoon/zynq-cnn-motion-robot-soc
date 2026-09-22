#ifndef OUTPUT_CONTROLLER_SERVO_HAL_H
#define OUTPUT_CONTROLLER_SERVO_HAL_H
#include "output_controller/servo_control.h"
void servo_hal_init(void);
int servo_hal_enable(void);
int servo_hal_disable(void);
/* Validate all five PWM values before writing any shadow register.
 * Write CH0..CH4 then UPDATE. Return 1 success, 0 failure.
 * Driver failures may leave partial shadow writes; no rollback. */
int servo_hal_apply(const ServoPwmCommand *cmd);
/* Five config center writes -> UPDATE -> ENABLE.
 * NOT VERIFIED FOR NEW 5-AXIS MECHANISM. CH5 is never written;
 * global enable behavior on that unused physical output is unchanged. */
int servo_hal_startup(void);
#endif
