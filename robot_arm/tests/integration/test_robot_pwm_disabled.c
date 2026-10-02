#include <assert.h>
#include <stdio.h>

#include "drivers/servo_pwm_driver.h"
#include "integration/agent_pipeline.h"
#include "output_controller/servo_hal.h"

int main(void)
{
    AgentPipelineContext pipeline;
    ServoPwmDriverMockWrite write;
    servo_pwm_driver_mock_reset();
    servo_hal_init();
    assert(agent_pipeline_init_mode(&pipeline, 0) == 0);
    assert(!pipeline.output_enabled);
    assert(servo_pwm_driver_mock_get_log_count() == 1U);
    assert(servo_pwm_driver_mock_get_log(0U, &write));
    assert(write.offset == 0x18U && write.value == 0U);
    assert(agent2_tick(&pipeline));
    assert(agent3_run(&pipeline));
    assert(servo_pwm_driver_mock_get_log_count() == 1U);
    assert(pipeline.servo_writes == 0U && pipeline.servo_errors == 0U);
    assert(agent_pipeline_set_output_enabled(&pipeline, 1));
    assert(pipeline.output_enabled && pipeline.applied_command_valid);
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
    pipeline.motion.axes[0].v = 1.0;
    assert(!agent_pipeline_set_output_enabled(&pipeline, 1));
    pipeline.motion.axes[0].v = 0.0;
    pipeline.motion.axes[0].target += 1.0;
    assert(!agent_pipeline_set_output_enabled(&pipeline, 1));
    pipeline.motion.axes[0].target -= 1.0;
    pipeline.output.elbow_roll_deg += 1.0f;
    assert(!agent_pipeline_set_output_enabled(&pipeline, 1));
    assert(servo_pwm_driver_mock_get_log_count() == 9U);
    pipeline.output = pipeline.applied_command;
    assert(agent_pipeline_set_output_enabled(&pipeline, 1));
    assert(!agent_pipeline_set_output_enabled(NULL, 1));
    puts("test_robot_pwm_disabled: PASS");
    return 0;
}
