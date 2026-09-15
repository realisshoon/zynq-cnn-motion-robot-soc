#include "output_controller/servo_control.h"
#include "output_controller/servo_config.h"

#include <stddef.h>


/*
 * PWM Hardware Safe Range Clamp
 *
 * 계산된 PWM 값이 ServoConfig에 정의된
 * PWM 범위를 벗어나지 않도록 제한한다.
 *
 * 이것은 Robot Joint Angle Limit이 아니라
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
 * Angle Reference Range Clamp
 *
 * Servo PWM 변환식에 사용할 수 있는
 * 각도 범위를 벗어난 값이 들어왔을 때
 * 계산 오류를 방지하기 위한 Hardware Conversion 보호이다.
 *
 * Robot의 실제 안전 Joint Limit은 Agent2가 담당한다.
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
 * Servo Angle(deg) -> PWM Pulse Width(us)
 *
 * ServoConfig의
 *
 * min_deg    <-> min_us
 * center_deg <-> center_us
 * max_deg    <-> max_us
 *
 * 기준을 이용하여 선형 변환한다.
 */
static uint16_t angle_to_pwm_us(
    float angle_deg,
    const ServoConfig *config
)
{
    float pwm_us;

    /*
     * Conversion 범위 보호
     */
    angle_deg =
        clamp_servo_angle(
            angle_deg,
            config
        );


    /*
     * min_deg ~ center_deg 구간
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
     * center_deg ~ max_deg 구간
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


    /*
     * float 결과를 uint16_t PWM 값으로 변환 후
     * Hardware Safe Range 적용
     */
    return clamp_pwm_us(
        (uint16_t)pwm_us,
        config
    );
}


/*
 * Gripper Normalized Command -> PWM
 *
 * gripper_norm:
 *
 * 0.0 = Close
 * 1.0 = Open
 *
 * 범위를 벗어난 값은 0.0 ~ 1.0으로 제한한다.
 */
static uint16_t gripper_to_pwm_us(
    float gripper_norm,
    const ServoConfig *config
)
{
    float pwm_us;


    /*
     * Normalized Range Clamp
     */
    if (gripper_norm < 0.0f) {
        gripper_norm = 0.0f;
    }

    if (gripper_norm > 1.0f) {
        gripper_norm = 1.0f;
    }


    /*
     * 0.0 ~ 1.0
     *
     *      ↓
     *
     * min_us ~ max_us
     */
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
 * JointCommand -> ServoPwmCommand
 *
 * output_controller의 핵심 변환 함수.
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
     * NULL Pointer 확인
     */
    if (joint_cmd == NULL ||
        pwm_cmd == NULL) {

        return 0;
    }


    /*
     * Agent2에서 전달된 JointCommand가
     * 유효하지 않으면 새로운 PWM 값을 생성하지 않는다.
     */
    if (!joint_cmd->valid) {
        return 0;
    }


    /*
     * 각 Servo Hardware Configuration 가져오기
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
     * Config 조회 실패 확인
     */
    if (base_config == NULL ||
        shoulder_config == NULL ||
        elbow_config == NULL ||
        wrist_pitch_config == NULL ||
        wrist_roll_config == NULL ||
        gripper_config == NULL) {

        return 0;
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
     *
     * Gripper는 degree가 아니라
     * 0.0 ~ 1.0 Normalized 값이다.
     */
    pwm_cmd->gripper_pwm_us =
        gripper_to_pwm_us(
            joint_cmd->gripper_norm,
            gripper_config
        );


    return 1;
}