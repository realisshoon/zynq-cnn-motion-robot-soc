#ifndef OUTPUT_CONTROLLER_OUTPUT_CONTROL_H
#define OUTPUT_CONTROLLER_OUTPUT_CONTROL_H

#include <stdint.h>

#include "common/robot_types.h"
#include "output_controller/servo_control.h"


/*
 * ============================================================
 * Output Controller Initialization
 * ============================================================
 *
 * 현재 Output Controller는 별도의 내부 상태를 사용하지 않는다.
 *
 * 추후 Output Enable / Emergency Stop 등의
 * 출력 관리 기능이 추가될 경우 이 계층에서 관리할 수 있다.
 */
void output_control_init(void);


/*
 * ============================================================
 * Output Controller Update
 * ============================================================
 *
 * joint_cmd:
 *   상위 제어부에서 전달된 최종 Robot JointCommand
 *
 * pwm_cmd:
 *   Servo Hardware에 전달할 PWM Pulse Width Command
 *
 * 동작:
 *
 * JointCommand
 *      ↓
 * servo_control_convert()
 *      ↓
 * ServoPwmCommand
 *
 * 반환값:
 *
 * 1 = PWM Command 생성 성공
 * 0 = invalid 입력 / 잘못된 인자 / 변환 실패
 */
uint8_t output_control_update(
    const JointCommand *joint_cmd,
    ServoPwmCommand *pwm_cmd
);


#endif /* OUTPUT_CONTROLLER_OUTPUT_CONTROL_H */