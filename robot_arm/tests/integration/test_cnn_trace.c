#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "integration/trace.h"
#include "human_target_angle/agent1_forearm_stage.h"
#include "robot_calibration/forearm_safety_check.h"

static uint32_t now_us;
static char output[8192];
static unsigned output_len;

/* Unused legacy trace paths still need definitions on MinGW's PE linker. */
const ForearmMappingContext *agent1_forearm_stage_debug_context(void)
{
    return NULL;
}
int forearm_safety_check_apply(const ForearmJointCommand *command,
                               ForearmSafetyCheckFlags *flags)
{
    (void)command;
    (void)flags;
    return 0;
}

uint32_t platform_trace_time_us(void) { return now_us; }
void platform_trace_stats(TracePlatformStats *stats)
{
    memset(stats, 0, sizeof(*stats));
}
uint32_t platform_trace_tx(const uint8_t *data, uint32_t length)
{
    assert(output_len + length < sizeof(output));
    memcpy(output + output_len, data, length);
    output_len += length;
    output[output_len] = '\0';
    return length;
}

int main(void)
{
    HumanPose2D pose;
    AgentPipelineContext pipeline;
    unsigned i;
    memset(&pose, 0, sizeof(pose));
    memset(&pipeline, 0, sizeof(pipeline));
    trace_init();
    pose.frame_id = 7U;
    pose.shoulder_l = (Point2D){100.0f, 200.0f, 1U};
    pose.wrist = (Point2D){500.0f, 400.0f, 1U};
    now_us = 20000U;
    trace_cnn_frame(7U, 9U, 3U, 4200U, 0x540U, 0U);
    trace_camera(7U, 2U, 1500U, 1510U, 1520U, 1530U);
    trace_input(&pose);
    trace_cnn_error(8U, -14, 4U);
    for (i = 0U; i < 1000U; ++i) trace_poll(NULL);
    assert(strstr(output, "CN,7,20,9,3,4200,0x540,0\r\n") != NULL);
    assert(strstr(output, "CAM,7,20,2,1500,1510,1520,1530\r\n") != NULL);
    assert(strstr(output, "IN,7,20,100.0,200.0,1") != NULL);
    assert(strstr(output, "CE,8,20,-14\r\n") != NULL);
    now_us = 1000000U;
    for (i = 0U; i < 257U; ++i) trace_poll(&pipeline);
    assert(strstr(output, "CS,1000,4,1,1,1,4200,4200,0\r\n") != NULL);
    puts("test_cnn_trace: PASS");
    return 0;
}
