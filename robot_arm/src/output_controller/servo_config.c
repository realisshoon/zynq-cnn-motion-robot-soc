#include "output_controller/servo_config.h"

#include <stddef.h>


/*
 * ============================================================
 * Servo Hardware Configuration
 * ============================================================
 *
 * 현재 초기 설정:
 *
 * 0 deg   -> 500 us
 * 90 deg  -> 1500 us
 * 180 deg -> 2500 us
 *
 * 실제 Robot Arm 조립 후
 * Servo별 Calibration을 진행하여 수정한다.
 */
static const ServoConfig servo_configs[SERVO_COUNT] = {

    /*
     * Elbow Roll
     */
    /* NOT VERIFIED FOR NEW 5-AXIS MECHANISM: retained legacy values. */
    [SERVO_ELBOW_ROLL] = {

        .min_deg    = 0.0f,
        .center_deg = 90.0f,
        .max_deg    = 180.0f,

        .min_us     = 500U,
        .center_us  = 1500U,
        .max_us     = 2500U
    },


    /*
     * Elbow Pitch
     */
    /* NOT VERIFIED FOR NEW 5-AXIS MECHANISM: retained legacy values. */
    [SERVO_ELBOW_PITCH] = {

        .min_deg    = 0.0f,
        .center_deg = 90.0f,
        .max_deg    = 180.0f,

        .min_us     = 500U,
        .center_us  = 1500U,
        .max_us     = 2500U
    },


    /*
     * Wrist Pitch
     */
    /* NOT VERIFIED FOR NEW 5-AXIS MECHANISM: retained legacy values. */
    [SERVO_WRIST_PITCH] = {

        .min_deg    = 0.0f,
        .center_deg = 90.0f,
        .max_deg    = 180.0f,

        .min_us     = 500U,
        .center_us  = 1500U,
        .max_us     = 2500U
    },


    /*
     * Wrist Roll
     */
    /* NOT VERIFIED FOR NEW 5-AXIS MECHANISM: retained legacy values. */
    [SERVO_WRIST_ROLL] = {

        .min_deg    = 0.0f,
        .center_deg = 90.0f,
        .max_deg    = 180.0f,

        .min_us     = 500U,
        .center_us  = 1500U,
        .max_us     = 2500U
    },


    /*
     * Gripper
     *
     * JointCommand에서는 gripper_norm 0.0 ~ 1.0을 사용한다.
     *
     * 이 ServoConfig에서는 실제 PWM Hardware 범위만 관리한다.
     */
    /* NOT VERIFIED FOR NEW 5-AXIS MECHANISM: retained legacy values. */
    [SERVO_GRIPPER] = {

        .min_deg    = 0.0f,
        .center_deg = 90.0f,
        .max_deg    = 180.0f,

        .min_us     = 500U,
        .center_us  = 1500U,
        .max_us     = 2500U
    }
};


/*
 * ============================================================
 * Servo Configuration Get
 * ============================================================
 */
const ServoConfig *servo_config_get(
    ServoChannel channel
)
{
    if ((uint32_t)channel >=
        (uint32_t)SERVO_COUNT) {

        return NULL;
    }

    return &servo_configs[channel];
}