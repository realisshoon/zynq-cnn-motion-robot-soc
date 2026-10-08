#include "output_controller/servo_hal.h"
#include "drivers/servo_pwm_driver.h"
#include <stddef.h>
#include <string.h>
#include "dual_arm_config.h"

#if ROBOT_DUAL_ARM_ENABLE
static DualArmFollower left_follower;
#elif ROBOT_SPLIT_BOARD_CONTROL
static DualArmFollower selected_output;
#if defined(ROBOT_STEREO_LEFT)
#define SELECTED_PWM_BANK SERVO_PWM_BANK_LEFT
#else
#define SELECTED_PWM_BANK SERVO_PWM_BANK_RIGHT
#endif

static int selected_output_failure(void)
{
    (void)servo_hal_disable();
    selected_output.have_command = 0U;
    selected_output.mode = DUAL_ARM_HAL_FAULT;
    return 0;
}
#endif

static const ServoPwmDriverChannel driver_channels[SERVO_COUNT] = {
    SERVO_PWM_DRIVER_ELBOW_ROLL, SERVO_PWM_DRIVER_ELBOW_PITCH,
    SERVO_PWM_DRIVER_WRIST_PITCH, SERVO_PWM_DRIVER_WRIST_ROLL,
    SERVO_PWM_DRIVER_GRIPPER
};
void servo_hal_init(void)
{
    servo_pwm_driver_init();
#if ROBOT_DUAL_ARM_ENABLE
    dual_arm_follower_init(&left_follower);
#elif ROBOT_SPLIT_BOARD_CONTROL
    memset(&selected_output, 0, sizeof(selected_output));
    (void)servo_hal_disable();
#endif
}

int servo_hal_enable(void)
{
#if ROBOT_SPLIT_BOARD_CONTROL
    if (!selected_output.have_command || selected_output.mode == DUAL_ARM_HAL_FAULT) return 0;
    if (!servo_pwm_driver_disable_bank(SELECTED_PWM_BANK == SERVO_PWM_BANK_RIGHT
            ? SERVO_PWM_BANK_LEFT : SERVO_PWM_BANK_RIGHT) ||
        !servo_pwm_driver_enable_bank(SELECTED_PWM_BANK)) return selected_output_failure();
    selected_output.enabled = 1U;
    selected_output.mode = DUAL_ARM_TRACKING;
    return 1;
#elif ROBOT_DUAL_ARM_ENABLE
    if (!left_follower.have_command || left_follower.mode == DUAL_ARM_HAL_FAULT) return 0;
    if (!servo_pwm_driver_enable() || !servo_pwm_driver_enable_bank(SERVO_PWM_BANK_LEFT)) {
        (void)servo_hal_disable();
        left_follower.mode = DUAL_ARM_HAL_FAULT;
        return 0;
    }
    left_follower.enabled = 1U;
    left_follower.mode = left_follower.need_rejoin ? DUAL_ARM_REJOINING : DUAL_ARM_TRACKING;
    return 1;
#else
    return servo_pwm_driver_enable();
#endif
}

int servo_hal_disable(void)
{
    int right_ok = servo_pwm_driver_disable();
#if ROBOT_SPLIT_BOARD_CONTROL
    int left_ok = servo_pwm_driver_disable_bank(SERVO_PWM_BANK_LEFT);
    selected_output.enabled = 0U;
    selected_output.mode = right_ok && left_ok ? DUAL_ARM_OFF : DUAL_ARM_HAL_FAULT;
    if (!right_ok || !left_ok) selected_output.have_command = 0U;
    return right_ok && left_ok;
#elif ROBOT_DUAL_ARM_ENABLE
    int left_ok = servo_pwm_driver_disable_bank(SERVO_PWM_BANK_LEFT);
    if (left_ok) dual_arm_follower_pause(&left_follower);
    else left_follower.mode = DUAL_ARM_HAL_FAULT;
    return right_ok && left_ok;
#else
    return right_ok;
#endif
}

const DualArmFollower *servo_hal_dual_status(void)
{
#if ROBOT_DUAL_ARM_ENABLE
    return &left_follower;
#elif ROBOT_SPLIT_BOARD_CONTROL
    return &selected_output;
#else
    return NULL;
#endif
}

int servo_hal_faulted(void)
{
    const DualArmFollower *status = servo_hal_dual_status();
    return status != NULL && status->mode == DUAL_ARM_HAL_FAULT;
}

int servo_hal_apply(const ServoPwmCommand *cmd)
{
    unsigned i;
    if (cmd == NULL) return 0;
#if ROBOT_SPLIT_BOARD_CONTROL
    if (servo_hal_faulted()) return 0;
#endif
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
#if ROBOT_SPLIT_BOARD_CONTROL
        if (!servo_pwm_driver_write_channel_bank(SELECTED_PWM_BANK,
                driver_channels[i], values[i])) return selected_output_failure();
#else
        if (!servo_pwm_driver_write_channel(driver_channels[i], values[i])) return 0;
#endif
    }
#if ROBOT_SPLIT_BOARD_CONTROL
    if (!servo_pwm_driver_update_bank(SELECTED_PWM_BANK)) return selected_output_failure();
    selected_output.have_command = 1U;
    return 1;
#else
    return servo_pwm_driver_update();
#endif
}

int servo_hal_apply_joint_command(const ForearmJointCommand *command,
                                 const ServoPwmCommand *pwm, int control_tick)
{
#if ROBOT_SPLIT_BOARD_CONTROL
    ServoPwmCommand expected;
    (void)control_tick;
    if (command == NULL || pwm == NULL || !forearm_safety_check_apply(command, NULL) ||
        !servo_control_convert(command, &expected) || memcmp(&expected, pwm, sizeof(expected)) != 0)
        return 0;
    if (!servo_hal_apply(pwm)) return 0;
    selected_output.applied = *command;
    return 1;
#elif ROBOT_DUAL_ARM_ENABLE
    DualArmFollower next = left_follower;
    ForearmJointCommand left_command;
    ServoPwmCommand left_pwm, expected;
    unsigned channel;
    uint16_t right_values[SERVO_COUNT], left_values[SERVO_COUNT];
    if (command == NULL || pwm == NULL || left_follower.mode == DUAL_ARM_HAL_FAULT ||
        !servo_control_convert(command, &expected) || memcmp(&expected, pwm, sizeof(expected)) != 0 ||
        !dual_arm_follower_prepare(&next, command, control_tick, &left_command) ||
        !servo_control_convert(&left_command, &left_pwm)) return 0;
    right_values[0] = pwm->elbow_roll_pwm_us;
    right_values[1] = pwm->elbow_pitch_pwm_us;
    right_values[2] = pwm->wrist_pitch_pwm_us;
    right_values[3] = pwm->wrist_roll_pwm_us;
    right_values[4] = pwm->gripper_pwm_us;
    left_values[0] = left_pwm.elbow_roll_pwm_us;
    left_values[1] = left_pwm.elbow_pitch_pwm_us;
    left_values[2] = left_pwm.wrist_pitch_pwm_us;
    left_values[3] = left_pwm.wrist_roll_pwm_us;
    left_values[4] = left_pwm.gripper_pwm_us;
    for (channel = 0U; channel < SERVO_COUNT; ++channel) {
        const ServoConfig *config = servo_config_get((ServoChannel)channel);
        if (config == NULL || right_values[channel] < config->min_us ||
            right_values[channel] > config->max_us || left_values[channel] < config->min_us ||
            left_values[channel] > config->max_us) return 0;
    }
    for (channel = 0U; channel < SERVO_COUNT; ++channel) {
        if (!servo_pwm_driver_write_channel_bank(SERVO_PWM_BANK_RIGHT,
                driver_channels[channel], right_values[channel]) ||
            !servo_pwm_driver_write_channel_bank(SERVO_PWM_BANK_LEFT,
                driver_channels[channel], left_values[channel])) goto failure;
    }
    if (!servo_pwm_driver_update() || !servo_pwm_driver_update_bank(SERVO_PWM_BANK_LEFT)) goto failure;
    left_follower = next;
    return 1;
failure:
    (void)servo_hal_disable();
    left_follower.mode = DUAL_ARM_HAL_FAULT;
    return 0;
#else
    (void)command;
    (void)control_tick;
    return servo_hal_apply(pwm);
#endif
}

int servo_hal_startup(void)
{
    uint16_t startup[SERVO_COUNT];
    unsigned i;
#if ROBOT_SPLIT_BOARD_CONTROL
    if (!servo_hal_disable()) return 0;
#endif
    for (i = 0; i < SERVO_COUNT; ++i) {
        const ServoConfig *config = servo_config_get((ServoChannel)i);
        if (config == NULL) return 0;
        startup[i] = config->startup_us;
    }
    const ServoPwmCommand cmd = {
        startup[0], startup[1], startup[2], startup[3], startup[4]
    };
    if (!servo_hal_apply(&cmd)) return 0;
#if ROBOT_SPLIT_BOARD_CONTROL
    return 1;
#else
    return servo_hal_enable();
#endif
}
