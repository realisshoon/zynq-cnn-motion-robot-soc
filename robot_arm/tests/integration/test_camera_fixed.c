#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "camera_tracking_app.h"
#include "xparameters.h"
#include "xil_io.h"
#include "xtime_l.h"
#include "xuartps_hw.h"

static u32 camera_registers[8];
static XTime now_ticks;

u32 Xil_In32(UINTPTR address)
{
    assert(address >= XPAR_SERVO_PWM_CAMERA_0_S00_AXI_BASEADDR);
    assert(address < XPAR_SERVO_PWM_CAMERA_0_S00_AXI_BASEADDR + sizeof(camera_registers));
    return camera_registers[(address - XPAR_SERVO_PWM_CAMERA_0_S00_AXI_BASEADDR) / 4U];
}

void Xil_Out32(UINTPTR address, u32 value)
{
    assert(address >= XPAR_SERVO_PWM_CAMERA_0_S00_AXI_BASEADDR);
    assert(address < XPAR_SERVO_PWM_CAMERA_0_S00_AXI_BASEADDR + sizeof(camera_registers));
    camera_registers[(address - XPAR_SERVO_PWM_CAMERA_0_S00_AXI_BASEADDR) / 4U] = value;
}

void XTime_GetTime(XTime *time) { *time = now_ticks; }

int xil_printf(const char *format, ...)
{
    (void)format;
    return 0;
}

int XUartPs_IsReceiveData(UINTPTR base) { (void)base; return 0; }
u8 XUartPs_ReadReg(UINTPTR base, u32 offset)
{
    (void)base;
    (void)offset;
    return 0U;
}
u8 XUartPs_RecvByte(UINTPTR base) { (void)base; return '\n'; }

int main(void)
{
    camera_tracking_app_t app;
    cnn_result_t result;

    memset(&app, 0, sizeof(app));
    memset(&result, 0, sizeof(result));
    assert(camera_tracking_app_init(&app) == XST_SUCCESS);
    assert(strcmp(camera_tracking_app_mode_string(&app), "FIXED") == 0);
    assert(!app.tracker.enabled && app.gimbal.enabled);
    assert(camera_registers[0] == 1500U && camera_registers[1] == 1500U);
    assert(camera_registers[6] == 1U);

    camera_gimbal_pwm_set_target(&app.gimbal, CAMERA_GIMBAL_PAN, 1700U);
    now_ticks += COUNTS_PER_SECOND / 50U;
    assert(camera_gimbal_pwm_service(&app.gimbal));
    assert(app.gimbal.current_pulse_us[CAMERA_GIMBAL_PAN] == 1510U);
    assert(camera_tracking_app_handle_key(&app, 'f'));
    assert(strcmp(camera_tracking_app_mode_string(&app), "FIXED") == 0);
    assert(app.gimbal.target_pulse_us[CAMERA_GIMBAL_PAN] == 1510U);
    assert(camera_registers[0] == 1510U && camera_registers[6] == 1U);
    result.frame_id = 1U;
    camera_tracking_app_on_result(&app, &result);
    assert(app.gimbal.target_pulse_us[CAMERA_GIMBAL_PAN] == 1510U);

    assert(camera_tracking_app_handle_key(&app, 'u'));
    assert(strcmp(camera_tracking_app_mode_string(&app), "TRACKING") == 0);
    assert(camera_tracking_app_handle_key(&app, 'u'));
    assert(strcmp(camera_tracking_app_mode_string(&app), "OFF") == 0);
    assert(camera_registers[6] == 0U);
    assert(camera_tracking_app_handle_key(&app, 'f'));
    assert(strcmp(camera_tracking_app_mode_string(&app), "FIXED") == 0);
    assert(camera_registers[0] == 1510U && camera_registers[6] == 1U);
    assert(camera_tracking_app_handle_key(&app, 'h'));
    assert(camera_registers[0] == 1500U && camera_registers[1] == 1500U);
    puts("test_camera_fixed: PASS");
    return 0;
}
