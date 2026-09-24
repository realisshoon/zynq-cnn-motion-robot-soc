#ifndef ROBOT_CALIBRATION_FOREARM_CALIBRATION_H
#define ROBOT_CALIBRATION_FOREARM_CALIBRATION_H

#include "robot_calibration/forearm_motion_control.h"
#include "robot_calibration/forearm_safety_check.h"
#include "robot_calibration/motion.h"

/*
 * Agent2 새 5축 공개 API. Agent1의 HumanForearmTarget을 받아 보정 및
 * 안전검사를 마친 ForearmJointCommand로 변환한다.
 * 현재 지원하는 유일한 로봇 보정 경로다.
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
 * 틱 단위 램프 상태. D:\Working\robot-motion-harness에서 Codex가 설계/검증한
 * motion.c(Motion, SPEED_ACCEL)를 관절마다 하나씩 4개 써서, 실제 속도 상태를
 * 유지하며 매틱 가속도+관절한계 제동거리를 지키는 궤적을 만든다(문서:
 * D:\Working\robot-motion-harness\README.md). 이전의 "정해진 tick 수에 걸쳐
 * 보간(smoothstep/선형), 가속도 제한 없음" 방식을 대체한다 — 처음 쓰기 전에
 * forearm_calibration_state_init()으로 0 초기화한다.
 *
 * 사용법: forearm_calibration_apply()가 새 ForearmJointCommand를 만들 때마다
 * forearm_calibration_set_target() 호출, 고정 제어 틱(20ms)마다
 * forearm_calibration_step() 호출.
 *
 * axes 배열 순서: elbow_roll, elbow_pitch, wrist_pitch, wrist_roll --
 * ForearmJointCommand 필드 순서와 동일(gripper 제외).
 */
typedef struct {
    Motion axes[FOREARM_MOTION_JOINT_COUNT];
    float gripper;
    int has_target;
    /* Unsafe intermediate command: hold last output and expose the cause
     * FOR THIS TICK ONLY. A rejected tick freezes each axis at its last safe
     * position (motion_emergency_hold: target=q, v=0) -- and that frozen
     * position trivially re-passes the safety check on the very next tick
     * (같은 자리, e=0이라 항상 안전), so blocked_flags snaps back to OK one
     * tick later even though the axes never actually resumed toward the
     * real target. blocked_flags alone is NOT a reliable "still stuck"
     * signal -- use `held` for that (Codex 코드리뷰 2026-09-24 발견).
     * A new target clears both. This checks sampled centerline poses, not
     * swept volume, mechanical thickness, or actual servo feedback. */
    ForearmSafetyCheckFlags blocked_flags;
    /* 1이면 현재 축들이 emergency_hold로 얼어붙은 채(내부 target이 실제
     * 원하는 목표가 아니라 정지 위치로 덮어써진 상태)라는 뜻이다.
     * forearm_calibration_set_target()을 호출해야만 0으로 풀린다 --
     * blocked_flags와 달리 매틱 자동으로 안 풀린다. 호출자(agent_pipeline.c)는
     * "같은 명령이면 재계획 생략" 최적화를 할 때 이 플래그로 재개가 필요한지
     * 판단해야 한다(blocked_flags로 판단하면 안 됨). */
    int held;
} ForearmMotionState;

void forearm_calibration_state_init(ForearmMotionState *state);

/*
 * 로봇팔의 현재 명령 위치에서 target까지 가는 목표를 다시 세운다(각 축
 * motion_set_target() 그대로 위임 -- 목표만 갱신, 현재 속도/위치는 안 건드림).
 * 이전 램프가 끝나기 전에 다시 부를 수 있고, hold 중이었어도 이 호출로
 * 곧바로 새 목표를 향해 재개한다(호출자가 이전과 같은 값을 다시 넣어도
 * 마찬가지 -- hold 해제는 이 함수 호출 여부로만 결정된다, ctx->command와의
 * 비교로 스킵하지 말 것). 최초 호출은 곧바로 target으로 스냅한다.
 * target은 forearm_calibration_apply() 등으로 안전검사한 valid==1 명령이어야 한다.
 * 최초 target은 실측/승인한 시작 명령으로 시드해야 한다. 위치 피드백은 없다.
 */
void forearm_calibration_set_target(ForearmMotionState *state, const ForearmJointCommand *target);

/* 램프를 한 틱 진행시킨다. 4축 사본에 각각 motion_step()을 적용해 후보를
 * 만들고, 합쳐서 forearm_safety_check_apply()를 통과해야 4축을 함께 commit한다.
 * 실패하면(motion_step 자체 실패 포함) 사본을 버리고 4축 모두
 * motion_emergency_hold()를 적용해 그 자리에서 속도를 0으로 묶는다 --
 * 위험한 후보를 매틱 다시 시도하며 내부 속도가 누적되지 않게 하기 위해서다.
 * blocked_flags에 사유를 남긴다. 목표가 안전해도 그 사이 경로는 다를 수
 * 있고, 다른 경로 자동 탐색은 하지 않는다. */
void forearm_calibration_step(ForearmMotionState *state, ForearmJointCommand *output);

#endif /* ROBOT_CALIBRATION_FOREARM_CALIBRATION_H */
