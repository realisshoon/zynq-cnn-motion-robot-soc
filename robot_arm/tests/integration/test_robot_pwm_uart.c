#include <assert.h>
#include <stdio.h>
#include <string.h>

#define main unused_integration_main
#include "../../src/integration/main_integration.c"
#undef main

static int apply_ok = 1, enable_ok = 1, disable_ok = 1;
static unsigned applies, enables, disables, reported_enabled;
static const char *reported_result;
static int async_enabled;

int platform_init(void) { return -1; }
int platform_tick_due(void) { return 0; }
void platform_uart_service(void) { }
void input_pose_init(void) { }
int input_pose_ready(void) { return 0; }
int input_pose_take(HumanPose2D *pose, float *dt_sec)
{
    (void)pose; (void)dt_sec;
    return 0;
}
int input_pose_cnn_take_stereo(HumanPose2D *pose, HumanPose3D *measured, float *dt_sec)
{
    (void)pose; (void)measured; (void)dt_sec;
    return 0;
}
int stereo_board_init(void) { return 0; }
int stereo_board_set_async_test(int enabled) { async_enabled = !!enabled; return 1; }
int stereo_board_async_test_enabled(void) { return async_enabled; }
void stereo_board_service(void) { }
int cnn_app_init(void) { return 0; }
void cnn_app_service(void) { }

void servo_hal_init(void) { }
int servo_hal_apply(const ServoPwmCommand *command)
{
    assert(command != NULL);
    applies++;
    return apply_ok;
}
int servo_hal_enable(void) { enables++; return enable_ok; }
int servo_hal_disable(void) { disables++; return disable_ok; }
uint32_t platform_tick_overrun_count(void) { return 0U; }
void cnn_app_report_pwm_result(unsigned enabled, const char *result, const char *mode)
{
    assert(mode != NULL);
    reported_enabled = enabled;
    reported_result = result;
}
void cnn_app_report_async_result(unsigned enabled, unsigned pwm_enabled,
                                const char *result, const char *mode)
{
    assert(mode != NULL);
    (void)enabled;
    (void)pwm_enabled;
    reported_result = result;
}
void cnn_app_report_control_result(CnnAppEvent event, int accepted,
    const char *mode, const char *reason, unsigned long record_count,
    unsigned long replay_count, const char *record_source, const char *replay_source)
{
    (void)event; (void)accepted; (void)mode; (void)reason;
    (void)record_count; (void)replay_count; (void)record_source; (void)replay_source;
}

int main(void)
{
    AgentPipelineContext pipeline;
    MotionRecordReplay controller;
    assert(agent_pipeline_init_mode(&pipeline, 0) == 0);
    motion_record_replay_init(&controller);
    handle_record_or_play_event(&controller, &pipeline, CNN_APP_EVENT_PWM_STATUS);
    assert(reported_enabled == 0 && strcmp(reported_result, "status") == 0);
    assert(applies == 0 && enables == 0 && disables == 1);
    handle_record_or_play_event(&controller, &pipeline, CNN_APP_EVENT_ASYNC_ENABLE);
    assert(!async_enabled && applies == 0);
    handle_record_or_play_event(&controller, &pipeline, CNN_APP_EVENT_PWM_ENABLE);
#ifdef ROBOT_STEREO_LEFT
    assert(!pipeline.output_enabled);
    assert(strcmp(reported_result, "rejected_left_sender_only") == 0);
    assert(applies == 0 && enables == 0);
#else
    assert(pipeline.output_enabled && reported_enabled == 1);
    assert(applies == 1 && enables == 1);
    handle_record_or_play_event(&controller, &pipeline, CNN_APP_EVENT_ASYNC_ENABLE);
    assert(async_enabled && applies == 1 && enables == 1);
    handle_record_or_play_event(&controller, &pipeline, CNN_APP_EVENT_ASYNC_STATUS);
    assert(async_enabled);
    handle_record_or_play_event(&controller, &pipeline, CNN_APP_EVENT_ASYNC_DISABLE);
    assert(!async_enabled && pipeline.output_enabled);
    handle_record_or_play_event(&controller, &pipeline, CNN_APP_EVENT_ASYNC_ENABLE);
    assert(async_enabled);
    handle_record_or_play_event(&controller, &pipeline, CNN_APP_EVENT_PWM_ENABLE);
    assert(applies == 1 && enables == 1);
    handle_record_or_play_event(&controller, &pipeline, CNN_APP_EVENT_PWM_DISABLE);
    assert(!pipeline.output_enabled && disables == 2);
    assert(!async_enabled);
    controller.mode = MOTION_RR_RECORDING;
    handle_record_or_play_event(&controller, &pipeline, CNN_APP_EVENT_PWM_ENABLE);
    assert(!pipeline.output_enabled && applies == 1);
    controller.mode = MOTION_RR_ALIGNING;
    handle_record_or_play_event(&controller, &pipeline, CNN_APP_EVENT_PWM_ENABLE);
    assert(!pipeline.output_enabled && applies == 1);
    controller.mode = MOTION_RR_PLAYING;
    handle_record_or_play_event(&controller, &pipeline, CNN_APP_EVENT_PWM_ENABLE);
    assert(!pipeline.output_enabled && applies == 1);
    controller.mode = MOTION_RR_HOLDING;
    handle_record_or_play_event(&controller, &pipeline, CNN_APP_EVENT_PWM_ENABLE);
    assert(!pipeline.output_enabled && applies == 1);
    controller.mode = MOTION_RR_LIVE;
    controller.gripper_catchup = 1U;
    handle_record_or_play_event(&controller, &pipeline, CNN_APP_EVENT_PWM_ENABLE);
    assert(!pipeline.output_enabled && applies == 1);
    controller.gripper_catchup = 0U;
#endif
    apply_ok = 0;
    assert(!agent_pipeline_set_output_enabled(&pipeline, 1));
    assert(!pipeline.output_enabled);
    apply_ok = 1;
    enable_ok = 0;
    assert(!agent_pipeline_set_output_enabled(&pipeline, 1));
    assert(!pipeline.output_enabled);
    enable_ok = 1;
    assert(agent_pipeline_set_output_enabled(&pipeline, 1));
    disable_ok = 0;
    assert(!agent_pipeline_set_output_enabled(&pipeline, 0));
    assert(pipeline.output_enabled);
    disable_ok = 1;
    controller.mode = MOTION_RR_PLAYING;
    handle_record_or_play_event(&controller, &pipeline, CNN_APP_EVENT_PWM_DISABLE);
    assert(!pipeline.output_enabled);
    assert(agent_pipeline_set_output_enabled(&pipeline, 1));
    assert(agent_pipeline_set_output_enabled(&pipeline, 0));
    pipeline.output.elbow_roll_deg += 1.0f;
    assert(!agent_pipeline_set_output_enabled(&pipeline, 1));
    pipeline.output = pipeline.applied_command;
    pipeline.agent3_command.gripper_norm += 0.1f;
    assert(!agent_pipeline_set_output_enabled(&pipeline, 1));
    pipeline.agent3_command = pipeline.applied_command;
    pipeline.motion.axes[0].q += 1.0;
    pipeline.motion.axes[0].target += 1.0;
    assert(!agent_pipeline_set_output_enabled(&pipeline, 1));
    puts("test_robot_pwm_uart: PASS");
    return 0;
}
