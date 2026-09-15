#ifndef OUTPUT_CONTROLLER_SERVO_CONTROL_H
#define OUTPUT_CONTROLLER_SERVO_CONTROL_H

#include <stdint.h>

#include "common/robot_types.h"


/*
 * 6개 Servo에 전달할 PWM Pulse Width Command
 *
 * 단위:
 *   microsecond(us)
 */
typedef struct {

    uint16_t base_pwm_us;

    uint16_t shoulder_pwm_us;
    uint16_t elbow_pwm_us;

    uint16_t wrist_pitch_pwm_us;
    uint16_t wrist_roll_pwm_us;

    uint16_t gripper_pwm_us;

} ServoPwmCommand;


/*
 * JointCommand -> ServoPwmCommand
 *
 * Agent2에서 전달된 Robot Joint Angle을
 * 실제 Servo Hardware에서 사용할 PWM 값으로 변환한다.
 *
 * 반환값:
 *
 * 1 : 정상 변환
 * 0 : 입력 invalid 또는 잘못된 인자
 */
uint8_t servo_control_convert(
    const JointCommand *joint_cmd,
    ServoPwmCommand *pwm_cmd
);


#endif /* OUTPUT_CONTROLLER_SERVO_CONTROL_H */