#include "output_controller/servo_hal.h"
#include "drivers/servo_pwm_driver.h"
#include <stddef.h>

static const ServoPwmDriverChannel driver_channels[SERVO_COUNT] = {
    SERVO_PWM_DRIVER_ELBOW_ROLL, SERVO_PWM_DRIVER_ELBOW_PITCH,
    SERVO_PWM_DRIVER_WRIST_PITCH, SERVO_PWM_DRIVER_WRIST_ROLL,
    SERVO_PWM_DRIVER_GRIPPER
};
void servo_hal_init(void) { servo_pwm_driver_init(); }
int servo_hal_enable(void) { return servo_pwm_driver_enable(); }
int servo_hal_disable(void) { return servo_pwm_driver_disable(); }

int servo_hal_apply(const ServoPwmCommand *cmd)
{
    unsigned i;
    if (cmd == NULL) return 0;
    const uint16_t values[SERVO_COUNT] = {
        cmd->elbow_roll_pwm_us, cmd->elbow_pitch_pwm_us,
        cmd->wrist_pitch_pwm_us, cmd->wrist_roll_pwm_us, cmd->gripper_pwm_us
    };
    for (i = 0; i < SERVO_COUNT; ++i) {
        const ServoConfig *config = servo_config_get((ServoChannel)i);
        if (config == NULL || values[i] < config->min_us ||
            values[i] > config->max_us) return 0;
    }
    for (i = 0; i < SERVO_COUNT; ++i) {
        if (!servo_pwm_driver_write_channel(driver_channels[i], values[i])) return 0;
    }
    return servo_pwm_driver_update();
}

int servo_hal_startup(void)
{
    uint16_t centers[SERVO_COUNT];
    unsigned i;
    for (i = 0; i < SERVO_COUNT; ++i) {
        const ServoConfig *config = servo_config_get((ServoChannel)i);
        if (config == NULL) return 0;
        centers[i] = config->center_us;
    }
    const ServoPwmCommand cmd = {
        centers[0], centers[1], centers[2], centers[3], centers[4]
    };
    if (!servo_hal_apply(&cmd)) return 0;
    return servo_hal_enable();
}
