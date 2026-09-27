#include <assert.h>
#include <stdio.h>
#include "human_target_angle/agent1_stage.h"
#include "human_target_angle/agent1_forearm_stage.h"

int main(void)
{
    assert(agent1_stage_init()==0);
    assert(agent1_stage_debug_context()->initialized);
    assert(agent1_forearm_stage_init()==0);
    assert(agent1_forearm_stage_debug_context()->pose.initialized);
    assert(!agent1_forearm_stage_debug_context()->pose.body_frame_valid);
    assert(agent1_forearm_stage_start_roll_zero_calibration()==0);
    assert(agent1_forearm_stage_debug_context()->pose.roll_zero_calibrating);
    agent1_forearm_stage_clear_roll_zero();
    assert(!agent1_forearm_stage_debug_context()->pose.roll_zero_calibrating);
    puts("ROBOT_TRACE legacy + forearm getter compile/link/run: PASS");
    return 0;
}
