#include <stddef.h>
#include <string.h>
#include <math.h>

#include "common/robot_types.h"
#include "robot_calibration/robot_calibration.h"
#include "robot_calibration/robot_calibration_config.h"
#include "robot_calibration/motion_control.h"

#define DEG_TO_RAD 0.01745329251994329577f
#define RAD_TO_DEG 57.29577951308232088f
#define DIRECTION_EPSILON 0.000001f

// 이 파일 안에서만 쓰는, 즉 application에서 사용하지 않는 함수는 static이며 header에 포함하지 않는다.
// 결국 함수간, 모듈간 인터페이스가 된다.
// 각 raw data에 configuration data를 곱한다.
static float map_joint_angle(float human_deg, const JointCalibration *config)
{
    return human_deg
         * config->scale
         * (float)config->direction
         + config->zero_offset_deg;
}
// clamping, 최대/최소값을 제한한다.
static float clamp_value(float value, float min_value, float max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

// -180~180 범위로 wrap된 각도를 다시 -180~180 범위로 접어넣는다.
static float wrap_to_180(float deg)
{
    deg = fmodf(deg, 360.0f);
    if (deg > 180.0f) deg -= 360.0f;
    if (deg < -180.0f) deg += 360.0f;
    return deg;
}

// last_unwrapped_deg 기준 최단 회전 방향으로 raw_deg를 풀어낸다.
// last_unwrapped_deg는 누적으로 -180~180 밖의 값을 가질 수 있으므로,
// 비교 직전에 wrap_to_180으로 접어서 raw_deg와 같은 표현 범위로 맞춘다.
static float unwrap_angle(float raw_deg, float last_unwrapped_deg, int has_reference)
{
    float last_raw_deg;
    float diff;

    if (!has_reference) return raw_deg;

    last_raw_deg = wrap_to_180(last_unwrapped_deg);
    diff = wrap_to_180(raw_deg - last_raw_deg);

    return last_unwrapped_deg + diff;
}
// configuration에 적용된 clamping을 적용한다.
static float clamp_joint_angle(float angle_deg, const JointCalibration *config)
{
    return clamp_value(angle_deg, config->min_deg, config->max_deg);
}

// 외부 함수는 header에 포함한다.
// 1st. 입력 유효 확인 함수
int motion_control_validate_target(const HumanJointTarget *target)
{
    // if invalid
    if(target == NULL) return 0;
    if(target->valid == 0) return 0;
    if(!isfinite(target->base_deg)) return 0;
    if(!isfinite(target->shoulder_deg)) return 0;
    if(!isfinite(target->elbow_deg)) return 0;
    if(!isfinite(target->wrist_pitch_deg)) return 0;
    if(!isfinite(target->wrist_roll_deg)) return 0;
    if(!isfinite(target->gripper_norm)) return 0;
    if(target->shoulder_deg < -90.0f || target->shoulder_deg > 90.0f) return 0;
    if(target->elbow_deg < 0.0f || target->elbow_deg > 180.0f) return 0;
    if(target->gripper_norm < 0.0f || target->gripper_norm > 1.0f) return 0;
    // else valid
    return 1;
}

// configuration에 따른 변환 수행
// gripper_norm은 agent1 -> agent2 -> agent3 구간에서 값이 바뀌면 안 되므로
// 보정 없이 그대로 복사만 한다.
void motion_control_map_target(const HumanJointTarget *input, JointCommand *output)
{
    float azimuth, elevation, horizontal;
    float right, up, forward;
    float flexion_deg, abduction_deg;

    if (input == NULL || output == NULL) return;

    /* A1 supplies spherical angles, not the two robot shoulder rotations.
     * Recover Body (+X right, +Y up, +Z reference-forward). Body +Z remains
     * A1's shoulder-derived reference; monocular reconstruction is not a
     * measurement of the person's actual torso forward direction.
     *
     * Robot joint order: base flexion, then local shoulder abduction.
     * u = (sin(a), -cos(a)*cos(b), cos(a)*sin(b)) in A1 Body coordinates.
     * Choose a in [-90,90]; b is immaterial at pure lateral elevation.
     * Canonicalize azimuth before trig: the integration unwrap state may
     * contain several turns, but a finite-range servo must not accumulate them.
     */
    azimuth = wrap_to_180(input->base_deg) * DEG_TO_RAD;
    elevation = input->shoulder_deg * DEG_TO_RAD;
    horizontal = cosf(elevation);
    if (fabsf(horizontal) < DIRECTION_EPSILON) horizontal = 0.0f;
    right = horizontal * sinf(azimuth);
    up = sinf(elevation);
    forward = horizontal * cosf(azimuth);
    abduction_deg = atan2f(right, hypotf(up, forward)) * RAD_TO_DEG;
    flexion_deg = hypotf(up, forward) < DIRECTION_EPSILON
        ? 0.0f : atan2f(forward, -up) * RAD_TO_DEG;

    output->base_deg = map_joint_angle(flexion_deg, &robot_calibration_config.base);
    output->shoulder_deg = map_joint_angle(abduction_deg, &robot_calibration_config.shoulder);
    /* A1 elbow is an interior angle: 180 straight -> servo 90.
     * The configured -1 scale direction and +270 offset give 270 - interior. */
    output->elbow_deg = map_joint_angle(input->elbow_deg, &robot_calibration_config.elbow);
    /* Wrist scales are zero for the initial three-joint test. Their physical
     * directions/zero references have not been calibrated. Gripper is separate. */
    output->wrist_pitch_deg = map_joint_angle(wrap_to_180(input->wrist_pitch_deg), &robot_calibration_config.wrist_pitch);
    output->wrist_roll_deg = map_joint_angle(wrap_to_180(input->wrist_roll_deg), &robot_calibration_config.wrist_roll);
    output->gripper_norm = input->gripper_norm;
    output->valid = input->valid;
}
// limit 적용 함수
// gripper_norm은 그대로 전달하는 값이라 clamp 대상에서 제외한다.
void motion_control_apply_limits(JointCommand *command)
{
    if (command == NULL) return;
    command->base_deg = clamp_joint_angle(command->base_deg, &robot_calibration_config.base);
    command->shoulder_deg = clamp_joint_angle(command->shoulder_deg, &robot_calibration_config.shoulder);
    command->elbow_deg = clamp_joint_angle(command->elbow_deg, &robot_calibration_config.elbow);
    command->wrist_pitch_deg = clamp_joint_angle(command->wrist_pitch_deg, &robot_calibration_config.wrist_pitch);
    command->wrist_roll_deg = clamp_joint_angle(command->wrist_roll_deg, &robot_calibration_config.wrist_roll);
}

void motion_control_unwrap_state_init(HumanAngleUnwrapState *state)
{
    if (state == NULL) return;
    memset(state, 0, sizeof(*state));
}

// base_deg/wrist_pitch_deg/wrist_roll_deg만 unwrap 대상이다.
// (shoulder_deg/elbow_deg는 wrap되지 않는 범위라 건드리지 않는다.)
void motion_control_unwrap_target(HumanAngleUnwrapState *state, HumanJointTarget *target)
{
    if (state == NULL || target == NULL) return;

    target->base_deg = unwrap_angle(target->base_deg, state->base_deg, state->has_reference);
    target->wrist_pitch_deg = unwrap_angle(target->wrist_pitch_deg, state->wrist_pitch_deg, state->has_reference);
    target->wrist_roll_deg = unwrap_angle(target->wrist_roll_deg, state->wrist_roll_deg, state->has_reference);

    state->base_deg = target->base_deg;
    state->wrist_pitch_deg = target->wrist_pitch_deg;
    state->wrist_roll_deg = target->wrist_roll_deg;
    state->has_reference = 1;
}
