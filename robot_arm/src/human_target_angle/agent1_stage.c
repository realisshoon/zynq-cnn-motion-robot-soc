#include "human_target_angle/agent1_stage.h"

#include <string.h>

static PoseMappingContext g_agent1_ctx;
static HumanJointTarget g_agent1_output;
static int g_agent1_initialized = 0;

int agent1_stage_init(void)
{
    int ret;

    memset(&g_agent1_ctx, 0, sizeof(g_agent1_ctx));
    memset(&g_agent1_output, 0, sizeof(g_agent1_output));

    ret = pose_mapping_init(&g_agent1_ctx);
    if (ret != 0) {
        g_agent1_initialized = 0;
        return ret;
    }

    g_agent1_initialized = 1;
    return 0;
}

int agent1_stage_run(
    const HumanPose2D *pose,
    PoseArmSide active_arm,
    float dt_sec
)
{
    if (!g_agent1_initialized || pose == 0) {
        return -1;
    }

    return pose_mapping_update(
        &g_agent1_ctx,
        pose,
        active_arm,
        dt_sec,
        &g_agent1_output
    );
}

const HumanJointTarget *agent1_stage_output(void)
{
    return &g_agent1_output;
}

int agent1_stage_output_valid(void)
{
    return g_agent1_initialized &&
           (g_agent1_output.valid != 0U);
}
