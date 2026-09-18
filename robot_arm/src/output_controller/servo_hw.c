#include "output_controller/servo_hw.h"

#include <stdint.h>
#include <stddef.h>


/*
 * ============================================================
 * Vitis Hardware Access
 * ============================================================
 *
 * Vitis 실제 Hardware 환경:
 *
 *   SERVO_HW_USE_XILINX 정의
 *   -> Xil_Out32() 사용
 *
 * Host / WSL Test:
 *
 *   SERVO_HW_USE_XILINX 미정의
 *   -> Mock Register 사용
 */
#ifdef SERVO_HW_USE_XILINX

#include "xil_io.h"

#endif


/*
 * ============================================================
 * Servo Hardware State
 * ============================================================
 */
static uintptr_t g_servo_hw_base_addr = 0U;

static uint8_t g_servo_hw_initialized = 0U;


/*
 * ============================================================
 * Host Test Mock Registers
 * ============================================================
 *
 * 0 -> 0x00 BASE
 * 1 -> 0x04 SHOULDER
 * 2 -> 0x08 ELBOW
 * 3 -> 0x0C WRIST_PITCH
 * 4 -> 0x10 WRIST_ROLL
 * 5 -> 0x14 GRIPPER
 * 6 -> 0x18 CONTROL
 * 7 -> 0x1C UPDATE
 */
#ifndef SERVO_HW_USE_XILINX

#define SERVO_HW_REGISTER_COUNT 8U

static uint32_t
    g_servo_hw_mock_regs[SERVO_HW_REGISTER_COUNT];

#endif


/*
 * ============================================================
 * Register Address
 * ============================================================
 */
#ifdef SERVO_HW_USE_XILINX

static uintptr_t servo_hw_reg_addr(
    uint32_t offset
)
{
    return
        g_servo_hw_base_addr
        +
        (uintptr_t)offset;
}

#endif


/*
 * ============================================================
 * 32-bit Register Write
 * ============================================================
 */
static void servo_hw_write32(
    uint32_t offset,
    uint32_t value
)
{
#ifdef SERVO_HW_USE_XILINX

    /*
     * Actual AXI4-Lite Write
     */
    Xil_Out32(
        servo_hw_reg_addr(offset),
        value
    );

#else

    /*
     * Host Test
     */
    uint32_t index =
        offset / 4U;

    if (index <
        SERVO_HW_REGISTER_COUNT) {

        g_servo_hw_mock_regs[index] =
            value;
    }

#endif
}


/*
 * ============================================================
 * Initialization
 * ============================================================
 */
void servo_hw_init(
    uintptr_t base_addr
)
{
    g_servo_hw_base_addr =
        base_addr;

    g_servo_hw_initialized =
        1U;


#ifndef SERVO_HW_USE_XILINX

    /*
     * Mock Register 초기화
     */
    for (uint32_t i = 0U;
         i < SERVO_HW_REGISTER_COUNT;
         ++i) {

        g_servo_hw_mock_regs[i] =
            0U;
    }

#endif
}


/*
 * ============================================================
 * PWM Enable
 * ============================================================
 *
 * CONTROL
 * 0x18 bit0 = 1
 */
void servo_hw_enable(void)
{
    if (!g_servo_hw_initialized) {
        return;
    }

    servo_hw_write32(
        SERVO_HW_CONTROL_OFFSET,
        SERVO_HW_CONTROL_ENABLE
    );
}


/*
 * ============================================================
 * PWM Disable
 * ============================================================
 *
 * CONTROL
 * 0x18 bit0 = 0
 */
void servo_hw_disable(void)
{
    if (!g_servo_hw_initialized) {
        return;
    }

    servo_hw_write32(
        SERVO_HW_CONTROL_OFFSET,
        SERVO_HW_CONTROL_DISABLE
    );
}


/*
 * ============================================================
 * Servo PWM Shadow Register Write
 * ============================================================
 */
int servo_hw_write_shadow(
    const ServoPwmCommand *cmd
)
{
    if (!g_servo_hw_initialized) {
        return 0;
    }

    if (cmd == NULL) {
        return 0;
    }


    /*
     * Robot BASE
     *
     * AXI 0x00
     * RTL SHOULDER
     */
    servo_hw_write32(
        SERVO_HW_BASE_OFFSET,
        (uint32_t)cmd->base_pwm_us
    );


    /*
     * Robot SHOULDER
     *
     * AXI 0x04
     * RTL SHOULDER_ROLL
     */
    servo_hw_write32(
        SERVO_HW_SHOULDER_OFFSET,
        (uint32_t)cmd->shoulder_pwm_us
    );


    /*
     * Robot ELBOW
     *
     * AXI 0x08
     */
    servo_hw_write32(
        SERVO_HW_ELBOW_OFFSET,
        (uint32_t)cmd->elbow_pwm_us
    );


    /*
     * Robot WRIST PITCH
     *
     * AXI 0x0C
     * RTL WRIST
     */
    servo_hw_write32(
        SERVO_HW_WRIST_PITCH_OFFSET,
        (uint32_t)cmd->wrist_pitch_pwm_us
    );


    /*
     * Robot WRIST ROLL
     *
     * AXI 0x10
     */
    servo_hw_write32(
        SERVO_HW_WRIST_ROLL_OFFSET,
        (uint32_t)cmd->wrist_roll_pwm_us
    );


    /*
     * Robot GRIPPER
     *
     * AXI 0x14
     */
    servo_hw_write32(
        SERVO_HW_GRIPPER_OFFSET,
        (uint32_t)cmd->gripper_pwm_us
    );


    return 1;
}


/*
 * ============================================================
 * UPDATE
 * ============================================================
 *
 * AXI 0x1C <- 1
 *
 * RTL에서 이 Write Event를 감지하여
 * 6개 Shadow 값을 동시에 Active 값으로 적용한다.
 */
void servo_hw_update(void)
{
    if (!g_servo_hw_initialized) {
        return;
    }

    servo_hw_write32(
        SERVO_HW_UPDATE_OFFSET,
        SERVO_HW_UPDATE_APPLY
    );
}


/*
 * ============================================================
 * Servo PWM Apply
 * ============================================================
 *
 * 1. 6개 Shadow Register Write
 * 2. UPDATE = 1
 */
int servo_hw_apply(
    const ServoPwmCommand *cmd
)
{
    if (!servo_hw_write_shadow(cmd)) {
        return 0;
    }

    servo_hw_update();

    return 1;
}