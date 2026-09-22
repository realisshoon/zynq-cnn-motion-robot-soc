#include "drivers/servo_pwm_driver.h"

#include <stddef.h>


/* Fixed six-channel AXI IP. Five logical servos use CH0..CH4.
 * CH5 at 0x14 is unused and never written. CONTROL/UPDATE must not move. */
#define SERVO_PWM_ELBOW_ROLL_OFFSET    0x00U
#define SERVO_PWM_ELBOW_PITCH_OFFSET   0x04U
#define SERVO_PWM_WRIST_PITCH_OFFSET   0x08U
#define SERVO_PWM_WRIST_ROLL_OFFSET    0x0CU
#define SERVO_PWM_GRIPPER_OFFSET       0x10U

#define SERVO_PWM_CONTROL_OFFSET       0x18U
#define SERVO_PWM_UPDATE_OFFSET        0x1CU


#define SERVO_PWM_CONTROL_DISABLE      0U
#define SERVO_PWM_CONTROL_ENABLE       1U

#define SERVO_PWM_UPDATE_APPLY         1U


#define SERVO_PWM_REGISTER_COUNT       8U


/*
 * ============================================================
 * Xilinx / Host Selection
 * ============================================================
 */
#ifdef SERVO_PWM_DRIVER_USE_XILINX

#include "xparameters.h"
#include "xil_io.h"
#include "xil_types.h"


/*
 * 최종 Vitis에서 xparameters.h를 확인한 뒤
 * 실제 생성된 Macro 이름을 사용한다.
 *
 * 현재 예상 이름:
 *
 * XPAR_SERVO_PWM_0_S00_AXI_BASEADDR
 */
#ifndef SERVO_PWM_DRIVER_BASEADDR

#ifdef XPAR_SERVO_PWM_0_S00_AXI_BASEADDR

#define SERVO_PWM_DRIVER_BASEADDR \
    XPAR_SERVO_PWM_0_S00_AXI_BASEADDR

#else

#error "SERVO PWM AXI Base Address is not defined. Check xparameters.h."

#endif

#endif


#else


/*
 * ============================================================
 * HOST / WSL Mock Register
 * ============================================================
 */

#define SERVO_PWM_MOCK_LOG_CAPACITY 128U


static uint32_t g_servo_pwm_mock_regs[
    SERVO_PWM_REGISTER_COUNT
];


static ServoPwmDriverMockWrite g_servo_pwm_mock_log[
    SERVO_PWM_MOCK_LOG_CAPACITY
];


static uint32_t g_servo_pwm_mock_log_count = 0U;


#endif


/*
 * ============================================================
 * Driver State
 * ============================================================
 */

static uint8_t g_servo_pwm_driver_initialized = 0U;


/*
 * ============================================================
 * Internal AXI Write
 * ============================================================
 */
static int servo_pwm_driver_write32(
    uint32_t offset,
    uint32_t value
)
{
    if (!g_servo_pwm_driver_initialized) {
        return 0;
    }


#ifdef SERVO_PWM_DRIVER_USE_XILINX

    /*
     * Actual Zybo / AXI Write
     */
    Xil_Out32(
        (UINTPTR)(
            SERVO_PWM_DRIVER_BASEADDR
            +
            offset
        ),
        value
    );

    return 1;


#else

    uint32_t register_index;


    /*
     * Register Address 검사.
     */
    if ((offset & 0x03U) != 0U) {
        return 0;
    }


    register_index =
        offset >> 2U;


    if (register_index >=
        SERVO_PWM_REGISTER_COUNT) {

        return 0;
    }


    /*
     * Write Log Overflow 방지.
     */
    if (g_servo_pwm_mock_log_count >=
        SERVO_PWM_MOCK_LOG_CAPACITY) {

        return 0;
    }


    /*
     * Mock Register Write.
     */
    g_servo_pwm_mock_regs[
        register_index
    ] = value;


    /*
     * Write 순서 기록.
     */
    g_servo_pwm_mock_log[
        g_servo_pwm_mock_log_count
    ].offset = offset;


    g_servo_pwm_mock_log[
        g_servo_pwm_mock_log_count
    ].value = value;


    g_servo_pwm_mock_log_count++;


    return 1;

#endif
}


/*
 * ============================================================
 * Driver Initialization
 * ============================================================
 */
void servo_pwm_driver_init(void)
{

#ifndef SERVO_PWM_DRIVER_USE_XILINX

    uint32_t i;


    /*
     * Mock Register 초기화.
     */
    for (i = 0U;
         i < SERVO_PWM_REGISTER_COUNT;
         ++i) {

        g_servo_pwm_mock_regs[i] = 0U;
    }


    /*
     * Mock Write Log 초기화.
     */
    for (i = 0U;
         i < SERVO_PWM_MOCK_LOG_CAPACITY;
         ++i) {

        g_servo_pwm_mock_log[i].offset = 0U;
        g_servo_pwm_mock_log[i].value  = 0U;
    }


    g_servo_pwm_mock_log_count = 0U;

#endif


    g_servo_pwm_driver_initialized = 1U;
}


/*
 * ============================================================
 * PWM Enable
 * ============================================================
 */
int servo_pwm_driver_enable(void)
{
    return servo_pwm_driver_write32(
        SERVO_PWM_CONTROL_OFFSET,
        SERVO_PWM_CONTROL_ENABLE
    );
}


/*
 * ============================================================
 * PWM Disable
 * ============================================================
 */
int servo_pwm_driver_disable(void)
{
    return servo_pwm_driver_write32(
        SERVO_PWM_CONTROL_OFFSET,
        SERVO_PWM_CONTROL_DISABLE
    );
}


/*
 * ============================================================
 * Channel Write
 * ============================================================
 */
int servo_pwm_driver_write_channel(
    ServoPwmDriverChannel channel,
    uint16_t pwm_us
)
{
    uint32_t offset;


    switch (channel) {

        case SERVO_PWM_DRIVER_ELBOW_ROLL:

            offset =
                SERVO_PWM_ELBOW_ROLL_OFFSET;

            break;


        case SERVO_PWM_DRIVER_ELBOW_PITCH:

            offset =
                SERVO_PWM_ELBOW_PITCH_OFFSET;

            break;


        case SERVO_PWM_DRIVER_WRIST_PITCH:

            offset =
                SERVO_PWM_WRIST_PITCH_OFFSET;

            break;


        case SERVO_PWM_DRIVER_WRIST_ROLL:

            offset =
                SERVO_PWM_WRIST_ROLL_OFFSET;

            break;


        case SERVO_PWM_DRIVER_GRIPPER:

            offset =
                SERVO_PWM_GRIPPER_OFFSET;

            break;


        default:

            return 0;
    }


    return servo_pwm_driver_write32(
        offset,
        (uint32_t)pwm_us
    );
}


/*
 * ============================================================
 * UPDATE
 * ============================================================
 */
int servo_pwm_driver_update(void)
{
    return servo_pwm_driver_write32(
        SERVO_PWM_UPDATE_OFFSET,
        SERVO_PWM_UPDATE_APPLY
    );
}


#ifndef SERVO_PWM_DRIVER_USE_XILINX

/*
 * ============================================================
 * Mock Reset
 * ============================================================
 */
void servo_pwm_driver_mock_reset(void)
{
    uint32_t i;


    for (i = 0U;
         i < SERVO_PWM_REGISTER_COUNT;
         ++i) {

        g_servo_pwm_mock_regs[i] = 0U;
    }


    for (i = 0U;
         i < SERVO_PWM_MOCK_LOG_CAPACITY;
         ++i) {

        g_servo_pwm_mock_log[i].offset = 0U;
        g_servo_pwm_mock_log[i].value  = 0U;
    }


    g_servo_pwm_mock_log_count = 0U;


    /*
     * 완전한 Power-On Reset 상태처럼 만든다.
     */
    g_servo_pwm_driver_initialized = 0U;
}


/*
 * ============================================================
 * Mock Log Count
 * ============================================================
 */
uint32_t servo_pwm_driver_mock_get_log_count(void)
{
    return g_servo_pwm_mock_log_count;
}


/*
 * ============================================================
 * Mock Log Read
 * ============================================================
 */
int servo_pwm_driver_mock_get_log(
    uint32_t index,
    ServoPwmDriverMockWrite *entry
)
{
    if (entry == NULL) {
        return 0;
    }


    if (index >=
        g_servo_pwm_mock_log_count) {

        return 0;
    }


    *entry =
        g_servo_pwm_mock_log[index];


    return 1;
}

#endif