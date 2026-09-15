#include "output_controller/servo_config.h"

#include <stddef.h>


/*
 * Servo별 Hardware Configuration
 *
 * 현재 값:
 *
 *   0 deg   -> 1000 us
 *   90 deg  -> 1500 us
 *   180 deg -> 2000 us
 *
 * 현재는 초기 SW 기능 검증을 위한 값이다.
 *
 * 실제 G51 + MG996R 테스트 후
 * Servo별 min / center / max 값을
 * 이 파일에서만 수정하면 된다.
 */
static const ServoConfig servo_configs[SERVO_COUNT] = {

    /*
     * Base
     */
    [SERVO_BASE] = {
        .min_deg    = 0.0f,
        .center_deg = 90.0f,
        .max_deg    = 180.0f,

        .min_us     = 1000,
        .center_us  = 1500,
        .max_us     = 2000
    },


    /*
     * Shoulder
     */
    [SERVO_SHOULDER] = {
        .min_deg    = 0.0f,
        .center_deg = 90.0f,
        .max_deg    = 180.0f,

        .min_us     = 1000,
        .center_us  = 1500,
        .max_us     = 2000
    },


    /*
     * Elbow
     */
    [SERVO_ELBOW] = {
        .min_deg    = 0.0f,
        .center_deg = 90.0f,
        .max_deg    = 180.0f,

        .min_us     = 1000,
        .center_us  = 1500,
        .max_us     = 2000
    },


    /*
     * Wrist Pitch
     */
    [SERVO_WRIST_PITCH] = {
        .min_deg    = 0.0f,
        .center_deg = 90.0f,
        .max_deg    = 180.0f,

        .min_us     = 1000,
        .center_us  = 1500,
        .max_us     = 2000
    },


    /*
     * Wrist Roll
     */
    [SERVO_WRIST_ROLL] = {
        .min_deg    = 0.0f,
        .center_deg = 90.0f,
        .max_deg    = 180.0f,

        .min_us     = 1000,
        .center_us  = 1500,
        .max_us     = 2000
    },


    /*
     * Gripper
     *
     * gripper_norm은 0.0 ~ 1.0을 사용하지만,
     * PWM Hardware 범위는 동일한 ServoConfig에서 관리한다.
     */
    [SERVO_GRIPPER] = {
        .min_deg    = 0.0f,
        .center_deg = 90.0f,
        .max_deg    = 180.0f,

        .min_us     = 1000,
        .center_us  = 1500,
        .max_us     = 2000
    }
};


const ServoConfig *servo_config_get(ServoChannel channel)
{
    /*
     * 잘못된 Channel 방지
     *
     * 음수 enum도 unsigned 변환 시 큰 값이 되므로
     * SERVO_COUNT 이상으로 걸러진다.
     */
    if ((uint32_t)channel >= (uint32_t)SERVO_COUNT) {
        return NULL;
    }

    return &servo_configs[channel];
}