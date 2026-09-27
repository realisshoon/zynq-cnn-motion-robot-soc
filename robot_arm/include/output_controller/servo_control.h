#ifndef OUTPUT_CONTROLLER_SERVO_CONTROL_H
#define OUTPUT_CONTROLLER_SERVO_CONTROL_H
#include <stdint.h>
#include "common/robot_types.h"
#include "output_controller/servo_config.h"

typedef struct {
    uint16_t elbow_roll_pwm_us;
    uint16_t elbow_pitch_pwm_us;
    uint16_t wrist_pitch_pwm_us;
    uint16_t wrist_roll_pwm_us;
    uint16_t gripper_pwm_us;
} ServoPwmCommand;

/* Hardware conversion primitive, independent of Agent2's pending contract.
 * Input: servo degrees for joints, normalized 0..1 for gripper.
 * Rejects non-finite values, invalid channel and NULL; leaves output untouched.
 * Finite values are clamped to the retained hardware calibration range. */
uint8_t servo_control_convert_channel(ServoChannel channel, float value,
                                      uint16_t *pwm_us);

/* Convert Agent2's final five-axis robot command to PWM.
 * Returns 1 only after all five conversions succeed.
 * Returns 0 on NULL, invalid command or conversion failure; output unchanged.
 * ForearmJointCommand is owned by common/robot_types.h (Agent2). */
uint8_t servo_control_convert(const ForearmJointCommand *joint_cmd,
                              ServoPwmCommand *pwm_cmd);
#endif
