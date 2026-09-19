#include "output_controller/servo_hal.h"

#include "output_controller/servo_config.h"
#include "drivers/servo_pwm_driver.h"

#include <stddef.h>


/*
 * ============================================================
 * PWM Range Check
 * ============================================================
 */
static int servo_hal_pwm_valid(
    ServoChannel channel,
    uint16_t pwm_us
)
{
    const ServoConfig *config;


    config =
        servo_config_get(channel);


    if (config == NULL) {
        return 0;
    }


    /*
     * 현재 기준:
     *
     * 500 us ~ 2500 us
     *
     * 추후 Servo별 Calibration 값이 바뀌면
     * ServoConfig의 min_us / max_us를 자동으로 따라간다.
     */
    if (pwm_us < config->min_us) {
        return 0;
    }


    if (pwm_us > config->max_us) {
        return 0;
    }


    return 1;
}


/*
 * ============================================================
 * ServoPwmCommand 전체 검사
 * ============================================================
 */
static int servo_hal_command_valid(
    const ServoPwmCommand *cmd
)
{
    if (cmd == NULL) {
        return 0;
    }


    /*
     * 중요한 점:
     *
     * Register Write를 시작하기 전에
     * 6개 값을 전부 검사한다.
     *
     * 따라서 잘못된 값 때문에
     * 일부 Shadow Register만 변경되는 상황을 방지한다.
     */

    if (!servo_hal_pwm_valid(
            SERVO_BASE,
            cmd->base_pwm_us)) {

        return 0;
    }


    if (!servo_hal_pwm_valid(
            SERVO_SHOULDER,
            cmd->shoulder_pwm_us)) {

        return 0;
    }


    if (!servo_hal_pwm_valid(
            SERVO_ELBOW,
            cmd->elbow_pwm_us)) {

        return 0;
    }


    if (!servo_hal_pwm_valid(
            SERVO_WRIST_PITCH,
            cmd->wrist_pitch_pwm_us)) {

        return 0;
    }


    if (!servo_hal_pwm_valid(
            SERVO_WRIST_ROLL,
            cmd->wrist_roll_pwm_us)) {

        return 0;
    }


    if (!servo_hal_pwm_valid(
            SERVO_GRIPPER,
            cmd->gripper_pwm_us)) {

        return 0;
    }


    return 1;
}


/*
 * ============================================================
 * Initialization
 * ============================================================
 */
void servo_hal_init(void)
{
    servo_pwm_driver_init();
}


/*
 * ============================================================
 * Enable
 * ============================================================
 */
int servo_hal_enable(void)
{
    return servo_pwm_driver_enable();
}


/*
 * ============================================================
 * Disable
 * ============================================================
 */
int servo_hal_disable(void)
{
    return servo_pwm_driver_disable();
}


/*
 * ============================================================
 * Servo PWM Apply
 * ============================================================
 */
int servo_hal_apply(
    const ServoPwmCommand *cmd
)
{
    /*
     * --------------------------------------------------------
     * 1. 6개 값 전체 검사
     * --------------------------------------------------------
     */
    if (!servo_hal_command_valid(cmd)) {
        return 0;
    }


    /*
     * --------------------------------------------------------
     * 2. Shadow Register Write
     * --------------------------------------------------------
     */

    if (!servo_pwm_driver_write_channel(
            SERVO_PWM_DRIVER_BASE,
            cmd->base_pwm_us)) {

        return 0;
    }


    if (!servo_pwm_driver_write_channel(
            SERVO_PWM_DRIVER_SHOULDER,
            cmd->shoulder_pwm_us)) {

        return 0;
    }


    if (!servo_pwm_driver_write_channel(
            SERVO_PWM_DRIVER_ELBOW,
            cmd->elbow_pwm_us)) {

        return 0;
    }


    if (!servo_pwm_driver_write_channel(
            SERVO_PWM_DRIVER_WRIST_PITCH,
            cmd->wrist_pitch_pwm_us)) {

        return 0;
    }


    if (!servo_pwm_driver_write_channel(
            SERVO_PWM_DRIVER_WRIST_ROLL,
            cmd->wrist_roll_pwm_us)) {

        return 0;
    }


    if (!servo_pwm_driver_write_channel(
            SERVO_PWM_DRIVER_GRIPPER,
            cmd->gripper_pwm_us)) {

        return 0;
    }


    /*
     * --------------------------------------------------------
     * 3. UPDATE
     * --------------------------------------------------------
     *
     * Shadow 값 6개를 모두 쓴 뒤
     * 마지막에 한 번만 UPDATE 한다.
     */
    if (!servo_pwm_driver_update()) {
        return 0;
    }


    return 1;
}


/*
 * ============================================================
 * Startup Safe Pose
 * ============================================================
 *
 * 현재 ServoConfig:
 *
 * center_deg = 90 deg
 * center_us  = 1500 us
 *
 * 따라서 현재는 6축 모두 1500 us가 된다.
 *
 * 나중에 Servo별 Calibration이 바뀌어도
 * center_us 값을 따라가므로
 * 이 함수 자체를 다시 수정할 필요가 없다.
 */
int servo_hal_startup(void)
{
    ServoPwmCommand startup_cmd;

    const ServoConfig *base_config;
    const ServoConfig *shoulder_config;
    const ServoConfig *elbow_config;
    const ServoConfig *wrist_pitch_config;
    const ServoConfig *wrist_roll_config;
    const ServoConfig *gripper_config;


    /*
     * 각 Servo Config 조회.
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


    if ((base_config == NULL) ||
        (shoulder_config == NULL) ||
        (elbow_config == NULL) ||
        (wrist_pitch_config == NULL) ||
        (wrist_roll_config == NULL) ||
        (gripper_config == NULL)) {

        return 0;
    }


    /*
     * ========================================================
     * Startup Safe Position
     * ========================================================
     *
     * 현재는 모두:
     *
     * 90 deg
     * = 1500 us
     */
    startup_cmd.base_pwm_us =
        base_config->center_us;

    startup_cmd.shoulder_pwm_us =
        shoulder_config->center_us;

    startup_cmd.elbow_pwm_us =
        elbow_config->center_us;

    startup_cmd.wrist_pitch_pwm_us =
        wrist_pitch_config->center_us;

    startup_cmd.wrist_roll_pwm_us =
        wrist_roll_config->center_us;

    startup_cmd.gripper_pwm_us =
        gripper_config->center_us;


    /*
     * ========================================================
     * Startup Sequence
     * ========================================================
     *
     * 1. Shadow x6
     * 2. UPDATE
     *
     * 아직 ENABLE은 하지 않는다.
     */
    if (!servo_hal_apply(
            &startup_cmd)) {

        return 0;
    }


    /*
     * 안전한 PWM 값이 Active 쪽에 들어간 이후
     * PWM 출력을 Enable.
     */
    if (!servo_hal_enable()) {
        return 0;
    }


    return 1;
}