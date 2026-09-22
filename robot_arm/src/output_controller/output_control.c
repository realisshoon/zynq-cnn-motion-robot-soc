#include "output_controller/output_control.h"
void output_control_init(void) {}
uint8_t output_control_update(const ForearmJointCommand *joint_cmd,
                              ServoPwmCommand *pwm_cmd)
{
    return servo_control_convert(joint_cmd, pwm_cmd);
}
