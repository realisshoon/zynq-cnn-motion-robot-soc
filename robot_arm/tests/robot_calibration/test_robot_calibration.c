#include <assert.h>
#include <math.h>
#include <stddef.h>

#include "robot_calibration/motion_control.h"
#include "robot_calibration/robot_calibration.h"
int main(void)
{
    HumanJointTarget target = {
        .base_deg = 0.0f,
        .shoulder_deg = 30.0f,
        .elbow_deg = 90.0f,
        .wrist_pitch_deg = 10.0f,
        .wrist_roll_deg = 0.0f,
        .gripper_norm = 0.5f,
        .valid = 1
    };

    assert(motion_control_validate_target(&target) == 1);

    target.valid = 0;
    assert(motion_control_validate_target(&target) == 0);

    target.valid = 1;
    target.elbow_deg = NAN;
    assert(motion_control_validate_target(&target) == 0);

    assert(motion_control_validate_target(NULL) == 0);

    JointCommand command = {
    .base_deg = -100.0f,
    .shoulder_deg = 200.0f,
    .elbow_deg = 500.0f,
    .wrist_pitch_deg = -50.0f,
    .wrist_roll_deg = 300.0f,
    .gripper_norm = 2.0f,
    .valid = 1
    };

    motion_control_apply_limits(&command);

    assert(command.base_deg == 10.0f);
    assert(command.shoulder_deg == 160.0f);
    assert(command.elbow_deg == 170.0f);
    assert(command.wrist_pitch_deg == 20.0f);
    assert(command.wrist_roll_deg == 180.0f);
    /* gripper_norm은 clamp 대상이 아니라 그대로 통과한다. */
    assert(command.gripper_norm == 2.0f);

    HumanJointTarget apply_target = {
        .base_deg = 0.0f,
        .shoulder_deg = 30.0f,
        .elbow_deg = 90.0f,
        .wrist_pitch_deg = 10.0f,
        .wrist_roll_deg = 0.0f,
        .gripper_norm = 0.5f,
        .valid = 1
    };
    JointCommand apply_output;

    assert(robot_calibration_apply(&apply_target, &apply_output) == 1);
    assert(apply_output.valid == 1);
    assert(apply_output.base_deg == 90.0f);
    assert(apply_output.shoulder_deg == 120.0f);
    assert(apply_output.elbow_deg == 170.0f);
    assert(apply_output.wrist_pitch_deg == 100.0f);
    assert(apply_output.wrist_roll_deg == 90.0f);
    assert(apply_output.gripper_norm == 0.5f);

    apply_target.valid = 0;
    assert(robot_calibration_apply(&apply_target, &apply_output) == 0);
    assert(robot_calibration_apply(NULL, &apply_output) == 0);
    assert(robot_calibration_apply(&apply_target, NULL) == 0);

    HumanJointTarget unsafe_target = {
        .base_deg = 0.0f,
        .shoulder_deg = -90.0f,
        .elbow_deg = -100.0f,
        .wrist_pitch_deg = 0.0f,
        .wrist_roll_deg = 0.0f,
        .gripper_norm = 0.5f,
        .valid = 1
    };
    JointCommand unsafe_output;

    assert(robot_calibration_apply(&unsafe_target, &unsafe_output) == 0);
    assert(unsafe_output.valid == 0);

    RobotMotionState state;
    robot_calibration_state_init(&state);

    JointCommand first_target = {
        .base_deg = 90.0f,
        .shoulder_deg = 90.0f,
        .elbow_deg = 90.0f,
        .wrist_pitch_deg = 90.0f,
        .wrist_roll_deg = 90.0f,
        .gripper_norm = 0.0f,
        .valid = 1
    };
    JointCommand step_output;

    /* 부트스트랩: 최초 target은 램프 없이 즉시 스냅된다. */
    robot_calibration_set_target(&state, &first_target);
    robot_calibration_step(&state, &step_output);
    assert(step_output.valid == 1);
    assert(step_output.gripper_norm == 0.0f);
    assert(step_output.shoulder_deg == 90.0f);

    /* 재목표(re-target) 도중: shoulder는 90 -> 120(속도제한 3도/틱 -> 단독
     * 10틱), elbow는 90 -> 98(속도제한 4도/틱 -> 단독 2틱)로 서로 다른
     * 거리를 움직인다. 다관절 동기화 규칙상 둘 다 "제일 느린 관절"인
     * shoulder 기준 공통 시간(10틱, smoothstep 1.5배 -> 15틱)에 맞춰
     * 같이 도착해야 한다 -- elbow가 자기 혼자 낼 수 있는 속도로 먼저
     * 끝내버리면 안 됨. gripper는 0.0 -> 1.0으로 램프 없이 다음 틱에
     * 바로 반영돼야 한다. */
    JointCommand second_target = first_target;
    second_target.shoulder_deg = 120.0f;
    second_target.elbow_deg = 98.0f;
    second_target.gripper_norm = 1.0f;
    robot_calibration_set_target(&state, &second_target);

    robot_calibration_step(&state, &step_output);
    assert(step_output.valid == 1);
    assert(step_output.gripper_norm == 1.0f);
    assert(step_output.shoulder_deg > 90.0f);
    assert(step_output.shoulder_deg < 120.0f);

    int tick;
    for (tick = 1; tick < 15; tick++) {
        robot_calibration_step(&state, &step_output);
        assert(step_output.valid == 1);
        /* gripper는 램프 대상이 아니므로 재목표 이후 계속 목표값 그대로. */
        assert(step_output.gripper_norm == 1.0f);
        if (tick == 2) {
            /* elbow 혼자였다면 2틱 만에 끝났을 시점인데, shoulder에
             * 맞춰 페이싱되고 있어서 아직 목표(98)에 도달하면 안 된다. */
            assert(step_output.elbow_deg < 98.0f - 0.0001f);
        }
    }
    assert(fabsf(step_output.shoulder_deg - 120.0f) < 0.0001f);
    assert(fabsf(step_output.elbow_deg - 98.0f) < 0.0001f);

    /* 도착 이후 추가로 틱이 와도 목표값에 머문다. */
    robot_calibration_step(&state, &step_output);
    assert(fabsf(step_output.shoulder_deg - 120.0f) < 0.0001f);
    assert(fabsf(step_output.elbow_deg - 98.0f) < 0.0001f);
    assert(step_output.gripper_norm == 1.0f);

    /* NULL 안전성: 아래 호출들이 크래시하면 안 된다. */
    robot_calibration_state_init(NULL);
    robot_calibration_set_target(NULL, &second_target);
    robot_calibration_set_target(&state, NULL);
    robot_calibration_step(NULL, &step_output);
    robot_calibration_step(&state, NULL);

    return 0;
}