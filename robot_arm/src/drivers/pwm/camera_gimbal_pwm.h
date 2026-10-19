#ifndef CAMERA_GIMBAL_PWM_H
#define CAMERA_GIMBAL_PWM_H

#include "xil_types.h"
#include "xstatus.h"

typedef enum {
    CAMERA_GIMBAL_PAN = 0,
    CAMERA_GIMBAL_TILT = 1,
    CAMERA_GIMBAL_AXIS_COUNT = 2
} camera_gimbal_axis_t;

typedef struct {
    UINTPTR base_address;
    u16 min_pulse_us[CAMERA_GIMBAL_AXIS_COUNT];
    u16 max_pulse_us[CAMERA_GIMBAL_AXIS_COUNT];
    u16 center_pulse_us[CAMERA_GIMBAL_AXIS_COUNT];
    u16 current_pulse_us[CAMERA_GIMBAL_AXIS_COUNT];
    u16 target_pulse_us[CAMERA_GIMBAL_AXIS_COUNT];
    u16 slew_step_us;
    u64 last_service_ticks;
    u8 enabled;
    u8 initialized;
} camera_gimbal_pwm_t;

UINTPTR camera_gimbal_pwm_default_base(void);
int camera_gimbal_pwm_init(camera_gimbal_pwm_t *gimbal, UINTPTR base_address);
void camera_gimbal_pwm_set_enable(camera_gimbal_pwm_t *gimbal, int enabled);
u32 camera_gimbal_pwm_control_readback(const camera_gimbal_pwm_t *gimbal);
void camera_gimbal_pwm_set_limits(camera_gimbal_pwm_t *gimbal,
                                  u16 min_pulse_us, u16 max_pulse_us);
void camera_gimbal_pwm_set_centers(camera_gimbal_pwm_t *gimbal,
                                   u16 pan_center_us, u16 tilt_center_us);
void camera_gimbal_pwm_set_slew(camera_gimbal_pwm_t *gimbal, u16 step_us);
void camera_gimbal_pwm_set_target(camera_gimbal_pwm_t *gimbal,
                                  camera_gimbal_axis_t axis, u16 pulse_us);
void camera_gimbal_pwm_center(camera_gimbal_pwm_t *gimbal);
int camera_gimbal_pwm_service(camera_gimbal_pwm_t *gimbal);
void camera_gimbal_pwm_print(const camera_gimbal_pwm_t *gimbal);

#endif


