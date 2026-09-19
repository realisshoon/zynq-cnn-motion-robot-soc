#ifndef ROBOT_CALIBRATION_MOTION_CONTROL_H
#define ROBOT_CALIBRATION_MOTION_CONTROL_H

#include "common/robot_types.h"

int motion_control_validate_target(const HumanJointTarget *target);
void motion_control_map_target(const HumanJointTarget *input, JointCommand *output);
void motion_control_apply_limits(JointCommand *command);

/*
 * base_deg/wrist_pitch_deg/wrist_roll_deg는 agent1에서 -180~180으로 wrap되어
 * 출력된다(예: 179도 -> -179도는 실제로는 -358도가 아니라 +2도 움직인 것).
 * shoulder_deg(대략 -90~90)와 elbow_deg(0~180)는 애초에 wrap되지 않으므로
 * unwrap 대상이 아니다.
 *
 * motion_control_unwrap_target()은 이전 프레임 기준 최단 회전 방향으로 위
 * 세 각도를 풀어서 target을 in-place로 고쳐 쓴다. 호출 순서 계약: 호출자는
 * motion_control_validate_target()으로 이미 유효성을 확인한 target만
 * 넘겨야 한다(유효하지 않은 값을 넘기면 NaN 등이 상태에 그대로 누적된다).
 * robot_calibration_apply()보다 먼저, 매 HumanJointTarget 수신 시 1회
 * 호출한다.
 */
typedef struct {
    float base_deg;
    float wrist_pitch_deg;
    float wrist_roll_deg;
    int has_reference;
} HumanAngleUnwrapState;

void motion_control_unwrap_state_init(HumanAngleUnwrapState *state);
void motion_control_unwrap_target(HumanAngleUnwrapState *state, HumanJointTarget *target);

#endif /* ROBOT_CALIBRATION_MOTION_CONTROL_H */
