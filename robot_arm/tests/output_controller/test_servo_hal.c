#include "output_controller/servo_hal.h"
#include "drivers/servo_pwm_driver.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
_Static_assert(SERVO_COUNT == 5, "five logical servos");
_Static_assert(SERVO_PWM_DRIVER_CHANNEL_COUNT == 5, "five driver channels");
_Static_assert(SERVO_PWM_DRIVER_HW_CHANNEL_COUNT == 6, "six hardware channels");
_Static_assert(sizeof(ServoPwmCommand) == 5 * sizeof(uint16_t), "five PWM fields");
#ifndef SERVO_PWM_DRIVER_USE_XILINX
static void check(unsigned index, unsigned offset, unsigned value)
{
    ServoPwmDriverMockWrite entry;
    assert(servo_pwm_driver_mock_get_log(index, &entry));
    assert(entry.offset == offset && entry.value == value);
    assert(entry.offset != 0x14U);
}
static void reset(void) { servo_pwm_driver_mock_reset(); servo_hal_init(); }
int main(void)
{
    ServoPwmCommand cmd = {600, 900, 1200, 1800, 2400};
    const unsigned values[] = {600, 900, 1200, 1800, 2400};
    unsigned i, side;
    reset();
    assert(servo_hal_apply(&cmd));
    assert(servo_pwm_driver_mock_get_log_count() == 6);
    for (i = 0; i < 5; ++i) check(i, i * 4, values[i]);
    check(5, 0x1C, 1);
    reset();
    assert(servo_hal_startup());
    assert(servo_pwm_driver_mock_get_log_count() == 7);
    for (i = 0; i < 5; ++i)
        check(i, i * 4, servo_config_get((ServoChannel)i)->center_us);
    check(5, 0x1C, 1); check(6, 0x18, 1);
    assert(servo_hal_disable()); check(7, 0x18, 0);
    for (i = 0; i < 5; ++i) for (side = 0; side < 2; ++side) {
        ServoPwmCommand bad = cmd;
        uint16_t *fields[] = {&bad.elbow_roll_pwm_us, &bad.elbow_pitch_pwm_us,
            &bad.wrist_pitch_pwm_us, &bad.wrist_roll_pwm_us, &bad.gripper_pwm_us};
        const ServoConfig *config = servo_config_get((ServoChannel)i);
        *fields[i] = side ? config->max_us + 1 : config->min_us - 1;
        reset();
        assert(!servo_hal_apply(&bad));
        assert(servo_pwm_driver_mock_get_log_count() == 0);
    }
    reset();
    assert(!servo_hal_apply(NULL));
    assert(!servo_pwm_driver_write_channel((ServoPwmDriverChannel)5, 1500));
    assert(!servo_pwm_driver_write_channel((ServoPwmDriverChannel)-1, 1500));
    assert(servo_pwm_driver_mock_get_log_count() == 0);
    servo_pwm_driver_mock_reset();
    assert(!servo_hal_startup()); /* no enable after an uninitialized write failure */
    assert(servo_pwm_driver_mock_get_log_count() == 0);
    /* Host log capacity models a write failure. UPDATE must not follow it. */
    reset();
    for (i = 0; i < 127; ++i) assert(servo_hal_disable());
    assert(!servo_hal_apply(&cmd));
    assert(servo_pwm_driver_mock_get_log_count() == 128);
    check(127, 0x00, 600);
    puts("PASS HAL/driver: mapping, startup, CH5 unused, invalid and failure paths");
    return 0;
}
#else
int main(void)
{
    puts("SKIP HAL mock test: Xilinx real-driver build");
    return 0;
}
#endif
