#include <stddef.h>
#include <string.h>

#include "robot_calibration/robot_calibration.h"
#include "robot_calibration/motion_control.h"
#include "robot_calibration/robot_calibration_config.h"
#include "robot_calibration/safety_check.h"
#include "robot_calibration/motion_limits.h"
#include "robot_calibration/motion_smoothing.h"

int robot_calibration_apply(const HumanJointTarget *input, JointCommand *output)
{
    if (output == NULL) return 0;
    if (!motion_control_validate_target(input)) return 0;

    motion_control_map_target(input, output);
    motion_control_apply_limits(output);
    output->valid = 1;

    if (!safety_check_apply(output, NULL, NULL)) {
        output->valid = 0;
        return 0;
    }

    return 1;
}

/* 배열 순서: base, shoulder, elbow, wrist_pitch, wrist_roll (gripper 제외).
 * JointCommand 필드 순서와 동일 (docs/agent2_design_log.md 참고). */
static void joint_command_to_array(const JointCommand *command, float array[ROBOT_MOTION_JOINT_COUNT])
{
    array[0] = command->base_deg;
    array[1] = command->shoulder_deg;
    array[2] = command->elbow_deg;
    array[3] = command->wrist_pitch_deg;
    array[4] = command->wrist_roll_deg;
}

static void array_to_joint_command(const float array[ROBOT_MOTION_JOINT_COUNT], JointCommand *command)
{
    command->base_deg = array[0];
    command->shoulder_deg = array[1];
    command->elbow_deg = array[2];
    command->wrist_pitch_deg = array[3];
    command->wrist_roll_deg = array[4];
}

static void fill_max_delta_per_tick(float array[ROBOT_MOTION_JOINT_COUNT])
{
    array[0] = robot_calibration_config.base.max_delta_deg;
    array[1] = robot_calibration_config.shoulder.max_delta_deg;
    array[2] = robot_calibration_config.elbow.max_delta_deg;
    array[3] = robot_calibration_config.wrist_pitch.max_delta_deg;
    array[4] = robot_calibration_config.wrist_roll.max_delta_deg;
}

void robot_calibration_state_init(RobotMotionState *state)
{
    if (state == NULL) return;

    memset(state, 0, sizeof(*state));
}

void robot_calibration_set_target(RobotMotionState *state, const JointCommand *target)
{
    float target_array[ROBOT_MOTION_JOINT_COUNT];
    float max_delta[ROBOT_MOTION_JOINT_COUNT];
    int total_ticks = 0;
    int i;

    if (state == NULL || target == NULL) return;

    joint_command_to_array(target, target_array);
    fill_max_delta_per_tick(max_delta);
    /* gripper는 램프하지 않고 목표값을 그대로 보관한다 (agent3에서
     * 세부 처리하는 단순 전달값). */
    state->gripper = target->gripper_norm;

    if (!state->has_target) {
        /* 부트스트랩: 아직 알려진 이전 위치가 없으므로, 임의로 0인 자세에서
         * 램프하지 않고 바로 첫 목표로 스냅한다. */
        memcpy(state->current, target_array, sizeof(state->current));
        state->has_target = 1;
    }

    /* 관절 중 가장 느린 것(개별로 혼자 움직였을 때 제일 오래 걸리는 것)
     * 기준으로 공통 소요 시간을 정한다. motion_limits는 관절 개수를
     * 몰라도 되는 스칼라 유틸이라, "몇 개인지/어느 게 제일 느린지"는
     * 개수를 아는 이쪽(robot_calibration)이 직접 순회하며 계산한다. */
    for (i = 0; i < ROBOT_MOTION_JOINT_COUNT; i++) {
        int joint_ticks = motion_limits_ticks_to_target(
            state->current[i], target_array[i], max_delta[i]);

        if (joint_ticks < 0) {
            /* 설정이 잘못된 경우(예: 아직 이동해야 하는 관절의
             * max_delta_per_tick이 양수가 아님). 임의로 추측하지 않고
             * 진행 중이던 램프를 그대로 유지한다. */
            return;
        }
        if (joint_ticks > total_ticks) {
            total_ticks = joint_ticks;
        }
    }

    memcpy(state->start, state->current, sizeof(state->start));
    memcpy(state->target, target_array, sizeof(state->target));
    memcpy(state->max_delta_per_tick, max_delta, sizeof(state->max_delta_per_tick));
    /* 틱당 이동 모양은 motion_smoothing의 smoothstep 곡선이 담당하고,
     * robot_calibration_step()에서는 (원래 값 그대로인) max_delta_per_tick으로
     * motion_limits_step_toward()를 방어적 하드 clamp로만 사용한다. 위에서
     * 계산한 total_ticks는 그중 "가장 느린 관절 기준 공통 소요 시간"이다. */
    state->stretched_ticks = motion_smoothing_stretch_ticks(total_ticks);
    state->ticks_elapsed = 0;
}

void robot_calibration_step(RobotMotionState *state, JointCommand *output)
{
    float progress;
    float eased;
    int i;

    if (state == NULL || output == NULL) return;

    if (!state->has_target || state->stretched_ticks <= 0) {
        array_to_joint_command(state->current, output);
        output->gripper_norm = state->gripper;
        output->valid = state->has_target ? 1 : 0;
        return;
    }

    if (state->ticks_elapsed < state->stretched_ticks) {
        state->ticks_elapsed++;
    }

    progress = (float)state->ticks_elapsed / (float)state->stretched_ticks;
    eased = motion_smoothing_ease(progress);

    for (i = 0; i < ROBOT_MOTION_JOINT_COUNT; i++) {
        float ideal = state->start[i] + (state->target[i] - state->start[i]) * eased;
        state->current[i] = motion_limits_step_toward(state->current[i], ideal, state->max_delta_per_tick[i]);
    }

    array_to_joint_command(state->current, output);
    /* gripper는 램프하지 않고 매 틱 목표값을 그대로 내보낸다. */
    output->gripper_norm = state->gripper;
    output->valid = 1;
}
