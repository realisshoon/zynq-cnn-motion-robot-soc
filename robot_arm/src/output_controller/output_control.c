#include "output_controller/output_control.h"
void output_control_init(void) {}
uint8_t output_control_update(const JointCommand *joint_cmd,
                              ServoPwmCommand *pwm_cmd)
{
    /* BLOCKED BY AGENT2 INTERFACE; conversion currently rejects legacy input. */
    return servo_control_convert(joint_cmd, pwm_cmd);
}
