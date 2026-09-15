#ifndef OUTPUT_CONTROLLER_SERVO_CONFIG_H
#define OUTPUT_CONTROLLER_SERVO_CONFIG_H

#include <stdint.h>


/*
 * G51 Robot Arm Servo Channel
 *
 * 각 Servo를 구분하기 위한 번호.
 *
 * SERVO_BASE        = 0
 * SERVO_SHOULDER    = 1
 * SERVO_ELBOW       = 2
 * SERVO_WRIST_PITCH = 3
 * SERVO_WRIST_ROLL  = 4
 * SERVO_GRIPPER     = 5
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
 * Servo Hardware Configuration
 *
 * deg :
 *   Servo 각도와 PWM을 변환하기 위한 기준 각도
 *
 * us :
 *   실제 Servo에 전달할 PWM Pulse Width
 *
 * 예)
 *
 * 0 deg   -> 1000 us
 * 90 deg  -> 1500 us
 * 180 deg -> 2000 us
 *
 * 실제 값은 MG996R + G51 실물 테스트 후
 * Servo별로 수정할 수 있다.
 */
typedef struct {

    /* Angle reference */
    float min_deg;
    float center_deg;
    float max_deg;

    /* PWM reference */
    uint16_t min_us;
    uint16_t center_us;
    uint16_t max_us;

} ServoConfig;


/*
 * Servo Channel에 해당하는 설정값을 반환한다.
 *
 * 성공:
 *   ServoConfig 주소 반환
 *
 * 실패:
 *   NULL 반환
 */
const ServoConfig *servo_config_get(ServoChannel channel);


#endif /* OUTPUT_CONTROLLER_SERVO_CONFIG_H */