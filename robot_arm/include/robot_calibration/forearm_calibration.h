#ifndef ROBOT_CALIBRATION_FOREARM_CALIBRATION_H
#define ROBOT_CALIBRATION_FOREARM_CALIBRATION_H

#include "robot_calibration/forearm_motion_control.h"
#include "robot_calibration/forearm_safety_check.h"

/*
 * Agent2 새 5축 공개 API. Agent1의 HumanForearmTarget을 받아 보정 및
 * 안전검사를 마친 ForearmJointCommand로 변환한다. 기존 robot_calibration.h의
 * legacy 6축 API(robot_calibration_apply 등)와 별개이며 서로 상태를
 * 공유하지 않는다.
 */
int forearm_calibration_apply(
    const HumanForearmTarget *input,
    ForearmJointCommand *output
);

/* elbow_roll, elbow_pitch, wrist_pitch, wrist_roll (4개). gripper는
 * legacy와 마찬가지로 값이 바뀌면 안 되는 단순 전달값이라 램프 대상에서
 * 제외하고 gripper 필드로 별도 보관한다. */
#define FOREARM_MOTION_JOINT_COUNT 4

/*
 * 틱 단위 램프 상태. legacy RobotMotionState와 같은 설계(속도 제한 + 다관절
 * 동기화 + 스무딩, 정지 후 출발은 smoothstep/이동 중 재목표는 선형 추종,
 * 가속도 제한 없음 — docs/agent2_design_log.md 참고)를 4관절로 재사용한다.
 * 처음 쓰기 전에 forearm_calibration_state_init()으로 0 초기화한다.
 *
 * 사용법: forearm_calibration_apply()가 새 ForearmJointCommand를 만들 때마다
 * forearm_calibration_set_target() 호출, 고정 제어 틱마다
 * forearm_calibration_step() 호출.
 *
 * 관절 배열 순서: elbow_roll, elbow_pitch, wrist_pitch, wrist_roll --
 * ForearmJointCommand 필드 순서와 동일(gripper 제외).
 */
typedef struct {
    float current[FOREARM_MOTION_JOINT_COUNT];
    float start[FOREARM_MOTION_JOINT_COUNT];
    float target[FOREARM_MOTION_JOINT_COUNT];
    float max_delta_per_tick[FOREARM_MOTION_JOINT_COUNT];
    float gripper;
    int stretched_ticks;
    int ticks_elapsed;
    int has_target;
    int linear_retarget;
    /* Unsafe intermediate command: hold last output and expose the cause.
     * A new target clears the flag. This checks sampled centerline poses,
     * not swept volume, mechanical thickness, or actual servo feedback. */
    ForearmSafetyCheckFlags blocked_flags;
} ForearmMotionState;

void forearm_calibration_state_init(ForearmMotionState *state);

/*
 * 로봇팔의 현재 명령 위치에서 target까지 가는 램프를 다시 계획한다. legacy
 * robot_calibration_set_target()과 동일한 계약: 이전 램프가 끝나기 전에
 * 다시 부를 수 있고, 이동 중 재목표는 선형 추종(속도 연속성/가속도
 * 제한은 보장하지 않는다), 최초 호출은 곧바로 target으로 스냅한다.
 * target은 forearm_calibration_apply() 등으로 안전검사한 valid==1 명령이어야 한다.
 * 최초 target은 실측/승인한 시작 명령으로 시드해야 한다. 위치 피드백은 없다.
 */
void forearm_calibration_set_target(ForearmMotionState *state, const ForearmJointCommand *target);

/* 램프를 한 틱 진행시킨다. 중간 명령이 unsafe면 기존 출력을 유지하고
 * blocked_flags를 설정한다. 목표가 안전해도 그 사이 경로는 다를 수 있다.
 * 다른 경로 자동 탐색은 하지 않는다. */
void forearm_calibration_step(ForearmMotionState *state, ForearmJointCommand *output);

#endif /* ROBOT_CALIBRATION_FOREARM_CALIBRATION_H */
