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
    puts("test_robot_pwm_disabled: PASS");
    return 0;
}
