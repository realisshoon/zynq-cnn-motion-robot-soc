#include "output_controller/output_control.h"
#include "output_controller/servo_hal.h"
#include "drivers/servo_pwm_driver.h"
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
static void test_rejected_command(const ForearmJointCommand *command)
{
    ServoPwmCommand pwm = {600, 900, 1200, 1800, 2400};
    ServoPwmCommand before = pwm;
    assert(!output_control_update(command, &pwm));
    assert(memcmp(&pwm, &before, sizeof pwm) == 0);
}

#ifndef SERVO_PWM_DRIVER_USE_XILINX
static void test_failed_conversion_skips_apply(void)
{
    ForearmJointCommand invalid = {0};
    ServoPwmCommand pwm = {600, 900, 1200, 1800, 2400};
    int applied = 0;
    servo_pwm_driver_mock_reset();
    servo_hal_init();
    /* Caller contract example, not a test of the integration implementation.
     * The stale PWM is valid: an accidental apply would produce driver writes. */
    if (output_control_update(&invalid, &pwm)) {
        applied = servo_hal_apply(&pwm);
    }
    assert(!applied);
    assert(servo_pwm_driver_mock_get_log_count() == 0);
}
#endif

int main(void)
{
    ForearmJointCommand legacy = {0};
    output_control_init();
    test_rejected_command(NULL);
    test_rejected_command(&legacy);
    assert(!output_control_update(&legacy, NULL));
    assert(!output_control_update(NULL, NULL));
    /* BLOCKED BY AGENT2 INTERFACE: this is a legacy rejection test,
     * not a successful five-axis command -> PWM conversion test. */
    legacy.valid = 1;
    test_rejected_command(&legacy);
#ifndef SERVO_PWM_DRIVER_USE_XILINX
    test_failed_conversion_skips_apply();
#endif
    puts("PASS output: pending Agent2 interface rejects legacy commands without modifying PWM");
    puts("BLOCKED BY AGENT2 INTERFACE: final five-axis command -> PWM success path");
    return 0;
}
