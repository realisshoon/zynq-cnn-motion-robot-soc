#ifndef ROBOT_CALIBRATION_FOREARM_MOTION_CONTROL_H
#define ROBOT_CALIBRATION_FOREARM_MOTION_CONTROL_H

#include <stdint.h>
#include "common/robot_types.h"
#include "human_target_angle/forearm_mapping.h"

/*
 * Agent2 새 5축(팔꿈치부터 시작) 출력. ForearmJointCommand는
 * include/common/robot_types.h에 정의된다(2026-09-22, 사용자 지시 —
 * Agent3 실물 통합을 위해 공용 헤더로 옮김). 그 파일의 legacy JointCommand
 * (구 6축, base/shoulder/elbow/wrist_pitch/wrist_roll/gripper)와는 별도
 * 타입이다 — legacy 경로가 계속 쓰므로 필드를 재해석하거나 덮어쓰지 않는다.
 * 서보 명령(도 단위, 보정 이후 값)이며 사람 관절 의미가 아니다.
 */

/*
 * target->valid==0이거나 forearm_yaw/pitch/wrist_pitch/wrist_roll/gripper_norm
 * 중 하나라도 비유한이면 무효. forearm_pitch_deg는 A1 계약상 [-90,90]을
 * 벗어나지 않아야 하므로(wrap 없음) 범위도 확인한다(약간의 여유를 둔다).
 * yaw/wrist_pitch/wrist_roll은 주기적(wrap)이라 범위를 제한하지 않는다.
 * yaw_observable/hand_fresh/calibrated는 유효성 판정에 넣지 않는다 — 이들은
 * "관측됐는가"를 뜻하지 "이번 target을 써도 되는가"를 뜻하지 않는다
 * (docs/agent1_forearm.md 참고). calibrated==0인 TableFrame으로 만들어진
 * target을 실제 서보로 내보낼지는 통합 계층이 별도로 판단해야 한다.
 */
int forearm_motion_control_validate_target(const HumanForearmTarget *target);

/* forearm_yaw_deg -> elbow_roll_deg, forearm_pitch_deg -> elbow_pitch_deg,
 * wrist_pitch_deg/wrist_roll_deg는 그대로 이름 대응. gripper_norm은 보정 없이
 * 전달. 서보 calibration(zero/direction/scale)만 적용하고 clamp는 하지 않는다. */
void forearm_motion_control_map_target(const HumanForearmTarget *input, ForearmJointCommand *output);
void forearm_motion_control_apply_limits(ForearmJointCommand *command);

/*
 * forearm_yaw_deg/wrist_pitch_deg/wrist_roll_deg는 A1에서 [-180,180)으로
 * wrap되어 출력된다(예: 179도->-179도는 실제로 -358도가 아니라 +2도 움직인
 * 것). forearm_pitch_deg([-90,90])는 wrap 대상이 아니다(docs/agent1_forearm.md,
 * "forearm_pitch에 circular unwrap을 적용하지 마라").
 *
 * forearm_motion_control_unwrap_target()은 이전 프레임 기준 최단 회전 방향으로
 * 위 세 각도를 풀어서 target을 in-place로 고쳐 쓴다. 호출자는
 * forearm_motion_control_validate_target()으로 이미 유효성을 확인한 target만
 * 넘겨야 한다. forearm_calibration_apply()보다 먼저, 매 HumanForearmTarget
 * 수신 시 1회 호출한다. legacy motion_control.c의 HumanAngleUnwrapState와
 * 같은 목적이며 구현도 같은 패턴이다(파일을 공유하지 않는 이유는
 * docs/agent2_design_log.md 참고 — legacy 경로를 건드리지 않기 위해서다).
 */
typedef struct {
    float yaw_deg;
    float wrist_pitch_deg;
    float wrist_roll_deg;
    int has_reference;
} ForearmAngleUnwrapState;

void forearm_motion_control_unwrap_state_init(ForearmAngleUnwrapState *state);
void forearm_motion_control_unwrap_target(ForearmAngleUnwrapState *state, HumanForearmTarget *target);

#endif /* ROBOT_CALIBRATION_FOREARM_MOTION_CONTROL_H */
