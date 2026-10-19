#include "camera_gimbal_pwm.h"

#include "xil_io.h"
#include "xil_printf.h"
#include "xparameters.h"
#include "xtime_l.h"

#define PWM_CH0_OFFSET       0x00U
#define PWM_CH1_OFFSET       0x04U
#define PWM_CH2_OFFSET       0x08U
#define PWM_CH3_OFFSET       0x0CU
#define PWM_CH4_OFFSET       0x10U
#define PWM_CH5_OFFSET       0x14U
#define PWM_CONTROL_OFFSET   0x18U
#define PWM_UPDATE_OFFSET    0x1CU
#define PWM_CONTROL_ENABLE   0x00000001U

#if defined(XPAR_SERVO_PWM_CAMERA_0_S00_AXI_BASEADDR)
#define CAMERA_GIMBAL_PWM_BASE XPAR_SERVO_PWM_CAMERA_0_S00_AXI_BASEADDR
#elif defined(XPAR_SERVO_PWM_CAMERA_0_BASEADDR)
#define CAMERA_GIMBAL_PWM_BASE XPAR_SERVO_PWM_CAMERA_0_BASEADDR
#else
#define CAMERA_GIMBAL_PWM_BASE 0U
#endif

static u16 clamp_u16(u16 value, u16 low, u16 high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

static void pwm_write(const camera_gimbal_pwm_t *gimbal, u32 offset, u32 value)
{
    if (gimbal->base_address == 0) return;
    Xil_Out32(gimbal->base_address + offset, value);
}

static void pwm_commit(const camera_gimbal_pwm_t *gimbal)
{
    pwm_write(gimbal, PWM_CH0_OFFSET,
              gimbal->current_pulse_us[CAMERA_GIMBAL_PAN]);
    pwm_write(gimbal, PWM_CH1_OFFSET,
              gimbal->current_pulse_us[CAMERA_GIMBAL_TILT]);
    pwm_write(gimbal, PWM_UPDATE_OFFSET, 1U);
}

static void pwm_commit_all_zero(const camera_gimbal_pwm_t *gimbal)
{
    pwm_write(gimbal, PWM_CH0_OFFSET, 0U);
    pwm_write(gimbal, PWM_CH1_OFFSET, 0U);
    pwm_write(gimbal, PWM_CH2_OFFSET, 0U);
    pwm_write(gimbal, PWM_CH3_OFFSET, 0U);
    pwm_write(gimbal, PWM_CH4_OFFSET, 0U);
    pwm_write(gimbal, PWM_CH5_OFFSET, 0U);
    pwm_write(gimbal, PWM_UPDATE_OFFSET, 1U);
}

UINTPTR camera_gimbal_pwm_default_base(void)
{
    return (UINTPTR)CAMERA_GIMBAL_PWM_BASE;
}

int camera_gimbal_pwm_init(camera_gimbal_pwm_t *gimbal, UINTPTR base_address)
{
    unsigned int axis;

    if (gimbal == 0)
        return XST_INVALID_PARAM;

    gimbal->base_address = base_address;
    gimbal->slew_step_us = 10U;
    XTime_GetTime((XTime *)&gimbal->last_service_ticks);
    gimbal->enabled = base_address != 0;
    gimbal->initialized = 1U;

    for (axis = 0U; axis < CAMERA_GIMBAL_AXIS_COUNT; ++axis) {
        gimbal->min_pulse_us[axis] = 1000U;
        gimbal->max_pulse_us[axis] = 2000U;
        gimbal->center_pulse_us[axis] = 1500U;
        gimbal->current_pulse_us[axis] = 1500U;
        gimbal->target_pulse_us[axis] = 1500U;
    }

    if (base_address == 0) {
        xil_printf("camera gimbal PWM: absent from XSA; no MMIO, physically fix cameras\r\n");
        return XST_SUCCESS;
    }
    /* Only channels 0/1 leave the new camera PWM instance. */
    pwm_write(gimbal, PWM_CH2_OFFSET, 0U);
    pwm_write(gimbal, PWM_CH3_OFFSET, 0U);
    pwm_write(gimbal, PWM_CH4_OFFSET, 0U);
    pwm_write(gimbal, PWM_CH5_OFFSET, 0U);
    pwm_write(gimbal, PWM_CONTROL_OFFSET, PWM_CONTROL_ENABLE);
    pwm_commit(gimbal);

    xil_printf("camera gimbal PWM: base=0x%08x pan=ch0 tilt=ch1 centered\r\n",
               (unsigned int)gimbal->base_address);
    return XST_SUCCESS;
}

void camera_gimbal_pwm_set_enable(camera_gimbal_pwm_t *gimbal, int enabled)
{
    u32 control;

    if (gimbal == 0 || !gimbal->initialized)
        return;
    if (gimbal->base_address == 0) {
        gimbal->enabled = 0;
        return;
    }

    if (enabled) {
        /* Restore the retained software pulse values before restarting. */
        pwm_commit(gimbal);
        pwm_write(gimbal, PWM_CONTROL_OFFSET, PWM_CONTROL_ENABLE);
    }
    else {
        /* Stop immediately, then leave a zero-width fallback committed too. */
        pwm_write(gimbal, PWM_CONTROL_OFFSET, 0U);
        pwm_commit_all_zero(gimbal);
        pwm_write(gimbal, PWM_CONTROL_OFFSET, 0U);
    }

    control = camera_gimbal_pwm_control_readback(gimbal);
    gimbal->enabled = (control & PWM_CONTROL_ENABLE) ? 1U : 0U;
    if (gimbal->enabled != (enabled ? 1U : 0U)) {
        xil_printf("camera gimbal PWM CONTROL mismatch: requested=%u "
                   "readback=%08x\r\n",
                   enabled ? 1U : 0U, (unsigned int)control);
    }
}

u32 camera_gimbal_pwm_control_readback(const camera_gimbal_pwm_t *gimbal)
{
    if (gimbal == 0 || !gimbal->initialized || gimbal->base_address == 0)
        return 0U;
    return Xil_In32(gimbal->base_address + PWM_CONTROL_OFFSET);
}

void camera_gimbal_pwm_set_limits(camera_gimbal_pwm_t *gimbal,
                                  u16 min_pulse_us, u16 max_pulse_us)
{
    unsigned int axis;

    if (gimbal == 0 || min_pulse_us >= max_pulse_us)
        return;
    for (axis = 0U; axis < CAMERA_GIMBAL_AXIS_COUNT; ++axis) {
        gimbal->min_pulse_us[axis] = min_pulse_us;
        gimbal->max_pulse_us[axis] = max_pulse_us;
        gimbal->current_pulse_us[axis] = clamp_u16(
            gimbal->current_pulse_us[axis], min_pulse_us, max_pulse_us);
        gimbal->target_pulse_us[axis] = clamp_u16(
            gimbal->target_pulse_us[axis], min_pulse_us, max_pulse_us);
    }
    pwm_commit(gimbal);
}

void camera_gimbal_pwm_set_centers(camera_gimbal_pwm_t *gimbal,
                                   u16 pan_center_us, u16 tilt_center_us)
{
    if (gimbal == 0)
        return;
    gimbal->center_pulse_us[CAMERA_GIMBAL_PAN] = clamp_u16(
        pan_center_us, gimbal->min_pulse_us[CAMERA_GIMBAL_PAN],
        gimbal->max_pulse_us[CAMERA_GIMBAL_PAN]);
    gimbal->center_pulse_us[CAMERA_GIMBAL_TILT] = clamp_u16(
        tilt_center_us, gimbal->min_pulse_us[CAMERA_GIMBAL_TILT],
        gimbal->max_pulse_us[CAMERA_GIMBAL_TILT]);
}

void camera_gimbal_pwm_set_slew(camera_gimbal_pwm_t *gimbal, u16 step_us)
{
    if (gimbal == 0)
        return;
    gimbal->slew_step_us = step_us == 0U ? 1U : step_us;
}

void camera_gimbal_pwm_set_target(camera_gimbal_pwm_t *gimbal,
                                  camera_gimbal_axis_t axis, u16 pulse_us)
{
    if (gimbal == 0 || axis >= CAMERA_GIMBAL_AXIS_COUNT)
        return;
    gimbal->target_pulse_us[axis] = clamp_u16(
        pulse_us, gimbal->min_pulse_us[axis], gimbal->max_pulse_us[axis]);
}

void camera_gimbal_pwm_center(camera_gimbal_pwm_t *gimbal)
{
    unsigned int axis;

    if (gimbal == 0)
        return;
    for (axis = 0U; axis < CAMERA_GIMBAL_AXIS_COUNT; ++axis) {
        gimbal->target_pulse_us[axis] = gimbal->center_pulse_us[axis];
        gimbal->current_pulse_us[axis] = gimbal->center_pulse_us[axis];
    }
    pwm_commit(gimbal);
}

int camera_gimbal_pwm_service(camera_gimbal_pwm_t *gimbal)
{
    unsigned int axis;
    int changed = 0;
    XTime now;
    u64 service_period = (u64)COUNTS_PER_SECOND / 50U;

    if (gimbal == 0 || !gimbal->initialized || !gimbal->enabled)
        return 0;
    XTime_GetTime(&now);
    if (((u64)now - gimbal->last_service_ticks) < service_period)
        return 0;
    gimbal->last_service_ticks = (u64)now;

    for (axis = 0U; axis < CAMERA_GIMBAL_AXIS_COUNT; ++axis) {
        u16 current = gimbal->current_pulse_us[axis];
        u16 target = gimbal->target_pulse_us[axis];
        u16 step = gimbal->slew_step_us;

        if (current < target) {
            u16 distance = (u16)(target - current);
            current = (u16)(current + (distance < step ? distance : step));
            changed = 1;
        }
        else if (current > target) {
            u16 distance = (u16)(current - target);
            current = (u16)(current - (distance < step ? distance : step));
            changed = 1;
        }
        gimbal->current_pulse_us[axis] = current;
    }

    if (changed)
        pwm_commit(gimbal);
    return changed;
}

void camera_gimbal_pwm_print(const camera_gimbal_pwm_t *gimbal)
{
    if (gimbal == 0)
        return;
    xil_printf("gimbal PWM base=%08x enabled=%u control-readback=%08x "
               "slew=%u us/update\r\n",
               (unsigned int)gimbal->base_address,
               (unsigned int)gimbal->enabled,
               (unsigned int)camera_gimbal_pwm_control_readback(gimbal),
               (unsigned int)gimbal->slew_step_us);
    xil_printf("  pan  current/target/center=%u/%u/%u us range=%u..%u\r\n",
               gimbal->current_pulse_us[0], gimbal->target_pulse_us[0],
               gimbal->center_pulse_us[0], gimbal->min_pulse_us[0],
               gimbal->max_pulse_us[0]);
    xil_printf("  tilt current/target/center=%u/%u/%u us range=%u..%u\r\n",
               gimbal->current_pulse_us[1], gimbal->target_pulse_us[1],
               gimbal->center_pulse_us[1], gimbal->min_pulse_us[1],
               gimbal->max_pulse_us[1]);
}

