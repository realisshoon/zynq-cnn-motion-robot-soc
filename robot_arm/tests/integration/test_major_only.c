#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "drivers/servo_pwm_driver.h"
#include "integration/agent_pipeline.h"
#include "output_controller/servo_hal.h"

static Point2D point(float x, float y)
{
    Point2D p = { x, y, 1U };
    return p;
}

static void test_no_fingers_through_pipeline(void)
{
    AgentPipelineContext ctx;
    HumanPose2D pose = {0};
    float home_pitch, home_roll, home_gripper;

    pose.shoulder_l = point(501.0f, 261.0f);
    pose.shoulder_r = point(781.0f, 261.0f);
    pose.elbow = point(891.0f, 331.0f);
    pose.wrist = point(1001.0f, 391.0f);
    pose.valid = 1U;
    servo_pwm_driver_mock_reset();
    servo_hal_init();
    assert(agent_pipeline_init(&ctx) == 0);
    home_pitch = ctx.output.wrist_pitch_deg;
    home_roll = ctx.output.wrist_roll_deg;
    home_gripper = ctx.output.gripper_norm;

    for (unsigned i = 1U; i < 60U; ++i) {
        pose.frame_id = i;
        assert(agent1_run(&ctx, &pose, 0.05f) == 0);
    }
    pose.frame_id = 60U;
    assert(agent1_run(&ctx, &pose, 0.05f) == 1);
    assert(ctx.target.valid && !ctx.target.wrist_valid);
    assert(agent2_run(&ctx) == 1);
    assert(ctx.command.wrist_pitch_deg == home_pitch);
    assert(ctx.command.wrist_roll_deg == home_roll);
    assert(ctx.command.gripper_norm == home_gripper);
    assert(ctx.command.elbow_roll_deg != ctx.output.elbow_roll_deg ||
           ctx.command.elbow_pitch_deg != ctx.output.elbow_pitch_deg);
}

int main(void)
{
    AgentPipelineContext ctx;
    HumanForearmTarget target = {
        .elbow_roll_deg = 30.0f, .elbow_pitch_deg = -60.0f,
        .wrist_pitch_deg = 150.0f, .wrist_roll_deg = -150.0f,
        .gripper_norm = 1.0f, .valid = 1U
    };
    float home_pitch, home_roll, home_gripper;

    servo_pwm_driver_mock_reset();
    servo_hal_init();
    assert(agent_pipeline_init(&ctx) == 0);
    home_pitch = ctx.output.wrist_pitch_deg;
    home_roll = ctx.output.wrist_roll_deg;
    home_gripper = ctx.output.gripper_norm;

    /* No hand evidence: only measured elbow targets may move. */
    ctx.target = target;
    ctx.target_ready = 1U;
    assert(agent2_run(&ctx) == 1);
    assert(!ctx.unwrap.wrist_has_reference);
    assert(ctx.command.wrist_pitch_deg == home_pitch);
    assert(ctx.command.wrist_roll_deg == home_roll);
    assert(ctx.command.gripper_norm == home_gripper);
    assert(ctx.command.elbow_roll_deg != ctx.output.elbow_roll_deg);
    for (unsigned i = 0U; i < 10U; ++i) assert(agent2_tick(&ctx) == 1);
    assert(ctx.output.elbow_roll_deg != 90.0f);
    assert(ctx.output.wrist_pitch_deg == home_pitch);
    assert(ctx.output.wrist_roll_deg == home_roll);
    assert(ctx.output.gripper_norm == home_gripper);

    /* The first reconstructed hand target may now move the wrists. */
    target.wrist_valid = 1U;
    target.hand_fresh = 0U; /* a 60-frame estimate need not be fresh today */
    target.wrist_pitch_deg = 20.0f;
    target.wrist_roll_deg = -30.0f;
    ctx.target = target;
    ctx.target_ready = 1U;
    assert(agent2_run(&ctx) == 1);
    assert(ctx.unwrap.wrist_has_reference);
    assert(ctx.command.wrist_pitch_deg != home_pitch);
    assert(ctx.command.wrist_roll_deg != home_roll);
    assert(isfinite(ctx.command.elbow_roll_deg));
    test_no_fingers_through_pipeline();
    puts("Major-only elbow with held wrist/gripper: PASS");
    return 0;
}
