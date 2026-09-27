#include <stddef.h>
#include <string.h>

#include "robot_calibration/forearm_calibration.h"
#include "robot_calibration/forearm_calibration_config.h"
#include "robot_calibration/forearm_safety_check.h"
#include "robot_calibration/motion.h"

/* 제어 틱 주기. motion.c(motion_init)가 20ms±1e-12만 받으므로 double
 * 리터럴이어야 한다(float 리터럴 0.020f는 이중 반올림 때문에 그 허용오차를
 * 못 만족한다). platform_tick_due()가 실제로 20ms 고정임을 전제로 한다. */
#define FOREARM_CONTROL_DT_SEC 0.020

int forearm_calibration_apply(const HumanForearmTarget *input, ForearmJointCommand *output)
{
    if (output == NULL) return 0;
    output->valid = 0;
    if (!forearm_motion_control_validate_target(input)) return 0;

    forearm_motion_control_map_target(input, output);
    forearm_motion_control_apply_limits(output);
    output->valid = 1;

    if (!forearm_safety_check_apply(output, NULL)) {
        output->valid = 0;
        return 0;
    }

    return 1;
}

/* 배열 순서: elbow_roll, elbow_pitch, wrist_pitch, wrist_roll (gripper 제외). */
static void joint_command_to_array(const ForearmJointCommand *command, float array[FOREARM_MOTION_JOINT_COUNT])
{
    array[0] = command->elbow_roll_deg;
    array[1] = command->elbow_pitch_deg;
    array[2] = command->wrist_pitch_deg;
    array[3] = command->wrist_roll_deg;
}

static void array_to_joint_command(const float array[FOREARM_MOTION_JOINT_COUNT], ForearmJointCommand *command)
{
    command->elbow_roll_deg = array[0];
    command->elbow_pitch_deg = array[1];
    command->wrist_pitch_deg = array[2];
    command->wrist_roll_deg = array[3];
}

/* JointCalibration 순서: elbow_roll, elbow_pitch, wrist_pitch, wrist_roll --
 * joint_command_to_array()와 동일 순서. vmax는 max_delta_deg(20ms당 최대
 * 변화각)를 그대로 초당 속도로 환산한 값이다(기존 legacy 계약과 동일 상한). */
static void fill_joint_limits(float lower[FOREARM_MOTION_JOINT_COUNT],
                               float upper[FOREARM_MOTION_JOINT_COUNT],
                               float vmax[FOREARM_MOTION_JOINT_COUNT])
{
    const JointCalibration *cal[FOREARM_MOTION_JOINT_COUNT] = {
        &forearm_calibration_config.elbow_roll, &forearm_calibration_config.elbow_pitch,
        &forearm_calibration_config.wrist_pitch, &forearm_calibration_config.wrist_roll
    };
    int i;
    for (i = 0; i < FOREARM_MOTION_JOINT_COUNT; i++) {
        lower[i] = cal[i]->min_deg;
        upper[i] = cal[i]->max_deg;
        vmax[i] = cal[i]->max_delta_deg / (float)FOREARM_CONTROL_DT_SEC;
    }
}

void forearm_calibration_state_init(ForearmMotionState *state)
{
    if (state == NULL) return;
    memset(state, 0, sizeof(*state));
}

void forearm_calibration_set_target(ForearmMotionState *state, const ForearmJointCommand *target)
{
    float target_array[FOREARM_MOTION_JOINT_COUNT];
    float lower[FOREARM_MOTION_JOINT_COUNT], upper[FOREARM_MOTION_JOINT_COUNT];
    float vmax[FOREARM_MOTION_JOINT_COUNT];
    int i;

    if (state == NULL || target == NULL) return;

    joint_command_to_array(target, target_array);
    fill_joint_limits(lower, upper, vmax);
    state->gripper = target->gripper_norm;

    /* motion_init()은 initial이 [lower,upper] 밖이면 아무것도 안 쓰고
     * MOTION_INVALID만 반환한다(그 축의 Motion이 0으로 방치됨). 이
     * 함수의 계약(호출자가 이미 안전검사한 값을 넣는다)상 정상적으로는
     * 범위 안이어야 하지만, motion_set_target()도 어차피 clamp하므로
     * 여기서도 똑같이 clamp해 두 경로가 항상 같은 값을 쓰게 만든다. */
    for (i = 0; i < FOREARM_MOTION_JOINT_COUNT; i++) {
        if (target_array[i] < lower[i]) target_array[i] = lower[i];
        else if (target_array[i] > upper[i]) target_array[i] = upper[i];
    }

    if (!state->has_target) {
        for (i = 0; i < FOREARM_MOTION_JOINT_COUNT; i++) {
            motion_init(&state->axes[i], target_array[i], lower[i], upper[i],
                        vmax[i], forearm_calibration_config.amax_deg_s2[i],
                        FOREARM_CONTROL_DT_SEC, SPEED_ACCEL);
        }
        state->has_target = 1;
    } else {
        for (i = 0; i < FOREARM_MOTION_JOINT_COUNT; i++) {
            motion_set_target(&state->axes[i], target_array[i]);
        }
    }
    /* hold 해제는 이 호출 여부로만 결정한다 -- 직전과 같은 수치의 target이
     * 다시 들어와도(예: agent_pipeline.c가 같은 명령으로 재개시킬 때) 여기까지
     * 도달했다면 무조건 재개시킨다. 호출자가 "같은 명령이면 여기 호출을
     * 스킵"하면 안 된다(hold는 axes[i].v=0/target=q로 얼어붙은 채 영원히 안
     * 풀린다) -- 이 파일 헤더의 forearm_calibration_set_target() 주석 참고.
     * blocked_flags는 이번 틱 상태 표시용이라 다음 스텝에 자동으로 OK가 될 수
     * 있다(멈춘 자리 자체는 항상 안전검사를 통과하므로) -- 그래서 "재개가
     * 필요한가"는 blocked_flags가 아니라 아래 held로 판단해야 한다. */
    state->blocked_flags = FOREARM_SAFETY_CHECK_OK;
    state->held = 0;
}

void forearm_calibration_step(ForearmMotionState *state, ForearmJointCommand *output)
{
    Motion trial[FOREARM_MOTION_JOINT_COUNT];
    float next[FOREARM_MOTION_JOINT_COUNT];
    ForearmJointCommand candidate;
    ForearmSafetyCheckFlags issues;
    int feasible;
    int i;

    if (state == NULL || output == NULL) return;

    if (!state->has_target) {
        output->valid = 0;
        return;
    }

    feasible = 1;
    for (i = 0; i < FOREARM_MOTION_JOINT_COUNT; i++) {
        trial[i] = state->axes[i];
        if (motion_step(&trial[i]) != MOTION_OK) feasible = 0;
        next[i] = (float)trial[i].q;
    }

    array_to_joint_command(next, &candidate);
    candidate.gripper_norm = state->gripper;
    candidate.valid = 1;

    if (feasible && forearm_safety_check_apply(&candidate, &issues)) {
        /* 4축을 함께 commit한다 -- 사본으로만 계산했으므로 실패 시 실제
         * state->axes는 아직 이번 틱 이전 그대로다. */
        memcpy(state->axes, trial, sizeof(state->axes));
        state->blocked_flags = FOREARM_SAFETY_CHECK_OK;
    } else {
        /* 후보 폐기 + 4축 모두 그 자리에서 속도를 0으로 묶는다. 위험한
         * 후보를 다음 틱에도 다시 시도하겠지만(state는 그대로라 재계산해도
         * 같은 후보가 나옴), emergency_hold가 매틱 target=q/v=0을 다시
         * 걸어주므로 내부 속도가 누적되지 않는다. 새 목표(set_target)가
         * 오면 그때 재개한다. */
        for (i = 0; i < FOREARM_MOTION_JOINT_COUNT; i++) {
            motion_emergency_hold(&state->axes[i]);
        }
        state->blocked_flags = feasible ? issues : FOREARM_SAFETY_CHECK_INVALID_COMMAND;
        /* blocked_flags와 달리 다음 틱에 저절로 안 풀린다 -- set_target()만
         * 풀 수 있다(Codex 코드리뷰 2026-09-24: 안 그러면 멈춘 자리가 다음
         * 틱에 스스로 안전검사를 통과해버려서 agent_pipeline.c의 재개 판단이
         * "이미 재개됐다"고 착각한다). */
        state->held = 1;
    }

    for (i = 0; i < FOREARM_MOTION_JOINT_COUNT; i++) {
        next[i] = (float)state->axes[i].q;
    }
    array_to_joint_command(next, output);
    output->gripper_norm = state->gripper;
    output->valid = 1;
}
