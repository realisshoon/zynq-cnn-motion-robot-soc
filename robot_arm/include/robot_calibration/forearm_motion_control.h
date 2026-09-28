#ifndef ROBOT_CALIBRATION_FOREARM_MOTION_CONTROL_H
#define ROBOT_CALIBRATION_FOREARM_MOTION_CONTROL_H

#include <stdint.h>
#include "common/robot_types.h"
#include "human_target_angle/forearm_mapping.h"

/* Five-axis output uses ForearmJointCommand from common/robot_types.h. */

/*
 * target->valid==0이거나 elbow_roll/elbow_pitch/wrist_pitch/wrist_roll/
 * gripper_norm 중 하나라도 비유한이면 무효. elbow_pitch_deg는 A1 계약상
 * [-90,90]을 벗어나지 않아야 하므로(wrap 없음) 범위도 확인한다(약간의
 * 여유를 둔다). elbow_roll/wrist_pitch/wrist_roll은 주기적(wrap)이라
 * 범위를 제한하지 않는다.
 * elbow_roll_observable/hand_fresh/wrist_valid는 유효성 판정에 넣지 않는다 — 이들은
 * 관측 상태를 뜻한다. wrist_valid=0이면 통합 경로에서 로봇 손목과
 * 그리퍼를 현재 위치에 유지하고 팔꿈치만 적용한다
 * (docs/agent1_forearm.md 참고). 2026-09-22: Agent1이 TableFrame(외부
 * up/forward 보정) 요구를 없애고 기존 6축과 같은 BodyFrame(어깨 기반)으로
 * 바꿔서, calibrated 필드 자체가 더 이상 없다.
 */
int forearm_motion_control_validate_target(const HumanForearmTarget *target);

/* elbow_roll_deg -> elbow_roll_deg, elbow_pitch_deg -> elbow_pitch_deg,
 * wrist_pitch_deg/wrist_roll_deg는 그대로 이름 대응. gripper_norm은 보정 없이
 * 전달. 서보 calibration(zero/direction/scale)만 적용하고 clamp는 하지 않는다. */
void forearm_motion_control_map_target(const HumanForearmTarget *input, ForearmJointCommand *output);
void forearm_motion_control_apply_limits(ForearmJointCommand *command);

/*
 * elbow_roll_deg/wrist_pitch_deg/wrist_roll_deg는 A1에서 [-180,180)으로
 * wrap되어 출력된다(예: 179도->-179도는 실제로 -358도가 아니라 +2도 움직인
 * 것). elbow_pitch_deg([-90,90])는 wrap 대상이 아니다(docs/agent1_forearm.md,
 * "forearm_pitch에 circular unwrap을 적용하지 마라").
 *
 * forearm_motion_control_unwrap_target()은 과거 API 이름을 유지하지만,
 * 유한 가동범위의 위치 서보에 맞게 세 각도 모두 [-180,180]의 대표각으로
 * 정규화한다. 프레임 간 누적 unwrap은 하지 않는다. 호출자는
 * forearm_motion_control_validate_target()으로 이미 유효성을 확인한 target만
 * 넘겨야 한다. forearm_calibration_apply()보다 먼저, 매 HumanForearmTarget
 * 수신 시 1회 호출한다. state에는 마지막 대표각을 진단용으로 보관한다.
 */
typedef struct {
    float yaw_deg;
    float wrist_pitch_deg;
    float wrist_roll_deg;
    int has_reference;
    int wrist_has_reference;
} ForearmAngleUnwrapState;

void forearm_motion_control_unwrap_state_init(ForearmAngleUnwrapState *state);
void forearm_motion_control_unwrap_target(ForearmAngleUnwrapState *state, HumanForearmTarget *target);

/* 각 축의 대표각을 현재 보정식으로 매핑했을 때 서보 가동범위 안인지 확인한다.
 * 범위 밖이면 clamp로 양 끝까지 보내는 대신 통합 경로에서 직전 목표를 유지한다. */
int forearm_motion_control_elbow_roll_reachable(float human_deg);
int forearm_motion_control_wrist_pitch_reachable(float human_deg);
int forearm_motion_control_wrist_roll_reachable(float human_deg);

#endif /* ROBOT_CALIBRATION_FOREARM_MOTION_CONTROL_H */
