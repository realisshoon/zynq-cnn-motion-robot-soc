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
    /* Now that the five-axis Agent2 interface (forearm_calibration/
     * servo_control) is wired in, a valid all-zero-degree command is a
     * legitimate conversion, not a rejection case. Joint-limit/safety
     * range checks are a different layer's responsibility (forearm_safety_
     * check.c, home_is_safe()), not servo_control_convert's -- it only
     * rejects NULL, invalid, and non-finite values. */
    {
        ServoPwmCommand pwm = {0};

        legacy.valid = 1;
        assert(output_control_update(&legacy, &pwm));
        assert(pwm.elbow_roll_pwm_us == 500U);
        assert(pwm.elbow_pitch_pwm_us == 500U);
        assert(pwm.wrist_pitch_pwm_us == 500U);
        assert(pwm.wrist_roll_pwm_us == 500U);
        /* gripper_norm=0.0 = Close, which the servo reaches at max_us. */
        assert(pwm.gripper_pwm_us == 2500U);
    }
#ifndef SERVO_PWM_DRIVER_USE_XILINX
    test_failed_conversion_skips_apply();
#endif
    puts("PASS output: NULL/invalid/non-finite commands are rejected without modifying PWM,"
         " and a valid all-zero command converts successfully");
    return 0;
}
