#include "camera_tracking_app.h"
#include "xtime_l.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

u32 Xil_In32(UINTPTR address) { (void)address; assert(0); return 0; }
void Xil_Out32(UINTPTR address, u32 value) { (void)address; (void)value; assert(0); }
void XTime_GetTime(XTime *time) { *time = 1000000; }
int xil_printf(const char *format, ...) { (void)format; return 0; }
int XUartPs_IsReceiveData(UINTPTR base) { (void)base; return 0; }
u32 XUartPs_ReadReg(UINTPTR base, u32 offset) { (void)base; (void)offset; return 0; }
u8 XUartPs_RecvByte(UINTPTR base) { (void)base; return '\n'; }

int main(void)
{
    camera_tracking_app_t app;
    memset(&app, 0, sizeof(app));
    assert(camera_gimbal_pwm_default_base() == 0);
    assert(camera_tracking_app_init(&app) == XST_SUCCESS);
    assert(!app.gimbal.enabled && !app.tracker.enabled);
    assert(strcmp(camera_tracking_app_mode_string(&app), "FIXED(NO PWM IP)") == 0);
    camera_tracking_app_fix(&app);
    assert(camera_tracking_app_handle_key(&app, 'u'));
    assert(!app.tracker.enabled && !app.gimbal.enabled);
    assert(camera_tracking_app_handle_key(&app, 'h'));
    assert(camera_tracking_app_handle_key(&app, 'v'));
    camera_gimbal_pwm_set_limits(&app.gimbal, 1000, 2000);
    camera_gimbal_pwm_center(&app.gimbal);
    camera_gimbal_pwm_set_enable(&app.gimbal, 1);
    assert(!app.gimbal.enabled && camera_gimbal_pwm_control_readback(&app.gimbal) == 0);
    camera_tracking_app_service(&app);
    puts("test_camera_missing_pwm: PASS (no MMIO to absent IP)");
    return 0;
}
