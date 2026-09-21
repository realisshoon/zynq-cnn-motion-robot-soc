#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "robot_calibration/safety_check.h"

#define POSITION_TOLERANCE_CM 0.001f

static int nearly_equal(float actual, float expected)
{
    return fabsf(actual - expected) <= POSITION_TOLERANCE_CM;
}

static JointCommand make_command(float shoulder_deg, float elbow_deg, float wrist_pitch_deg)
{
    JointCommand command = {
        .base_deg = 90.0f,
        .shoulder_deg = shoulder_deg,
        .elbow_deg = elbow_deg,
        .wrist_pitch_deg = wrist_pitch_deg,
        .wrist_roll_deg = 90.0f,
        .gripper_norm = 0.5f,
        .valid = 1u,
    };

    return command;
}

static void test_forward_kinematics(void)
{
    JointCommand command = make_command(90.0f, 90.0f, 90.0f);
    RobotJointPositions2D positions;

    assert(robot_forward_kinematics_2d(&command, &positions) == 1);
    assert(nearly_equal(positions.shoulder.x_cm, 0.0f));
    assert(nearly_equal(positions.shoulder.z_cm, 0.0f));
    assert(nearly_equal(positions.elbow.x_cm, 0.0f));
    assert(nearly_equal(positions.elbow.z_cm, 11.0f));
    assert(nearly_equal(positions.wrist_pitch.x_cm, 0.0f));
    assert(nearly_equal(positions.wrist_pitch.z_cm, 24.0f));
    assert(nearly_equal(positions.tip.x_cm, 0.0f));
    assert(nearly_equal(positions.tip.z_cm, 32.0f));

    command = make_command(0.0f, 150.0f, 30.0f);
    command.base_deg = 25.0f;
    command.wrist_roll_deg = 160.0f;
    assert(robot_forward_kinematics_2d(&command, &positions) == 1);
    assert(nearly_equal(positions.elbow.x_cm, 11.0f));
    assert(nearly_equal(positions.elbow.z_cm, 0.0f));
    assert(nearly_equal(positions.wrist_pitch.x_cm, 17.5f));
    assert(nearly_equal(positions.wrist_pitch.z_cm, 11.25833f));
    assert(nearly_equal(positions.tip.x_cm, 25.5f));
    assert(nearly_equal(positions.tip.z_cm, 11.25833f));
}

static void test_safe_pose(void)
{
    JointCommand command = make_command(45.0f, 140.0f, 40.0f);
    RobotJointPositions2D positions;
    SafetyCheckFlags issues = UINT32_MAX;

    assert(safety_check_apply(&command, &positions, &issues) == 1);
    assert(issues == SAFETY_CHECK_OK);
    assert(positions.elbow.z_cm > 0.0f);
    assert(positions.wrist_pitch.z_cm > 0.0f);
    assert(positions.tip.z_cm > 0.0f);
}

static void test_self_collision(void)
{
    JointCommand command = make_command(20.0f, 246.0f, 90.0f);
    SafetyCheckFlags issues;

    assert(safety_check_apply(&command, NULL, &issues) == 0);
    assert((issues & SAFETY_CHECK_SELF_COLLISION) != 0u);
    assert((issues & SAFETY_CHECK_FLOOR_COLLISION) == 0u);
}

static void test_floor_collision(void)
{
    JointCommand command = make_command(20.0f, 10.0f, 90.0f);
    SafetyCheckFlags issues;

    assert(safety_check_apply(&command, NULL, &issues) == 0);
    assert((issues & SAFETY_CHECK_FLOOR_COLLISION) != 0u);
}

static void test_fully_extended_singularity(void)
{
    JointCommand command = make_command(45.0f, 90.0f, 90.0f);
    SafetyCheckFlags issues;

    assert(safety_check_apply(&command, NULL, &issues) == 0);
    assert((issues & SAFETY_CHECK_NEAR_SINGULARITY) != 0u);
    assert((issues & SAFETY_CHECK_REACH_BOUNDARY) != 0u);
    assert((issues & SAFETY_CHECK_SELF_COLLISION) == 0u);
}

static void test_invalid_command(void)
{
    JointCommand command = make_command(45.0f, 140.0f, 40.0f);
    SafetyCheckFlags issues;

    command.valid = 0u;
    assert(safety_check_apply(&command, NULL, &issues) == 0);
    assert(issues == SAFETY_CHECK_INVALID_COMMAND);

    assert(safety_check_apply(NULL, NULL, &issues) == 0);
    assert(issues == SAFETY_CHECK_INVALID_COMMAND);
}

int main(void)
{
    test_forward_kinematics();
    test_safe_pose();
    test_self_collision();
    test_floor_collision();
    test_fully_extended_singularity();
    test_invalid_command();

    puts("test_safety_check: all tests passed");
    return 0;
}
