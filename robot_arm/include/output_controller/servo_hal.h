#ifndef OUTPUT_CONTROLLER_SERVO_HAL_H
#define OUTPUT_CONTROLLER_SERVO_HAL_H

#include "output_controller/servo_control.h"


/*
 * ============================================================
 * Servo HAL Initialization
 * ============================================================
 */
void servo_hal_init(void);


/*
 * ============================================================
 * PWM Enable / Disable
 * ============================================================
 *
 * return:
 *
 * 1 = success
 * 0 = fail
 */
int servo_hal_enable(void);

int servo_hal_disable(void);


/*
 * ============================================================
 * Servo PWM Apply
 * ============================================================
 *
 * 동작 순서:
 *
 * 1. 6개 PWM 값 전체 범위 검사
 *
 * 2. Shadow Register 6개 Write
 *
 * 3. UPDATE
 *
 * 하나라도 잘못된 PWM 값이면
 * Register Write를 시작하지 않는다.
 *
 * return:
 *
 * 1 = success
 * 0 = fail
 */
int servo_hal_apply(
    const ServoPwmCommand *cmd
);


/*
 * ============================================================
 * Startup Safe Pose
 * ============================================================
 *
 * 현재 ServoConfig center 값 사용:
 *
 * BASE        = 1500 us
 * SHOULDER    = 1500 us
 * ELBOW       = 1500 us
 * WRIST_PITCH = 1500 us
 * WRIST_ROLL  = 1500 us
 * GRIPPER     = 1500 us
 *
 * 동작 순서:
 *
 * Shadow x6
 *    ↓
 * UPDATE
 *    ↓
 * ENABLE
 *
 * return:
 *
 * 1 = success
 * 0 = fail
 */
int servo_hal_startup(void);


#endif /* OUTPUT_CONTROLLER_SERVO_HAL_H */