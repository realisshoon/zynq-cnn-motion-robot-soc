#ifndef OUTPUT_CONTROLLER_SERVO_CONFIG_H
#define OUTPUT_CONTROLLER_SERVO_CONFIG_H

#include <stdint.h>


/*
 * ============================================================
 * G51 Robot Arm Servo Channel
 * ============================================================
 */
typedef enum {

    SERVO_BASE = 0,

    SERVO_SHOULDER,
    SERVO_ELBOW,

    SERVO_WRIST_PITCH,
    SERVO_WRIST_ROLL,

    SERVO_GRIPPER,

    SERVO_COUNT

} ServoChannel;


/*
 * ============================================================
 * Servo Hardware Configuration
 * ============================================================
 *
 * deg:
 *   Servo Angle 기준
 *
 * us:
 *   실제 Servo PWM Pulse Width
 *
 * 현재 초기 기준:
 *
 *   0 deg   -> 500 us
 *   90 deg  -> 1500 us
 *   180 deg -> 2500 us
 *
 * 실제 MG996R + G51 조립 후
 * Servo별 Calibration 값으로 수정한다.
 */
typedef struct {

    float min_deg;
    float center_deg;
    float max_deg;

    uint16_t min_us;
    uint16_t center_us;
    uint16_t max_us;

} ServoConfig;


/*
 * Servo Channel에 해당하는
 * Hardware Configuration을 반환한다.
 *
 * 성공:
 *   ServoConfig Pointer
 *
 * 실패:
 *   NULL
 */
const ServoConfig *servo_config_get(
    ServoChannel channel
);


#endif /* OUTPUT_CONTROLLER_SERVO_CONFIG_H */