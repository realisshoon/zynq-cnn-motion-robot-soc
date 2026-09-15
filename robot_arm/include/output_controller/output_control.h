#ifndef OUTPUT_CONTROLLER_OUTPUT_CONTROL_H
#define OUTPUT_CONTROLLER_OUTPUT_CONTROL_H

#include <stdint.h>

#include "common/robot_types.h"
#include "output_controller/servo_control.h"
#include "output_controller/robot_mode.h"


/*
 * ============================================================
 * Output Controller Initialization
 * ============================================================
 *
 * 내부 모듈:
 *   robot_mode
 *   motion_record
 *
 * 을 초기화한다.
 *
 * 프로그램 시작 시 한 번 호출한다.
 */
void output_control_init(void);


/*
 * ============================================================
 * Record Button Interrupt Event
 * ============================================================
 *
 * Zybo RECORD 버튼 ISR에서 호출한다.
 *
 * ISR 안에서는 실제 녹화 동작을 하지 않고
 * Event만 기록한다.
 */
void output_control_record_button_isr(void);


/*
 * ============================================================
 * Mode Button Interrupt Event
 * ============================================================
 *
 * Zybo MODE 버튼 ISR에서 호출한다.
 *
 * FOLLOW <-> PLAYBACK 전환 Event를 전달한다.
 */
void output_control_mode_button_isr(void);


/*
 * ============================================================
 * Output Controller Update
 * ============================================================
 *
 * live_cmd:
 *   Agent2에서 전달된 실시간 JointCommand
 *
 * pwm_cmd:
 *   최종적으로 생성된 Servo PWM Command
 *
 *
 * 동작:
 *
 * FOLLOW Mode
 *   live_cmd -> servo_control
 *
 * FOLLOW + RECORD ON
 *   live_cmd -> motion_record
 *            -> servo_control
 *
 * PLAYBACK Mode
 *   motion_record -> servo_control
 *
 *
 * 반환값:
 *
 * 1 = 새로운 PWM Command 생성 성공
 * 0 = 출력할 Command 없음 / invalid 입력 / 변환 실패
 */
uint8_t output_control_update(
    const JointCommand *live_cmd,
    ServoPwmCommand *pwm_cmd
);


/*
 * 현재 Robot Mode 반환
 */
RobotMode output_control_get_mode(void);


/*
 * 현재 Record 상태 반환
 *
 * 0 = Record OFF
 * 1 = Record ON
 */
uint8_t output_control_is_recording(void);


/*
 * 현재 녹화된 JointCommand 개수 반환
 */
uint32_t output_control_get_record_count(void);


#endif /* OUTPUT_CONTROLLER_OUTPUT_CONTROL_H */