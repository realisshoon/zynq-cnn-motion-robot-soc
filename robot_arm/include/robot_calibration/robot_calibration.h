#ifndef ROBOT_CALIBRATION_H
#define ROBOT_CALIBRATION_H

#include "common/robot_types.h"

/*
 * Agent 2 공개 API.
 *
 * Agent 1의 HumanJointTarget을 받아 보정 및 안전검사를 마친 JointCommand로
 * 변환한다.
 */
int robot_calibration_apply(
    const HumanJointTarget *input,
    JointCommand *output
);

/* base, shoulder, elbow, wrist_pitch, wrist_roll (5개).
 * gripper는 agent1->agent2->agent3 구간에서 값이 바뀌면 안 되는 단순
 * 전달값이라 램프/동기화 대상에서 제외하고 RobotMotionState.gripper
 * 필드로 별도 보관한다. motion_limits.h는 이제 관절 개수를 모르는 순수
 * 스칼라 유틸이라, 이 매크로가 저장소 전체에서 유일한 관절 개수 상수다. */
#define ROBOT_MOTION_JOINT_COUNT 5

/*
 * 틱 단위 램프 상태: 관절 5개에 대해 속도 제한 + 다관절 동기화 + 스무딩을
 * 적용하면서, 로봇팔의 현재 위치에서 최신 목표까지 이동시킨다. 그리퍼는
 * 이 램프 대상이 아니라 gripper 필드에 마지막 목표값을 그대로 저장해뒀다가
 * robot_calibration_step()에서 변형 없이 출력한다.
 * 처음 쓰기 전에 robot_calibration_state_init()으로 0으로 초기화한다.
 *
 * 사용법:
 *   - robot_calibration_apply()가 새 JointCommand를 만들 때마다(Agent 1이
 *     새 HumanJointTarget을 보낼 때마다, 가변 주기) robot_calibration_set_target() 호출.
 *   - 고정 제어 틱마다(예: 서보 PWM 50Hz에 맞춘 ~20ms) robot_calibration_step()을
 *     호출해서 이번 틱에 output_controller로 보낼 JointCommand를 얻는다.
 *
 * 관절 배열 순서: base, shoulder, elbow, wrist_pitch, wrist_roll --
 * JointCommand 필드 순서와 동일(gripper 제외). motion_limits.h는 배열이 아닌
 * 스칼라 함수만 제공하므로 이 순서를 몰라도 된다.
 * 이 레이어의 설계 근거는 docs/agent2_design_log.md 참고.
 */
typedef struct {
    float current[ROBOT_MOTION_JOINT_COUNT];
    float start[ROBOT_MOTION_JOINT_COUNT];
    float target[ROBOT_MOTION_JOINT_COUNT];
    float max_delta_per_tick[ROBOT_MOTION_JOINT_COUNT];
    float gripper;
    int stretched_ticks;
    int ticks_elapsed;
    int has_target;
} RobotMotionState;

void robot_calibration_state_init(RobotMotionState *state);

/*
 * 로봇팔의 현재 명령 위치에서 `target`까지 가는 램프를 다시 계획한다.
 * 이전 램프가 끝나기 전에 다시 호출해도 안전하다(예: 동작 도중 새
 * HumanJointTarget이 도착한 경우) -- 새 램프는 이전 목표가 아니라 로봇팔이
 * 실제로 지금 있는 위치에서 시작한다. 최초 호출(아직 위치를 모르는 상태)에는
 * 램프 없이 곧바로 `target`으로 스냅한다. `target`은 valid == 1이어야 하며,
 * 호출자는 robot_calibration_apply()가 위험 판정(0 반환)한 결과는 버리고
 * 마지막으로 승인된 target으로 계속 robot_calibration_step()을 호출해야 한다.
 */
void robot_calibration_set_target(RobotMotionState *state, const JointCommand *target);

/*
 * 램프를 한 틱 진행시키고 결과를 *output에 쓴다. set_target()이 얼마나
 * 자주 호출되는지와 무관하게 고정 주기로 호출해도 안전하다.
 */
void robot_calibration_step(RobotMotionState *state, JointCommand *output);

#endif /* ROBOT_CALIBRATION_H */
