#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "robot_calibration/forearm_calibration_config.h"

#define main unused_integration_main
#include "../../src/integration/main_integration.c"
#undef main

static int apply_ok = 1, enable_ok = 1, disable_ok = 1;
static unsigned applies, enables, disables, reported_enabled;
static const char *reported_result;
static int control_accepted;
static const char *control_reason;
static int async_enabled;
static int hardware_enabled;
static int enable_failure_after_write;
static ServoPwmCommand last_written_pwm;
static char hal_events[64];
static unsigned hal_event_count;

static void remember_hal_event(char event)
{
    if (hal_event_count < sizeof(hal_events)) hal_events[hal_event_count] = event;
    ++hal_event_count;
}

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

#ifdef ROBOT_TRACE
void trace_init(void) { }
void trace_mark(void) { }
void trace_input(const HumanPose2D *pose) { (void)pose; }
void trace_a1(const AgentPipelineContext *pipeline) { (void)pipeline; }
void trace_a2(const AgentPipelineContext *pipeline) { (void)pipeline; }
void trace_tick(const AgentPipelineContext *pipeline) { (void)pipeline; }
void trace_poll(const AgentPipelineContext *pipeline) { (void)pipeline; }
#endif

void servo_hal_init(void) { }
int servo_hal_apply(const ServoPwmCommand *command)
{
    assert(command != NULL);
    applies++;
    remember_hal_event('W');
    if (apply_ok) last_written_pwm = *command;
    return apply_ok;
}
int servo_hal_enable(void)
{
    enables++;
    remember_hal_event('E');
    if (enable_ok || enable_failure_after_write) hardware_enabled = 1;
    return enable_ok;
}
int servo_hal_disable(void)
{
    disables++;
    remember_hal_event('X');
    if (disable_ok) hardware_enabled = 0;
    return disable_ok;
}
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
    (void)event; (void)mode;
    control_accepted = accepted;
    control_reason = reason;
    (void)record_count; (void)replay_count; (void)record_source; (void)replay_source;
}

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
    assert(pipeline->motion.blocked_flags == FOREARM_SAFETY_CHECK_OK);
    assert(pipeline->motion.gripper == reference->gripper_norm);
    assert(!pipeline->target_ready);
    for (axis = 0U; axis < FOREARM_MOTION_JOINT_COUNT; ++axis) {
        assert(pipeline->motion.axes[axis].q == positions[axis]);
        assert(pipeline->motion.axes[axis].target == positions[axis]);
        assert(pipeline->motion.axes[axis].v == 0.0);
    }
}

static void assert_transaction_preserved(const AgentPipelineContext *pipeline,
                                         const AgentPipelineContext *before)
{
    AgentPipelineContext expected = *before;
    expected.servo_errors = pipeline->servo_errors;
    expected.output_result = pipeline->output_result;
    expected.output_faulted = pipeline->output_faulted;
    assert(memcmp(pipeline, &expected, sizeof(expected)) == 0);
}

static void reset_hal(void)
{
    apply_ok = enable_ok = disable_ok = 1;
    applies = enables = disables = 0U;
    hardware_enabled = 0;
    enable_failure_after_write = 0;
    hal_event_count = 0U;
    memset(hal_events, 0, sizeof(hal_events));
    memset(&last_written_pwm, 0, sizeof(last_written_pwm));
}

static HumanForearmTarget moving_target(void)
{
    const HumanForearmTarget target = {
        .elbow_roll_deg = -20.0f, .elbow_pitch_deg = 40.0f,
        .wrist_pitch_deg = 10.0f, .wrist_roll_deg = -7.0f,
        .gripper_norm = 0.4f, .valid = 1U,
        .wrist_valid = 1U, .gripper_valid = 1U
    };
    return target;
}

static void inject_legacy_drift(AgentPipelineContext *pipeline)
{
    pipeline->motion.axes[0].q += 5.0;
    pipeline->motion.axes[0].target += 25.0;
    pipeline->motion.axes[0].v = 12.0;
    pipeline->motion.gripper = 0.9f;
    pipeline->motion.held = 1;
    pipeline->output.elbow_roll_deg += 5.0f;
    pipeline->output.gripper_norm = 0.9f;
    pipeline->command.elbow_roll_deg += 25.0f;
    pipeline->command.gripper_norm = 0.9f;
    pipeline->agent3_command = pipeline->output;
    pipeline->agent3_command_valid = 0U;
    pipeline->target = moving_target();
    pipeline->target_ready = 1U;
}

static void test_first_enable_and_legacy_recovery(void)
{
    AgentPipelineContext pipeline;
    AgentPipelineContext before;
    ForearmJointCommand home;
    reset_hal();
    assert(agent_pipeline_init_mode(&pipeline, 0) == 0);
    home = pipeline.output;
    assert(!pipeline.applied_command_valid && !hardware_enabled);
    assert(!pipeline.output_parked && !pipeline.output_faulted);
    inject_legacy_drift(&pipeline);
    assert(agent_pipeline_set_output_enabled(&pipeline, 1));
    assert_anchored(&pipeline, &home);
    assert_same_command(&pipeline.applied_command, &home);
    assert(pipeline.servo_writes == 1U && hardware_enabled);
    assert(!pipeline.output_parked && !pipeline.output_faulted);
    assert(hal_event_count == 3U && memcmp(hal_events, "XWE", 3U) == 0);
    assert(strcmp(agent_pipeline_output_result_name(&pipeline),
                  "enabled_physical_pose_not_verified") == 0);
    assert(agent_pipeline_set_output_enabled(&pipeline, 0));
    inject_legacy_drift(&pipeline);
    assert(agent_pipeline_set_output_enabled(&pipeline, 1));
    assert_anchored(&pipeline, &home);
    assert(pipeline.servo_writes == 2U);
    pipeline.target = moving_target();
    pipeline.target_ready = 1U;
    assert(agent2_run(&pipeline));
    assert(agent2_tick(&pipeline));
    before = pipeline;
    assert(agent_pipeline_set_output_enabled(&pipeline, 1));
    assert_transaction_preserved(&pipeline, &before);
    assert(enables == 2U && applies == 2U);
    puts("PWM first E / legacy drift / already-enabled no-op: PASS");
}

static void test_disable_mid_ramp_and_repeat(void)
{
    AgentPipelineContext pipeline;
    ForearmJointCommand reference;
    ForearmMotionState frozen;
    ServoPwmCommand written;
    unsigned step;
    unsigned writes_before;
    uint32_t retargets_before;
    reset_hal();
    assert(agent_pipeline_init_mode(&pipeline, 0) == 0);
    assert(agent_pipeline_set_output_enabled(&pipeline, 1));
    pipeline.target = moving_target();
    pipeline.target_ready = 1U;
    assert(agent2_run(&pipeline));
    for (step = 0U; step < 8U; ++step) {
        assert(agent2_tick(&pipeline));
        assert(agent3_run(&pipeline));
    }
    assert(pipeline.motion.axes[0].v != 0.0);
    assert(pipeline.output.elbow_roll_deg != pipeline.command.elbow_roll_deg);
    reference = pipeline.applied_command;
    written = pipeline.pwm;
    assert(agent2_tick(&pipeline));
    pipeline.motion.gripper = 0.8f;
    pipeline.output.gripper_norm = 0.8f;
    apply_ok = 0;
    assert(!agent3_run(&pipeline));
    apply_ok = 1;
    assert_same_command(&pipeline.applied_command, &reference);
    assert(memcmp(&pipeline.pwm, &written, sizeof(written)) == 0);
    assert(agent_pipeline_set_output_enabled(&pipeline, 0));
    assert(!pipeline.output_enabled && !hardware_enabled);
    assert_anchored(&pipeline, &reference);
    assert_same_command(&pipeline.applied_command, &reference);
    frozen = pipeline.motion;
    writes_before = applies;
    retargets_before = pipeline.retargets;
    for (step = 0U; step < 1500U; ++step) {
        pipeline.target = moving_target();
        pipeline.target_ready = 1U;
        assert(!agent2_run(&pipeline));
        assert(agent2_tick(&pipeline));
        assert(agent3_run(&pipeline));
        assert_same_command(&pipeline.output, &reference);
        assert_same_command(&pipeline.applied_command, &reference);
        assert(memcmp(&pipeline.motion, &frozen, sizeof(frozen)) == 0);
    }
    assert(!pipeline.output_enabled && !hardware_enabled);
    assert(applies == writes_before && pipeline.retargets == retargets_before);
    assert(agent_pipeline_set_output_enabled(&pipeline, 1));
    assert_anchored(&pipeline, &reference);
    assert(memcmp(&last_written_pwm, &written, sizeof(written)) == 0);
    for (step = 0U; step < 10U; ++step) {
        assert(agent_pipeline_set_output_enabled(&pipeline, 0));
        assert_anchored(&pipeline, &reference);
        assert(agent_pipeline_set_output_enabled(&pipeline, 1));
        assert_anchored(&pipeline, &reference);
    }
    pipeline.target = moving_target();
    pipeline.target_ready = 1U;
    assert(agent2_run(&pipeline));
    assert(agent2_tick(&pipeline));
    assert(pipeline.output.elbow_roll_deg > reference.elbow_roll_deg);
    assert(pipeline.output.elbow_roll_deg - reference.elbow_roll_deg <=
           forearm_calibration_config.elbow_roll.max_delta_deg);
    assert(agent3_run(&pipeline));
    puts("PWM X mid-ramp / last successful write / OFF freeze / repeated X-E: PASS");
}

static void test_hal_failure_rollback(void)
{
    AgentPipelineContext pipeline;
    AgentPipelineContext before;
    uint32_t errors_before;
    reset_hal();
    assert(agent_pipeline_init_mode(&pipeline, 0) == 0);
    inject_legacy_drift(&pipeline);
    before = pipeline;
    apply_ok = 0;
    assert(!agent_pipeline_set_output_enabled(&pipeline, 1));
    assert_transaction_preserved(&pipeline, &before);
    assert(!pipeline.output_enabled && !hardware_enabled && enables == 0U);
    assert(pipeline.servo_errors == 1U);
    assert(pipeline.output_result == AGENT_OUTPUT_RESULT_HAL_APPLY_FAILED);
    assert(hal_event_count == 3U && memcmp(hal_events, "XWX", 3U) == 0);
    apply_ok = 1;
    enable_ok = 0;
    before = pipeline;
    assert(!agent_pipeline_set_output_enabled(&pipeline, 1));
    assert_transaction_preserved(&pipeline, &before);
    assert(!pipeline.output_enabled && !hardware_enabled);
    assert(!pipeline.applied_command_valid && pipeline.servo_writes == 0U);
    assert(pipeline.servo_errors == 2U);
    assert(pipeline.output_result == AGENT_OUTPUT_RESULT_HAL_ENABLE_FAILED);
    assert(hal_event_count == 6U && memcmp(hal_events + 3U, "WEX", 3U) == 0);
    disable_ok = 0;
    enable_failure_after_write = 1;
    before = pipeline;
    assert(!agent_pipeline_set_output_enabled(&pipeline, 1));
    assert_transaction_preserved(&pipeline, &before);
    assert(pipeline.servo_errors == 4U);
    assert(pipeline.output_result == AGENT_OUTPUT_RESULT_HAL_RECOVERY_DISABLE_FAILED);
    assert(!pipeline.output_enabled && pipeline.output_faulted && hardware_enabled);
    assert(strcmp(agent_pipeline_output_result_name(&pipeline),
                  "HAL_recovery_disable_failed_hardware_state_unverified") == 0);
    enable_ok = disable_ok = 1;
    before = pipeline;
    assert(!agent_pipeline_set_output_enabled(&pipeline, 1));
    assert_transaction_preserved(&pipeline, &before);
    assert(agent_pipeline_set_output_enabled(&pipeline, 0));
    assert(!pipeline.output_faulted && !hardware_enabled);
    assert(agent_pipeline_set_output_enabled(&pipeline, 1));
    pipeline.target = moving_target();
    pipeline.target_ready = 1U;
    assert(agent2_run(&pipeline));
    assert(agent2_tick(&pipeline));
    before = pipeline;
    errors_before = pipeline.servo_errors;
    disable_ok = 0;
    assert(!agent_pipeline_set_output_enabled(&pipeline, 0));
    assert_transaction_preserved(&pipeline, &before);
    assert(pipeline.output_enabled && hardware_enabled);
    assert(pipeline.servo_errors == errors_before + 1U);
    assert(pipeline.output_result == AGENT_OUTPUT_RESULT_HAL_DISABLE_FAILED);
    disable_ok = 1;
    assert(agent_pipeline_set_output_enabled(&pipeline, 0));
    assert(!hardware_enabled);
    assert_anchored(&pipeline, &pipeline.applied_command);
    puts("PWM HAL write/enable/disable/cleanup failures are transactional: PASS");
}

static void test_reject_reference(ForearmJointCommand reference,
                                  AgentPipelineOutputResult expected_result)
{
    AgentPipelineContext pipeline;
    AgentPipelineContext before;
    unsigned events_before;
    reset_hal();
    assert(agent_pipeline_init_mode(&pipeline, 0) == 0);
    pipeline.applied_command = reference;
    pipeline.applied_command_valid = 1U;
    inject_legacy_drift(&pipeline);
    before = pipeline;
    events_before = hal_event_count;
    assert(!agent_pipeline_set_output_enabled(&pipeline, 1));
    assert_transaction_preserved(&pipeline, &before);
    assert(pipeline.output_result == expected_result);
    assert(!pipeline.output_enabled && !hardware_enabled);
    assert(hal_event_count == events_before && pipeline.servo_errors == 0U);
}

static void test_unsafe_reference_rejected(void)
{
    AgentPipelineContext pipeline;
    AgentPipelineContext before;
    ForearmJointCommand reference = {90.0f, 70.0f, 100.0f, 90.0f, 0.05f, 1U};
    const ForearmJointCommand home = reference;
    unsigned axis;
    reference.valid = 0U;
    test_reject_reference(reference, AGENT_OUTPUT_RESULT_REFERENCE_INVALID);
    for (axis = 0U; axis < 5U; ++axis) {
        float *value;
        reference = home;
        switch (axis) {
            case 0U: value = &reference.elbow_roll_deg; break;
            case 1U: value = &reference.elbow_pitch_deg; break;
            case 2U: value = &reference.wrist_pitch_deg; break;
            case 3U: value = &reference.wrist_roll_deg; break;
            default: value = &reference.gripper_norm; break;
        }
        *value = NAN;
        test_reject_reference(reference, AGENT_OUTPUT_RESULT_REFERENCE_INVALID);
        *value = INFINITY;
        test_reject_reference(reference, AGENT_OUTPUT_RESULT_REFERENCE_INVALID);
    }
    reference = home;
    reference.elbow_roll_deg = 161.0f;
    test_reject_reference(reference, AGENT_OUTPUT_RESULT_REFERENCE_LIMITS);
    reference = home;
    reference.gripper_norm = 1.01f;
    test_reject_reference(reference, AGENT_OUTPUT_RESULT_REFERENCE_LIMITS);
    reference.gripper_norm = -0.01f;
    test_reject_reference(reference, AGENT_OUTPUT_RESULT_REFERENCE_LIMITS);
    reference = home;
    reference.wrist_pitch_deg = 160.0f;
    assert(!forearm_safety_check_apply(&reference, NULL));
    test_reject_reference(reference, AGENT_OUTPUT_RESULT_REFERENCE_UNSAFE);
    reset_hal();
    assert(agent_pipeline_init_mode(&pipeline, 0) == 0);
    assert(agent_pipeline_set_output_enabled(&pipeline, 1));
    pipeline.applied_command = reference;
    before = pipeline;
    assert(!agent_pipeline_set_output_enabled(&pipeline, 0));
    before.output_enabled = 0U;
    before.output_parked = 1U;
    assert_transaction_preserved(&pipeline, &before);
    assert(!hardware_enabled);
    assert(pipeline.output_result == AGENT_OUTPUT_RESULT_REFERENCE_UNSAFE);
    assert(!agent_pipeline_set_output_enabled(&pipeline, 1));
    assert(!hardware_enabled && applies == 1U && enables == 1U);
    assert(strcmp(agent_pipeline_output_result_name(NULL), "invalid_pipeline") == 0);
    puts("PWM invalid / NaN / Inf / range / gripper / FK references rejected: PASS");
}

static void test_enabled_input_loss_completes(void)
{
    AgentPipelineContext pipeline;
    MotionRecordReplay controller;
    ForearmJointCommand goal;
    unsigned step;
    unsigned disables_before;
    reset_hal();
    assert(agent_pipeline_init_mode(&pipeline, 0) == 0);
    assert(agent_pipeline_set_output_enabled(&pipeline, 1));
    motion_record_replay_init(&controller);
    pipeline.target = moving_target();
    pipeline.target_ready = 1U;
    assert(agent2_run(&pipeline));
    goal = pipeline.command;
    pipeline.target_ready = 0U;
    disables_before = disables;
    for (step = 0U; step < 1500U; ++step) {
        const double previous = pipeline.motion.axes[0].q;
        const double previous_velocity = pipeline.motion.axes[0].v;
        assert(motion_record_replay_control_tick(&controller, &pipeline, 0U));
        assert(fabs(pipeline.motion.axes[0].q - previous) <=
               forearm_calibration_config.elbow_roll.max_delta_deg + 0.0001);
        assert(fabs(pipeline.motion.axes[0].v - previous_velocity) <=
               forearm_calibration_config.amax_deg_s2[0] * 0.020 + 0.0001);
        assert(pipeline.output_enabled && hardware_enabled && !pipeline.motion.held);
    }
    assert(disables == disables_before);
    assert_same_command(&pipeline.output, &goal);
    assert_same_command(&pipeline.applied_command, &goal);
    assert(motion_record_replay_mode(&controller) == MOTION_RR_LIVE);
    puts("PWM enabled input loss finishes approved goal and holds, no timeout OFF: PASS");
}

static void test_parked_record_play_commands_leave_resume_available(void)
{
    AgentPipelineContext pipeline;
    MotionRecordReplay controller;
    MotionSample replay = {90.0f, 90.0f, 90.0f, 90.0f, 0.5f};
    unsigned attempt;

    reset_hal();
    assert(agent_pipeline_init_mode(&pipeline, 0) == 0);
    motion_record_replay_init(&controller);
    assert(motion_record_replay_configure_align(&controller, 0.01f, 500U));
    assert(motion_record_replay_load_replay(&controller, &replay, 1U));
    handle_record_or_play_event(&controller, &pipeline, CNN_APP_EVENT_PWM_DISABLE);
    assert(!pipeline.output_enabled && pipeline.output_parked);
    for (attempt = 0U; attempt < 2U; ++attempt) {
        handle_record_or_play_event(&controller, &pipeline, CNN_APP_EVENT_RECORD_TOGGLE);
        assert(!control_accepted && strcmp(control_reason, "BUSY") == 0);
        assert(controller.mode == MOTION_RR_LIVE && !controller.gripper_catchup);
        assert(motion_record_replay_control_tick(&controller, &pipeline, 0U));
        assert(controller.record_count == 0U && controller.replay_count == 1U);
    }
    handle_record_or_play_event(&controller, &pipeline, CNN_APP_EVENT_PLAY_TOGGLE);
    assert(!control_accepted && strcmp(control_reason, "BUSY") == 0);
    assert(controller.mode == MOTION_RR_LIVE && !controller.gripper_catchup);
    handle_record_or_play_event(&controller, &pipeline, CNN_APP_EVENT_PWM_ENABLE);
#ifdef ROBOT_STEREO_RIGHT
    assert(pipeline.output_enabled && !controller.gripper_catchup);
    assert(controller.mode == MOTION_RR_LIVE);
#else
    assert(!pipeline.output_enabled);
    assert(strcmp(reported_result, "rejected_left_sender_only") == 0);
#endif
    puts("PWM X -> R -> R / P cannot latch recording or prevent E: PASS");
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
    assert(agent_pipeline_set_output_enabled(&pipeline, 1));
    assert_anchored(&pipeline, &pipeline.applied_command);
    assert(agent_pipeline_set_output_enabled(&pipeline, 0));
    pipeline.agent3_command.gripper_norm += 0.1f;
    assert(agent_pipeline_set_output_enabled(&pipeline, 1));
    assert_anchored(&pipeline, &pipeline.applied_command);
    assert(agent_pipeline_set_output_enabled(&pipeline, 0));
    pipeline.motion.axes[0].q += 1.0;
    pipeline.motion.axes[0].target += 1.0;
    assert(agent_pipeline_set_output_enabled(&pipeline, 1));
    assert_anchored(&pipeline, &pipeline.applied_command);
    test_first_enable_and_legacy_recovery();
    test_disable_mid_ramp_and_repeat();
    test_hal_failure_rollback();
    test_unsafe_reference_rejected();
    test_enabled_input_loss_completes();
    test_parked_record_play_commands_leave_resume_available();
    puts("test_robot_pwm_uart: PASS");
    return 0;
}
