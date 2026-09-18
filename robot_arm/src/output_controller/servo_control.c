#include "output_controller/servo_control.h"
#include "output_controller/servo_config.h"

#include <stddef.h>


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
uint8_t servo_control_convert(
    const JointCommand *joint_cmd,
    ServoPwmCommand *pwm_cmd
)
{
    const ServoConfig *base_config;
    const ServoConfig *shoulder_config;
    const ServoConfig *elbow_config;
    const ServoConfig *wrist_pitch_config;
    const ServoConfig *wrist_roll_config;
    const ServoConfig *gripper_config;


    /*
     * Pointer Check
     */
    if ((joint_cmd == NULL) ||
        (pwm_cmd == NULL)) {

        return 0U;
    }


    /*
     * Invalid JointCommand 사용 금지
     */
    if (!joint_cmd->valid) {
        return 0U;
    }


    /*
     * Servo Hardware Configuration
     */
    base_config =
        servo_config_get(SERVO_BASE);

    shoulder_config =
        servo_config_get(SERVO_SHOULDER);

    elbow_config =
        servo_config_get(SERVO_ELBOW);

    wrist_pitch_config =
        servo_config_get(SERVO_WRIST_PITCH);

    wrist_roll_config =
        servo_config_get(SERVO_WRIST_ROLL);

    gripper_config =
        servo_config_get(SERVO_GRIPPER);


    /*
     * Config Check
     */
    if ((base_config == NULL) ||
        (shoulder_config == NULL) ||
        (elbow_config == NULL) ||
        (wrist_pitch_config == NULL) ||
        (wrist_roll_config == NULL) ||
        (gripper_config == NULL)) {

        return 0U;
    }


    /*
     * Base
     */
    pwm_cmd->base_pwm_us =
        angle_to_pwm_us(
            joint_cmd->base_deg,
            base_config
        );


    /*
     * Shoulder
     */
    pwm_cmd->shoulder_pwm_us =
        angle_to_pwm_us(
            joint_cmd->shoulder_deg,
            shoulder_config
        );


    /*
     * Elbow
     */
    pwm_cmd->elbow_pwm_us =
        angle_to_pwm_us(
            joint_cmd->elbow_deg,
            elbow_config
        );


    /*
     * Wrist Pitch
     */
    pwm_cmd->wrist_pitch_pwm_us =
        angle_to_pwm_us(
            joint_cmd->wrist_pitch_deg,
            wrist_pitch_config
        );


    /*
     * Wrist Roll
     */
    pwm_cmd->wrist_roll_pwm_us =
        angle_to_pwm_us(
            joint_cmd->wrist_roll_deg,
            wrist_roll_config
        );


    /*
     * Gripper
     */
    pwm_cmd->gripper_pwm_us =
        gripper_to_pwm_us(
            joint_cmd->gripper_norm,
            gripper_config
        );


    return 1U;
}