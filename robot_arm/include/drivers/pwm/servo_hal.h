#ifndef OUTPUT_CONTROLLER_SERVO_HAL_H
#define OUTPUT_CONTROLLER_SERVO_HAL_H
#include "app/robot/control/servo_control.h"
#include "app/robot/control/forearm_calibration.h"

typedef enum {
    SERVO_OUTPUT_DISABLED = 0,
    SERVO_OUTPUT_ENABLED = 1,
    SERVO_OUTPUT_FAULT = 4
} ServoOutputMode;

typedef struct {
    ForearmJointCommand applied;
    uint8_t have_command, enabled;
    ServoOutputMode mode;
} ServoOutputStatus;
void servo_hal_init(void);
/* Granted only by the explicit pipeline E path. Not a physical safety proof. */
int servo_hal_set_output_permitted(int permitted);
int servo_hal_output_permitted(void);
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
const ServoOutputStatus *servo_hal_status(void);
/* Fail-closed startup: disables output and revokes permission; no pose staging
 * or enable. E must prepare its reference explicitly. CH5 is never written.
 * This software result does not verify physical pin levels. */
int servo_hal_startup(void);
#endif
