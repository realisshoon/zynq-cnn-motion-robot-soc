#ifndef OUTPUT_CONTROLLER_SERVO_HW_H
#define OUTPUT_CONTROLLER_SERVO_HW_H

#include <stdint.h>

#include "output_controller/servo_control.h"


/*
 * ============================================================
 * Servo PWM AXI Register Map
 * ============================================================
 *
 * Robot Joint       AXI Offset     RTL Register Name
 *
 * BASE              0x00           SHOULDER
 * SHOULDER          0x04           SHOULDER_ROLL
 * ELBOW             0x08           ELBOW
 * WRIST_PITCH       0x0C           WRIST
 * WRIST_ROLL        0x10           WRIST_ROLL
 * GRIPPER           0x14           GRIPPER
 *
 * CONTROL           0x18
 * UPDATE            0x1C
 */


/*
 * Servo PWM Shadow Registers
 */
#define SERVO_HW_BASE_OFFSET          0x00U
#define SERVO_HW_SHOULDER_OFFSET      0x04U
#define SERVO_HW_ELBOW_OFFSET         0x08U
#define SERVO_HW_WRIST_PITCH_OFFSET   0x0CU
#define SERVO_HW_WRIST_ROLL_OFFSET    0x10U
#define SERVO_HW_GRIPPER_OFFSET       0x14U


/*
 * Control Registers
 */
#define SERVO_HW_CONTROL_OFFSET       0x18U
#define SERVO_HW_UPDATE_OFFSET        0x1CU


/*
 * CONTROL bit0
 */
#define SERVO_HW_CONTROL_DISABLE      0x00000000U
#define SERVO_HW_CONTROL_ENABLE       0x00000001U


/*
 * UPDATE bit0
 */
#define SERVO_HW_UPDATE_APPLY         0x00000001U


/*
 * AXI Servo PWM Hardware 초기화
 *
 * base_addr:
 *   Vivado/Vitis에서 생성된 servo_pwm IP Base Address
 */
void servo_hw_init(
    uintptr_t base_addr
);


/*
 * PWM Output Enable / Disable
 */
void servo_hw_enable(void);
void servo_hw_disable(void);


/*
 * 6개 Servo PWM 값을
 * AXI Shadow Register에 기록한다.
 *
 * UPDATE는 발생시키지 않는다.
 */
int servo_hw_write_shadow(
    const ServoPwmCommand *cmd
);


/*
 * UPDATE Register에 1을 Write하여
 * 6개 Shadow 값을 동시에 적용한다.
 */
void servo_hw_update(void);


/*
 * Shadow Write + UPDATE
 */
int servo_hw_apply(
    const ServoPwmCommand *cmd
);


#endif /* OUTPUT_CONTROLLER_SERVO_HW_H */