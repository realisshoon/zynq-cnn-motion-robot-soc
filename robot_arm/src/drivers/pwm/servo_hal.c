#include "drivers/pwm/servo_hal.h"
#include "drivers/pwm/servo_pwm_driver.h"
#include <stddef.h>
#include <string.h>
#include "robot_board_config.h"

static int real_output_permitted, output_gate_fault;

static ServoOutputStatus selected_output;
#if defined(ROBOT_STEREO_LEFT)
#define SELECTED_PWM_BANK SERVO_PWM_BANK_LEFT
#else
#define SELECTED_PWM_BANK SERVO_PWM_BANK_RIGHT
#endif

static int selected_output_failure(void)
{
    (void)servo_hal_disable();
    selected_output.have_command = 0U;
    selected_output.mode = SERVO_OUTPUT_FAULT;
    return 0;
}

/* A failed CONTROL=0 write remains a latched fault, not proof of physical shutdown. */

static const ServoPwmDriverChannel driver_channels[SERVO_COUNT] = {
    SERVO_PWM_DRIVER_ELBOW_ROLL, SERVO_PWM_DRIVER_ELBOW_PITCH,
    SERVO_PWM_DRIVER_WRIST_PITCH, SERVO_PWM_DRIVER_WRIST_ROLL,
    SERVO_PWM_DRIVER_GRIPPER
};
void servo_hal_init(void)
{
    servo_pwm_driver_init();
    memset(&selected_output, 0, sizeof(selected_output));
    real_output_permitted = 0;
    (void)servo_hal_disable();
}

int servo_hal_output_permitted(void) { return real_output_permitted; }

int servo_hal_set_output_permitted(int permitted)
{
    int ok;
    if (!permitted) return servo_hal_disable();
    if (servo_hal_faulted()) return 0;
    ok = servo_pwm_driver_set_output_permitted_bank(SELECTED_PWM_BANK, 1);
    real_output_permitted = ok;
    if (!ok) (void)servo_hal_disable();
    return ok;
}

int servo_hal_enable(void)
{
    if (!real_output_permitted) return 0;
    if (!selected_output.have_command || selected_output.mode == SERVO_OUTPUT_FAULT) return 0;
    if (!servo_pwm_driver_disable_bank(SELECTED_PWM_BANK == SERVO_PWM_BANK_RIGHT
            ? SERVO_PWM_BANK_LEFT : SERVO_PWM_BANK_RIGHT) ||
        !servo_pwm_driver_enable_bank(SELECTED_PWM_BANK)) return selected_output_failure();
    selected_output.enabled = 1U;
    selected_output.mode = SERVO_OUTPUT_ENABLED;
    return 1;
}

int servo_hal_disable(void)
{
    real_output_permitted = 0;
    int right_ok = servo_pwm_driver_disable();
    int left_ok = servo_pwm_driver_disable_bank(SERVO_PWM_BANK_LEFT);
    selected_output.enabled = 0U;
    selected_output.mode = right_ok && left_ok ? SERVO_OUTPUT_DISABLED : SERVO_OUTPUT_FAULT;
    if (!right_ok || !left_ok) selected_output.have_command = 0U;
    output_gate_fault = !(right_ok && left_ok);
    return right_ok && left_ok;
}

const ServoOutputStatus *servo_hal_status(void)
{
    return &selected_output;
}

int servo_hal_faulted(void)
{
    const ServoOutputStatus *status = servo_hal_status();
    return output_gate_fault || (status != NULL && status->mode == SERVO_OUTPUT_FAULT);
}

int servo_hal_apply(const ServoPwmCommand *cmd)
{
    unsigned i;
    if (cmd == NULL || !real_output_permitted) return 0;
    if (servo_hal_faulted()) return 0;
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
        if (!servo_pwm_driver_write_channel_bank(SELECTED_PWM_BANK,
                driver_channels[i], values[i])) return selected_output_failure();
    }
    if (!servo_pwm_driver_update_bank(SELECTED_PWM_BANK)) return selected_output_failure();
    selected_output.have_command = 1U;
    return 1;
}

int servo_hal_apply_joint_command(const ForearmJointCommand *command,
                                 const ServoPwmCommand *pwm, int control_tick)
{
    if (!real_output_permitted) return 0;
    ServoPwmCommand expected;
    (void)control_tick;
    if (command == NULL || pwm == NULL || !forearm_safety_check_apply(command, NULL) ||
        !servo_control_convert(command, &expected) || memcmp(&expected, pwm, sizeof(expected)) != 0)
        return 0;
    if (!servo_hal_apply(pwm)) return 0;
    selected_output.applied = *command;
    return 1;
}

int servo_hal_startup(void)
{
    /* No automatic center command or enable. E prepares its reference explicitly. */
    return servo_hal_disable();
}
