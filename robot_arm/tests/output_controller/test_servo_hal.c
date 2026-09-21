#include "output_controller/servo_hal.h"
#include "drivers/servo_pwm_driver.h"

#include <assert.h>
#include <stdint.h>


int main(void)
{
#ifndef SERVO_PWM_DRIVER_USE_XILINX

    ServoPwmDriverMockWrite entry;

    ServoPwmCommand invalid_cmd;


    /*
     * ========================================================
     * TEST 1
     *
     * Startup Sequence
     *
     * 6 Shadow
     * -> UPDATE
     * -> ENABLE
     * ========================================================
     */

    servo_pwm_driver_mock_reset();

    servo_hal_init();


    assert(
        servo_hal_startup() == 1
    );


    /*
     * 총 8회 Write
     *
     * 6 Shadow
     * + UPDATE
     * + ENABLE
     */
    assert(
        servo_pwm_driver_mock_get_log_count()
        ==
        8U
    );


    /*
     * BASE
     */
    assert(
        servo_pwm_driver_mock_get_log(
            0U,
            &entry
        )
        ==
        1
    );

    assert(entry.offset == 0x00U);
    assert(entry.value  == 1500U);


    /*
     * SHOULDER
     */
    assert(
        servo_pwm_driver_mock_get_log(
            1U,
            &entry
        )
        ==
        1
    );

    assert(entry.offset == 0x04U);
    assert(entry.value  == 1500U);


    /*
     * ELBOW
     */
    assert(
        servo_pwm_driver_mock_get_log(
            2U,
            &entry
        )
        ==
        1
    );

    assert(entry.offset == 0x08U);
    assert(entry.value  == 1500U);


    /*
     * WRIST PITCH
     */
    assert(
        servo_pwm_driver_mock_get_log(
            3U,
            &entry
        )
        ==
        1
    );

    assert(entry.offset == 0x0CU);
    assert(entry.value  == 1500U);


    /*
     * WRIST ROLL
     */
    assert(
        servo_pwm_driver_mock_get_log(
            4U,
            &entry
        )
        ==
        1
    );

    assert(entry.offset == 0x10U);
    assert(entry.value  == 1500U);


    /*
     * GRIPPER
     */
    assert(
        servo_pwm_driver_mock_get_log(
            5U,
            &entry
        )
        ==
        1
    );

    assert(entry.offset == 0x14U);
    assert(entry.value  == 1500U);


    /*
     * UPDATE
     */
    assert(
        servo_pwm_driver_mock_get_log(
            6U,
            &entry
        )
        ==
        1
    );

    assert(entry.offset == 0x1CU);
    assert(entry.value  == 1U);


    /*
     * ENABLE
     */
    assert(
        servo_pwm_driver_mock_get_log(
            7U,
            &entry
        )
        ==
        1
    );

    assert(entry.offset == 0x18U);
    assert(entry.value  == 1U);


    /*
     * ========================================================
     * TEST 2
     *
     * Invalid PWM 값이면
     * 단 하나의 Register도 Write하면 안 됨.
     * ========================================================
     */

    servo_pwm_driver_mock_reset();

    servo_hal_init();


    invalid_cmd.base_pwm_us        = 0U;
    invalid_cmd.shoulder_pwm_us    = 1500U;
    invalid_cmd.elbow_pwm_us       = 1500U;
    invalid_cmd.wrist_pitch_pwm_us = 1500U;
    invalid_cmd.wrist_roll_pwm_us  = 1500U;
    invalid_cmd.gripper_pwm_us     = 1500U;


    assert(
        servo_hal_apply(
            &invalid_cmd
        )
        ==
        0
    );


    /*
     * 범위 검사를 Write 전에 하기 때문에
     * Log는 반드시 0개.
     */
    assert(
        servo_pwm_driver_mock_get_log_count()
        ==
        0U
    );

#endif


    return 0;
}