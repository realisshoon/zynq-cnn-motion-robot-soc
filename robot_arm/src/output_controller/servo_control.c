#include "output_controller/servo_control.h"
#include "output_controller/servo_config.h"

#include <stddef.h>
#include <math.h>


/*
 * ============================================================
 * PWM Hardware Safe Range Clamp
 * ============================================================
 *
 * 계산된 PWM 값이 ServoConfig에 정의된
 * PWM 범위를 벗어나지 않도록 제한한다.
 *
 * 이것은 Robot Joint Safety Limit이 아니라
 * Servo Hardware 보호용 제한이다.
 */
static uint16_t clamp_pwm_us(
    uint16_t pwm_us,
    const ServoConfig *config
)
{
    if (pwm_us < config->min_us) {
        return config->min_us;
    }

    if (pwm_us > config->max_us) {
        return config->max_us;
    }

    return pwm_us;
}


/*
 * ============================================================
 * Servo Angle Range Clamp
 * ============================================================
 *
 * PWM 변환에서 사용할 수 있는 각도 범위를
 * ServoConfig 기준으로 제한한다.
 *
 * Robot 자체의 Joint Limit / 충돌 방지 등은
 * 상위 제어부에서 처리한다.
 */
static float clamp_servo_angle(
    float angle_deg,
    const ServoConfig *config
)
{
    if (angle_deg < config->min_deg) {
        return config->min_deg;
    }

    if (angle_deg > config->max_deg) {
        return config->max_deg;
    }

    return angle_deg;
}


/*
 * ============================================================
 * Servo Angle(deg) -> PWM Pulse Width(us)
 * ============================================================
 */
static uint16_t angle_to_pwm_us(
    float angle_deg,
    const ServoConfig *config
)
{
    float pwm_us;

    angle_deg =
        clamp_servo_angle(
            angle_deg,
            config
        );


    /*
     * min_deg ~ center_deg
     */
    if (angle_deg <= config->center_deg) {

        pwm_us =
            (float)config->min_us
            +
            (
                (angle_deg - config->min_deg)
                /
                (config->center_deg - config->min_deg)
            )
            *
            (
                (float)config->center_us
                -
                (float)config->min_us
            );
    }

    /*
     * center_deg ~ max_deg
     */
    else {

        pwm_us =
            (float)config->center_us
            +
            (
                (angle_deg - config->center_deg)
                /
                (config->max_deg - config->center_deg)
            )
            *
            (
                (float)config->max_us
                -
                (float)config->center_us
            );
    }


    return clamp_pwm_us(
        (uint16_t)pwm_us,
        config
    );
}


/*
 * ============================================================
 * Gripper Normalized Command -> PWM
 * ============================================================
 *
 * gripper_norm:
 *
 * 0.0 = Close
 * 1.0 = Open
 */
static uint16_t gripper_to_pwm_us(
    float gripper_norm,
    const ServoConfig *config
)
{
    float pwm_us;


    if (gripper_norm < 0.0f) {
        gripper_norm = 0.0f;
    }

    if (gripper_norm > 1.0f) {
        gripper_norm = 1.0f;
    }


    pwm_us =
        (float)config->min_us
        +
        gripper_norm
        *
        (
            (float)config->max_us
            -
            (float)config->min_us
        );


    return clamp_pwm_us(
        (uint16_t)pwm_us,
        config
    );
}


/*
 * ============================================================
 * JointCommand -> ServoPwmCommand
 * ============================================================
 */
uint8_t servo_control_convert_channel(ServoChannel channel, float value,
                                      uint16_t *pwm_us)
{
    const ServoConfig *config = servo_config_get(channel);
    if (pwm_us == NULL || config == NULL || !isfinite(value)) return 0U;
    *pwm_us = channel == SERVO_GRIPPER
        ? gripper_to_pwm_us(value, config) : angle_to_pwm_us(value, config);
    return 1U;
}

uint8_t servo_control_convert(const JointCommand *joint_cmd,
                              ServoPwmCommand *pwm_cmd)
{
    /* BLOCKED BY AGENT2 INTERFACE. Never reinterpret legacy robot angles. */
    (void)joint_cmd;
    (void)pwm_cmd;
    return 0U;
}
