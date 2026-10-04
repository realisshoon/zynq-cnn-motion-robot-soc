#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "drivers/servo_pwm_driver.h"
#include "integration/agent_pipeline.h"
#include "output_controller/servo_hal.h"

static void assert_same_command(const ForearmJointCommand *actual,
                                const ForearmJointCommand *expected)
{
    assert(actual->elbow_roll_deg == expected->elbow_roll_deg);
    assert(actual->elbow_pitch_deg == expected->elbow_pitch_deg);
    assert(actual->wrist_pitch_deg == expected->wrist_pitch_deg);
    assert(actual->wrist_roll_deg == expected->wrist_roll_deg);
    assert(actual->gripper_norm == expected->gripper_norm);
    assert(actual->valid == expected->valid);
}

static void assert_anchored(const AgentPipelineContext *pipeline,
                            const ForearmJointCommand *reference)
{
    const float positions[FOREARM_MOTION_JOINT_COUNT] = {
        reference->elbow_roll_deg, reference->elbow_pitch_deg,
        reference->wrist_pitch_deg, reference->wrist_roll_deg
    };
    unsigned axis;
    assert_same_command(&pipeline->output, reference);
    assert_same_command(&pipeline->command, reference);
    assert_same_command(&pipeline->agent3_command, reference);
    assert(pipeline->command_valid && pipeline->agent3_command_valid);
    assert(pipeline->motion.has_target && !pipeline->motion.held);
    assert(pipeline->motion.gripper == reference->gripper_norm);
    assert(!pipeline->target_ready);
    for (axis = 0U; axis < FOREARM_MOTION_JOINT_COUNT; ++axis) {
        assert(pipeline->motion.axes[axis].q == positions[axis]);
        assert(pipeline->motion.axes[axis].target == positions[axis]);
        assert(pipeline->motion.axes[axis].v == 0.0);
    }
}

int main(void)
{
    AgentPipelineContext pipeline;
    ForearmJointCommand reference;
    ForearmMotionState frozen;
    ServoPwmDriverMockWrite write;
    unsigned tick;
    servo_pwm_driver_mock_reset();
    servo_hal_init();
    assert(agent_pipeline_init_mode(&pipeline, 0) == 0);
    assert(!pipeline.output_enabled);
    assert(servo_pwm_driver_mock_get_log_count() == 1U);
    assert(servo_pwm_driver_mock_get_log(0U, &write));
    assert(write.offset == 0x18U && write.value == 0U);
    reference = pipeline.output;
    pipeline.target = (HumanForearmTarget){
        .elbow_roll_deg = -20.0f, .elbow_pitch_deg = 40.0f,
        .wrist_pitch_deg = 10.0f, .wrist_roll_deg = -7.0f,
        .gripper_norm = 0.4f, .valid = 1U, .wrist_valid = 1U,
        .gripper_valid = 1U
    };
    pipeline.target_ready = 1U;
    assert(agent2_run(&pipeline));
    assert(pipeline.command_valid && pipeline.retargets == 1U);
    for (tick = 0U; tick < 250U; ++tick) {
        assert(agent2_tick(&pipeline));
        assert(agent3_run(&pipeline));
    }
    assert(agent2_tick(&pipeline));
    assert(agent3_run(&pipeline));
    assert(servo_pwm_driver_mock_get_log_count() == 1U);
    assert(pipeline.servo_writes == 0U && pipeline.servo_errors == 0U);
    assert(!pipeline.applied_command_valid && !pipeline.output_parked);
    assert(pipeline.output.elbow_roll_deg != reference.elbow_roll_deg);
    assert(agent_pipeline_set_output_enabled(&pipeline, 1));
    assert(pipeline.output_enabled && pipeline.applied_command_valid);
    assert_anchored(&pipeline, &reference);
    assert(servo_pwm_driver_mock_get_log_count() == 8U);
    assert(servo_pwm_driver_mock_get_log(6U, &write));
    assert(write.offset == 0x1CU && write.value == 1U);
    assert(servo_pwm_driver_mock_get_log(7U, &write));
    assert(write.offset == 0x18U && write.value == 1U);
    assert(agent_pipeline_set_output_enabled(&pipeline, 1));
    assert(servo_pwm_driver_mock_get_log_count() == 8U);
    assert(agent_pipeline_set_output_enabled(&pipeline, 0));
    assert(!pipeline.output_enabled);
    assert(servo_pwm_driver_mock_get_log(8U, &write));
    assert(write.offset == 0x18U && write.value == 0U);
    assert_anchored(&pipeline, &reference);
    assert(pipeline.output_parked);
    frozen = pipeline.motion;
    for (tick = 0U; tick < 250U; ++tick) {
        pipeline.target_ready = 1U;
        assert(!agent2_run(&pipeline));
        assert(agent2_tick(&pipeline));
        assert(agent3_run(&pipeline));
        assert_same_command(&pipeline.output, &reference);
        assert(memcmp(&pipeline.motion, &frozen, sizeof(frozen)) == 0);
    }
    pipeline.motion.axes[0].v = 1.0;
    assert(agent_pipeline_set_output_enabled(&pipeline, 1));
    assert_anchored(&pipeline, &reference);
    assert(agent_pipeline_set_output_enabled(&pipeline, 0));
    pipeline.motion.axes[0].target += 1.0;
    assert(agent_pipeline_set_output_enabled(&pipeline, 1));
    assert_anchored(&pipeline, &reference);
    assert(agent_pipeline_set_output_enabled(&pipeline, 0));
    pipeline.output.elbow_roll_deg += 1.0f;
    pipeline.output.gripper_norm = 0.9f;
    assert(agent3_run(&pipeline));
    assert_same_command(&pipeline.applied_command, &reference);
    assert(agent_pipeline_set_output_enabled(&pipeline, 1));
    assert_anchored(&pipeline, &reference);
    assert(servo_pwm_driver_mock_get_log_count() == 32U);
    assert(!agent_pipeline_set_output_enabled(NULL, 1));
    puts("test_robot_pwm_disabled: PASS");
    return 0;
}
