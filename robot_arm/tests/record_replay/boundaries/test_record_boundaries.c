#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "harness.h"
#include "ff.h"
#include "cnn_app_event.h"
#include "drivers/servo_pwm_driver.h"
#include "output_controller/servo_hal.h"
#include "output_controller/servo_control.h"
#include "record_replay/motion_library.h"
#include "record_replay/motion_sd.h"
#include "robot_calibration/forearm_calibration_config.h"
#include "robot_calibration/forearm_safety_check.h"

#ifdef ROBOT_STEREO_LEFT
#define ROLE "LEFT"
#define SIDE "l"
#define OWN_BANK SERVO_PWM_BANK_LEFT
#else
#define ROLE "RIGHT"
#define SIDE "r"
#define OWN_BANK SERVO_PWM_BANK_RIGHT
#endif

static AgentPipelineContext pipeline;
static MotionRecordReplay controller;
static char transcript[262144];
static size_t transcript_length;
static const char *case_name;
static unsigned checks, failures, cases, audited_ticks;
static jmp_buf loop_exit;
static unsigned loop_iteration, loop_a2_calls, loop_gripper_calls;
static unsigned loop_gated_a2, loop_gated_gripper;
static int loop_active, loop_resume, loop_resumed, loop_fill, loop_gated;

static int check_condition(int passed, const char *expression, unsigned line)
{
    ++checks;
    if (!passed) {
        ++failures;
        printf("FAIL %s %s:%u %s\n", ROLE, case_name, line, expression);
    }
    return passed;
}

#define CHECK(condition) check_condition((condition) != 0, #condition, (unsigned)__LINE__)
#define REQUIRE(condition) do { if (!CHECK(condition)) return; } while (0)

int regression_printf(const char *format, ...)
{
    va_list arguments;
    int length;
    size_t remaining = sizeof(transcript) - transcript_length;
    va_start(arguments, format);
    length = vsnprintf(transcript + transcript_length, remaining, format, arguments);
    va_end(arguments);
    if (length > 0 && (size_t)length < remaining) {
        fputs(transcript + transcript_length, stdout);
        transcript_length += (size_t)length;
    } else CHECK(0);
    return length;
}

static void clear_transcript(void)
{
    transcript[0] = '\0';
    transcript_length = 0U;
}

static const char *last_state(void)
{
    const char *found = transcript, *latest = NULL;
    while ((found = strstr(found, "[REC_STATE] ")) != NULL) {
        latest = found;
        ++found;
    }
    return latest;
}

static int state_token(const char *key, const char *expected)
{
    char token[96];
    const char *state = last_state(), *found, *end;
    if (state == NULL) return 0;
    (void)snprintf(token, sizeof(token), " %s=%s", key, expected);
    found = strstr(state, token);
    end = strchr(state, '\n');
    if (found == NULL || (end != NULL && found > end)) return 0;
    found += strlen(token);
    return *found == ' ' || *found == '\r' || *found == '\n' || *found == '\0';
}

static unsigned state_number(const char *key)
{
    char token[40];
    const char *state = last_state(), *found;
    unsigned value = UINT32_MAX;
    if (state == NULL) return value;
    (void)snprintf(token, sizeof(token), " %s=", key);
    found = strstr(state, token);
    if (found != NULL) (void)sscanf(found + strlen(token), "%u", &value);
    return value;
}

static int follow_enabled(void)
{
#ifdef ROBOT_STEREO_LEFT
    return mock.remote_follow;
#else
    return mock.local_follow;
#endif
}

static MotionSample sample_from_command(const ForearmJointCommand *command)
{
    MotionSample sample = { command->elbow_roll_deg, command->elbow_pitch_deg,
        command->wrist_pitch_deg, command->wrist_roll_deg, command->gripper_norm };
    return sample;
}

static ForearmJointCommand command_from_sample(const MotionSample *sample)
{
    ForearmJointCommand command = { sample->elbow_roll_deg, sample->elbow_pitch_deg,
        sample->wrist_pitch_deg, sample->wrist_roll_deg, sample->gripper_norm, 1U };
    return command;
}

static double sample_axis(const MotionSample *sample, unsigned axis)
{
    switch (axis) {
        case 0U: return sample->elbow_roll_deg;
        case 1U: return sample->elbow_pitch_deg;
        case 2U: return sample->wrist_pitch_deg;
        default: return sample->wrist_roll_deg;
    }
}

static int at_rest(const AgentPipelineContext *context)
{
    unsigned axis;
    for (axis = 0U; axis < FOREARM_MOTION_JOINT_COUNT; ++axis)
        if (!isfinite(context->motion.axes[axis].v) || fabs(context->motion.axes[axis].v) > .05 ||
            fabs(context->motion.axes[axis].target - context->motion.axes[axis].q) > .0002) return 0;
    return 1;
}

int harness_storage_write_allowed(void)
{
    return active_pipeline != NULL && active_controller != NULL &&
        (!active_pipeline->output_enabled ||
         (active_controller->mode == MOTION_RR_HOLDING && at_rest(active_pipeline)));
}

static void audit_registers(void)
{
    uint32_t index;
    for (index = 0U; index < servo_pwm_driver_mock_get_log_count(); ++index) {
        ServoPwmDriverMockWrite write;
        CHECK(servo_pwm_driver_mock_get_log(index, &write));
        CHECK(write.bank == OWN_BANK || (write.offset == 0x18U && write.value == 0U));
        CHECK(write.offset != 0x14U);
        if (write.offset < 0x14U) CHECK(write.value >= 400U && write.value <= 2600U);
    }
}

static int observed_pipeline_init(AgentPipelineContext *context, int enabled)
{
    active_pipeline = context;
    return agent_pipeline_init_mode(context, enabled);
}

static void observed_controller_init(MotionRecordReplay *record_replay)
{
    active_controller = record_replay;
    motion_record_replay_init(record_replay);
}

static int observed_agent2(AgentPipelineContext *context)
{
    ++loop_a2_calls;
    CHECK(motion_library_input_allowed());
    return agent2_run(context);
}

static int observed_gripper(AgentPipelineContext *context, const HumanPose2D *pose)
{
    ++loop_gripper_calls;
    CHECK(motion_library_input_allowed());
    return agent_gripper_run(context, pose);
}

static void observed_reset_gripper_latch(AgentPipelineContext *context)
{
    ++mock.reset_calls;
    agent_pipeline_reset_gripper_latch(context);
}

#define main regression_firmware_main
#define agent_pipeline_init_mode observed_pipeline_init
#define motion_record_replay_init observed_controller_init
#define agent2_run observed_agent2
#define agent_gripper_run observed_gripper
#define agent_pipeline_reset_gripper_latch observed_reset_gripper_latch
#include "firmware_under_test.h"
#undef main
#undef agent_pipeline_init_mode
#undef motion_record_replay_init
#undef agent2_run
#undef agent_gripper_run
#undef agent_pipeline_reset_gripper_latch

static void report(void)
{
    motion_library_report_state(active_controller, active_pipeline, 0);
}

static void event(char command)
{
    CnnAppEvent taken = CNN_APP_EVENT_NONE;
    CHECK(cnn_app_control_event_post_uart(command));
    CHECK(cnn_app_take_control_event(&taken));
    handle_record_or_play_event(active_controller, active_pipeline, taken);
    CHECK(!cnn_app_take_control_event(&taken));
    report();
    audit_registers();
    servo_pwm_driver_mock_clear_log();
}

static int initialize(void)
{
    clear_transcript();
    loop_active = 0;
    harness_mock_reset();
    fatfs_mock_reset();
    if (!CHECK(platform_init() == 0) || !CHECK(observed_pipeline_init(&pipeline, 0) == 0)) return 0;
    observed_controller_init(&controller);
    if (!CHECK(motion_record_replay_configure_align(&controller, .01f, 500U))) return 0;
    pipeline.gripper_independent = 1U;
    motion_record_replay_set_repeat(&controller, 1);
    motion_library_init();
    report();
    event('E');
    event('A');
    return CHECK(pipeline.output_enabled && follow_enabled());
}

static int foreground_tick(uint32_t overruns)
{
    uint32_t before = controller.record_count, index;
    int result;
    if (!motion_library_input_allowed() && !motion_library_record_preparing()) stop_local_follow();
    servo_pwm_driver_mock_clear_log();
    result = motion_record_replay_control_tick(&controller, &pipeline, overruns);
    ++audited_ticks;
    motion_library_tick(&controller, &pipeline);
    if (controller.record_count > before) {
        MotionSample expected = sample_from_command(&pipeline.applied_command);
        CHECK(pipeline.applied_command_valid && pipeline.output_enabled);
        CHECK(motion_record_replay_record_source(&controller) == MOTION_RR_APPLIED_HAL);
        for (index = before; index < controller.record_count; ++index) {
            MotionSample actual;
            CHECK(motion_record_replay_get_record_sample(&controller, index, &actual));
            CHECK(memcmp(&actual, &expected, sizeof(actual)) == 0);
        }
    }
    if (motion_library_service(&controller, &pipeline, overruns)) stop_local_follow();
    report();
    audit_registers();
    servo_pwm_driver_mock_clear_log();
    return result;
}

static void target_offset(float roll, float wrist_pitch, float wrist_roll, float gripper)
{
    ForearmJointCommand target = pipeline.output;
    target.elbow_roll_deg += roll;
    target.wrist_pitch_deg += wrist_pitch;
    target.wrist_roll_deg += wrist_roll;
    target.gripper_norm = gripper;
    CHECK(forearm_safety_check_apply(&target, NULL));
    forearm_calibration_set_target(&pipeline.motion, &target);
    pipeline.command = target;
    pipeline.command_valid = 1U;
    pipeline.gripper_desired = gripper;
}

static int begin_record(void)
{
    unsigned index;
    event('R');
    if (!CHECK(motion_library_record_preparing())) return 0;
    for (index = 0U; index < 700U && !motion_library_input_allowed(); ++index)
        if (!CHECK(foreground_tick(0U))) return 0;
    return CHECK(controller.mode == MOTION_RR_RECORDING && controller.record_count >= 2U &&
                 motion_library_input_allowed());
}

static int finish_name(void)
{
    unsigned index;
    event('R');
    CHECK(state_token("ui", "STOP_RECORD") && !motion_library_input_allowed());
    event('A');
    CHECK(strstr(mock.last_result, "rejected_") == mock.last_result);
    for (index = 0U; index < 700U && !state_token("ui", "NAME"); ++index)
        if (!CHECK(foreground_tick(0U))) return 0;
    return CHECK(state_token("ui", "NAME") && controller.mode == MOTION_RR_HOLDING);
}

static int make_clip(void)
{
    unsigned index;
    if (!begin_record()) return 0;
    target_offset(16.0f, 8.0f, -6.0f, .55f);
    for (index = 0U; index < 12U; ++index)
        if (!CHECK(foreground_tick(0U))) return 0;
    return finish_name();
}

static void send_frame(const char *text, int menu_active)
{
    while (*text) CHECK(motion_library_feed((uint8_t)*text++, menu_active));
    if (motion_library_service(&controller, &pipeline, 0U)) stop_local_follow();
    report();
}

static void send_name(const char *name)
{
    char frame[96];
    (void)snprintf(frame, sizeof(frame), "!REC,NAME,%s\r", name);
    send_frame(frame, 0);
}

static void service_save(void)
{
    unsigned index;
    for (index = 0U; index < MOTION_RECORD_REPLAY_MAX_SAMPLES / 16U + 8U && state_token("ui", "SAVE"); ++index) {
        (void)motion_library_service(&controller, &pipeline, 0U);
        report();
    }
    CHECK(!state_token("ui", "SAVE"));
    CHECK(unsafe_storage_writes == 0U && invalid_storage_paths == 0U);
}

static uint32_t crc_bytes(const unsigned char *bytes, unsigned length)
{
    uint32_t crc = UINT32_MAX;
    unsigned index, bit;
    for (index = 0U; index < length; ++index) {
        crc ^= bytes[index];
        for (bit = 0U; bit < 8U; ++bit) crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
    }
    return crc ^ UINT32_MAX;
}

static uint32_t clip_digest(void)
{
    MotionSample samples[MOTION_RECORD_REPLAY_MAX_SAMPLES];
    uint32_t index;
    for (index = 0U; index < controller.replay_count; ++index)
        CHECK(motion_record_replay_get_replay_sample(&controller, index, &samples[index]));
    return crc_bytes((const unsigned char *)samples, controller.replay_count * (unsigned)sizeof(MotionSample));
}

static void audit_clip(void)
{
    uint32_t index;
    MotionSample previous = {0}, first = {0};
    double previous_velocity[4] = {0};
    const JointCalibration *limits[4] = { &forearm_calibration_config.elbow_roll,
        &forearm_calibration_config.elbow_pitch, &forearm_calibration_config.wrist_pitch,
        &forearm_calibration_config.wrist_roll };
    CHECK(sizeof(MotionSample) == 20U);
    CHECK(controller.record_count == controller.replay_count && controller.replay_count >= 3U);
    CHECK(controller.replay_count <= MOTION_RECORD_REPLAY_MAX_SAMPLES);
    CHECK(motion_record_replay_validate_replay(&controller) == MOTION_RR_REASON_NONE);
    for (index = 0U; index < controller.replay_count; ++index) {
        MotionSample sample, recorded;
        ForearmJointCommand command;
        unsigned axis;
        CHECK(motion_record_replay_get_replay_sample(&controller, index, &sample));
        CHECK(motion_record_replay_get_record_sample(&controller, index, &recorded));
        CHECK(memcmp(&sample, &recorded, sizeof(sample)) == 0);
        command = command_from_sample(&sample);
        CHECK(forearm_safety_check_apply(&command, NULL));
        CHECK(isfinite(sample.gripper_norm) && sample.gripper_norm >= 0.0f && sample.gripper_norm <= 1.0f);
        if (!index) first = sample;
        if (index == 1U) CHECK(memcmp(&first, &sample, sizeof(sample)) == 0);
        for (axis = 0U; axis < 4U; ++axis) {
            double position = sample_axis(&sample, axis);
            double velocity = index ? (position - sample_axis(&previous, axis)) / .020 : 0.0;
            CHECK(isfinite(position) && position >= limits[axis]->min_deg && position <= limits[axis]->max_deg);
            CHECK(fabs(velocity) <= limits[axis]->max_delta_deg / .020 + .005);
            CHECK(fabs((velocity - previous_velocity[axis]) / .020) <= forearm_calibration_config.amax_deg_s2[axis] + .1);
            if (index + 1U == controller.replay_count)
                CHECK(fabs(velocity / .020) <= forearm_calibration_config.amax_deg_s2[axis] + .1);
            previous_velocity[axis] = velocity;
        }
        if (index) CHECK(fabsf(sample.gripper_norm - previous.gripper_norm) <= .0101f);
        previous = sample;
    }
    CHECK(at_rest(&pipeline));
}

static void seed_input_history(AgentPipelineContext *context)
{
    unsigned axis;
    context->gripper_latch = (AgentGripperLatch){
        .frame_id = 51U, .sample_time_us = 60000U, .confirmed_time_us = 40000U,
        .applied_time_us = 50000U, .candidate_started_us = 20000U,
        .candidate_active = 1U, .cancel_count = 1U, .have_frame = 1U,
        .source = 1U, .close_count = 3U, .latched = 1U, .closed_applied = 1U
    };
    memset(&context->gripper_motion_hold, 0, sizeof(context->gripper_motion_hold));
    context->gripper_motion_hold = (AgentGripperMotionHold){
        .speed = 12.0f, .low_since_us = 20000U, .frame_id = 51U, .fresh_us = 60000U,
        .samples = 2U, .speed_valid = 1U, .moving = 1U, .low_valid = 1U,
        .armed = 1U, .locked = 1U, .open_count = 1U, .have_frame = 1U, .source = 1U,
        .strong_open = { .started_us = 40000U, .fresh_us = 60000U, .frame_id = 51U,
            .pending = 1U, .have_frame = 1U, .source = 1U, .count = 1U }
    };
    for (axis = 0U; axis < 4U; ++axis) {
        context->gripper_motion_hold.angles[0][axis] = 90.0f;
        context->gripper_motion_hold.angles[1][axis] = 90.2f;
    }
    context->gripper_motion_hold.times[0] = 40000U;
    context->gripper_motion_hold.times[1] = 60000U;
    context->elbow_reentry = (AgentElbowReentry){
        .candidate = context->output.elbow_roll_deg, .time_us = 60000U,
        .candidate_time_us = 40000U, .frame_id = 51U, .seen = 1U,
        .outside = 1U, .pending = 1U, .ramping = 1U
    };
    context->elbow_return = (AgentElbowReturn){
        .goal = context->output.elbow_roll_deg, .time_us = 60000U,
        .valid = 1U, .held = 1U, .pending_epoch = 1U, .active = 1U
    };
    for (axis = 0U; axis < 2U; ++axis) {
        float goal = axis ? context->output.wrist_roll_deg : context->output.wrist_pitch_deg;
        context->wrist_return[axis] = (AgentWristReturn){
            .goal = goal, .candidate = goal, .time_us = 60000U, .candidate_us = 40000U,
            .candidate_frame = 50U, .last_fresh_us = 60000U, .last_fresh_frame = 51U,
            .candidate_epoch = 7U, .valid = 1U, .held = 1U, .outside = 1U,
            .active = 1U, .pending = 1U, .have_candidate = 1U, .have_fresh = 1U, .need_two = 1U
        };
    }
    context->wrist_observation_fresh = 1U;
    context->gripper_fresh = 1U;
    context->gripper_desired = .625f;
    context->gripper_distance_px = 16.0f;
}

static void check_rejected_async_preserves_state(const char *expected_ui)
{
    AgentPipelineContext original, before;
    MotionRecordReplay before_controller;
    unsigned resets_before = mock.reset_calls;
    unsigned local_calls_before = mock.set_local_calls, remote_calls_before = mock.set_remote_calls;
    int local_before = mock.local_follow, remote_before = mock.remote_follow;
    int foreign_before = mock.foreign_request;
    uint32_t digest = controller.replay_count ? clip_digest() : 0U;
    CHECK(state_token("ui", expected_ui) && !motion_library_input_allowed());
    memcpy(&original, &pipeline, sizeof(original));
    seed_input_history(&pipeline);
    memcpy(&before, &pipeline, sizeof(before));
    memcpy(&before_controller, &controller, sizeof(before_controller));
    printf("PHASE %s %s ui=%s mode=%s pwm=%u\n", ROLE, case_name, expected_ui,
        motion_record_replay_mode_name(controller.mode), (unsigned)pipeline.output_enabled);
    event('A');
    CHECK(strstr(mock.last_result, "rejected_") == mock.last_result);
    CHECK(mock.reset_calls == resets_before);
    CHECK(memcmp(&pipeline.gripper_latch, &before.gripper_latch, sizeof(before.gripper_latch)) == 0);
    CHECK(memcmp(&pipeline.gripper_motion_hold, &before.gripper_motion_hold, sizeof(before.gripper_motion_hold)) == 0);
    CHECK(memcmp(&pipeline.elbow_reentry, &before.elbow_reentry, sizeof(before.elbow_reentry)) == 0);
    CHECK(memcmp(&pipeline.elbow_return, &before.elbow_return, sizeof(before.elbow_return)) == 0);
    CHECK(memcmp(pipeline.wrist_return, before.wrist_return, sizeof(before.wrist_return)) == 0);
    CHECK(pipeline.wrist_observation_fresh == before.wrist_observation_fresh);
    CHECK(memcmp(&pipeline, &before, sizeof(before)) == 0);
    CHECK(memcmp(&controller, &before_controller, sizeof(before_controller)) == 0);
    CHECK(mock.local_follow == local_before && mock.remote_follow == remote_before);
    CHECK(mock.set_local_calls == local_calls_before && mock.set_remote_calls == remote_calls_before);
    CHECK(mock.foreign_request == foreign_before && state_token("ui", expected_ui));
    CHECK(!controller.replay_count || clip_digest() == digest);
    memcpy(&pipeline, &original, sizeof(original));
}

static void test_status_schema(void)
{
    unsigned writes_before;
    REQUIRE(initialize());
    CHECK(state_token("schema", "1") && state_token("side", SIDE));
    CHECK(state_token("ui", "IDLE") && state_token("mode", "LIVE") && state_token("pwm", "1"));
    CHECK(state_number("ram_samples") == 0U && state_number("entries") == 0U);
    CHECK(state_number("selected") == 0U && state_number("playing") == 0U);
    CHECK(state_number("delete") == 0U && state_number("unsaved") == 0U);
    clear_transcript();
    report();
    CHECK(last_state() == NULL);
    writes_before = writes;
    event('V');
    CHECK(last_state() != NULL && state_token("side", SIDE));
    CHECK(writes == writes_before);
}

static void test_prepare_hal_settle_and_seed(void)
{
    unsigned index;
    REQUIRE(initialize());
    target_offset(35.0f, 12.0f, -10.0f, .05f);
    for (index = 0U; index < 12U; ++index) REQUIRE(foreground_tick(0U));
    REQUIRE(!at_rest(&pipeline));
    event('R');
    REQUIRE(motion_library_record_preparing());
    CHECK(state_token("ui", "START_RECORD") && state_number("unsaved") == 0U);
    CHECK(!motion_library_input_allowed() && follow_enabled());
    CHECK(controller.mode == MOTION_RR_LIVE && controller.record_count == 0U);
    event('A');
    CHECK(strstr(mock.last_result, "rejected_") == mock.last_result);
    CHECK(follow_enabled() && controller.mode == MOTION_RR_LIVE);
    clear_transcript();
    motion_library_status();
    CHECK(strstr(transcript, "ui=6;") != NULL);
    for (index = 0U; index < 300U; ++index) {
        REQUIRE(agent2_tick(&pipeline));
        motion_library_tick(&controller, &pipeline);
        report();
        CHECK(controller.record_count == 0U && controller.mode == MOTION_RR_LIVE);
    }
    REQUIRE(at_rest(&pipeline));
    CHECK(motion_library_record_preparing());
    REQUIRE(foreground_tick(0U));
    CHECK(controller.mode == MOTION_RR_LIVE && controller.record_count == 0U);
    for (index = 0U; index < 20U; ++index) motion_library_tick(&controller, &pipeline);
    CHECK(controller.mode == MOTION_RR_LIVE);
    for (index = 0U; index < 8U && !motion_library_input_allowed(); ++index) REQUIRE(foreground_tick(0U));
    REQUIRE(controller.mode == MOTION_RR_RECORDING && motion_library_input_allowed());
    CHECK(controller.record_count >= 2U && follow_enabled());
    target_offset(-18.0f, -6.0f, 8.0f, .55f);
    for (index = 0U; index < 12U; ++index) REQUIRE(foreground_tick(0U));
    REQUIRE(finish_name());
    audit_clip();
}

static void test_prepare_cancel_and_pwm_off(void)
{
    unsigned index;
    REQUIRE(initialize());
    target_offset(30.0f, 0.0f, 0.0f, .05f);
    for (index = 0U; index < 10U; ++index) REQUIRE(foreground_tick(0U));
    event('R');
    REQUIRE(motion_library_record_preparing());
    event('R');
    CHECK(!motion_library_record_preparing() && motion_library_input_allowed());
    CHECK(controller.mode == MOTION_RR_LIVE && controller.record_count == 0U);
    CHECK(state_token("ui", "IDLE") && state_number("unsaved") == 0U);
    for (index = 0U; index < 150U; ++index) REQUIRE(foreground_tick(0U));
    CHECK(controller.record_count == 0U && writes == 0U);
    event('R');
    REQUIRE(motion_library_record_preparing());
    event('X');
    REQUIRE(foreground_tick(0U));
    CHECK(!motion_library_record_preparing() && controller.record_count == 0U);
    CHECK(!pipeline.output_enabled && writes == 0U && state_number("unsaved") == 0U);
}

static void test_prepare_hal_failure(void)
{
    unsigned index;
    REQUIRE(initialize());
    event('R');
    REQUIRE(motion_library_record_preparing());
    servo_pwm_driver_mock_fail_next(OWN_BANK, 0x1CU);
    CHECK(!foreground_tick(0U));
    CHECK(controller.record_count == 0U && writes == 0U);
    for (index = 0U; index < 4U; ++index) {
        motion_library_tick(&controller, &pipeline);
        report();
        CHECK(controller.record_count == 0U);
    }
}

static void test_manual_stop_and_name_survives_controls(void)
{
    uint32_t count, digest;
    REQUIRE(initialize());
    REQUIRE(make_clip());
    audit_clip();
    CHECK(!follow_enabled() && state_number("unsaved") == 1U);
    count = controller.replay_count;
    digest = clip_digest();
    clear_transcript();
    motion_library_status();
    CHECK(strstr(transcript, "ui=3;") != NULL);
    motion_library_report_state(&controller, &pipeline, 1);
    event('V');
    CHECK(state_token("ui", "NAME") && state_number("unsaved") == 1U);
    event('S');
    event('V');
    CHECK(state_token("ui", "NAME") && controller.mode == MOTION_RR_HOLDING);
    clear_transcript();
    event('P');
    CHECK(strstr(transcript, "rejected UI_BUSY") != NULL);
    event('V');
    CHECK(state_token("ui", "NAME") && controller.replay_count == count && clip_digest() == digest);
    event('A');
    CHECK(strstr(mock.last_result, "rejected_") == mock.last_result);
    event('X');
    REQUIRE(foreground_tick(0U));
    clear_transcript();
    event('V');
    CHECK(state_token("ui", "NAME") && state_number("unsaved") == 1U && state_number("pwm") == 0U);
    CHECK(state_number("ram_samples") == count && clip_digest() == digest);
    send_name("PWM_off-save");
    service_save();
    CHECK(state_token("ui", "MENU") && state_number("unsaved") == 0U);
    CHECK(fatfs_mock_exists("0:/MOTION/R0001.BIN"));
}

static void test_automatic_limit_safe_tail(void)
{
    unsigned index;
    uint32_t stop_count = 0U;
    double peak_speed = 0.0;
    REQUIRE(initialize());
    REQUIRE(begin_record());
    for (index = 0U; index < MOTION_RECORD_REPLAY_MAX_SAMPLES + 700U; ++index) {
        if (motion_library_input_allowed() && index % 60U == 0U) {
            float goal = index % 120U ? 90.0f : 135.0f;
            target_offset(goal - pipeline.output.elbow_roll_deg, 0.0f, 0.0f,
                          index % 120U ? .05f : .85f);
        }
        REQUIRE(foreground_tick(0U));
        if (fabs(pipeline.motion.axes[0].v) > peak_speed) peak_speed = fabs(pipeline.motion.axes[0].v);
        if (!stop_count && state_token("ui", "STOP_RECORD")) {
            stop_count = controller.record_count;
            CHECK(stop_count < MOTION_RECORD_REPLAY_MAX_SAMPLES);
            CHECK(controller.mode == MOTION_RR_RECORDING && !motion_library_input_allowed());
            CHECK(!at_rest(&pipeline));
            event('A');
            CHECK(strstr(mock.last_result, "rejected_") != NULL);
        }
        if (stop_count && controller.record_count > stop_count) CHECK(!follow_enabled());
        if (stop_count && !at_rest(&pipeline)) CHECK(controller.record_count < MOTION_RECORD_REPLAY_MAX_SAMPLES);
        if (state_token("ui", "NAME")) break;
    }
    REQUIRE(stop_count > 0U && state_token("ui", "NAME"));
    CHECK(controller.record_count > stop_count + 2U);
    CHECK(peak_speed >= forearm_calibration_config.elbow_roll.max_delta_deg / .020 - .01);
    CHECK(state_number("unsaved") == 1U && writes == 0U && !follow_enabled());
    audit_clip();
    send_name("full_speed-tail");
    service_save();
    CHECK(state_token("ui", "MENU") && fatfs_mock_exists("0:/MOTION/R0001.BIN"));
    motion_record_replay_set_repeat(&controller, 0);
    send_frame("!REC,SELECT,1\r", 0);
    REQUIRE(controller.mode == MOTION_RR_ALIGNING);
    CHECK(state_number("playing") == 1U && state_number("unsaved") == 0U);
    for (index = 0U; index < MOTION_RECORD_REPLAY_MAX_SAMPLES + 700U && controller.mode != MOTION_RR_HOLDING; ++index) {
        MotionSample expected;
        int playing = controller.mode == MOTION_RR_PLAYING;
        if (playing) REQUIRE(motion_record_replay_get_replay_sample(&controller, controller.replay_index, &expected));
        REQUIRE(foreground_tick(0U));
        if (playing) {
            MotionSample actual = sample_from_command(&pipeline.applied_command);
            CHECK(memcmp(&actual, &expected, sizeof(actual)) == 0);
        }
    }
    CHECK(controller.mode == MOTION_RR_HOLDING && controller.reason == MOTION_RR_REASON_COMPLETED);
    CHECK(state_number("selected") == 1U && state_number("playing") == 0U);
}

static void test_names_and_duplicate_preserve_ram(void)
{
    static const char *bad_names[] = { "", "has space", "../bad", "bad/name", "bad.name", "a,b", "1234567890123456789012345", "\xC3\xA9" };
    unsigned index, length;
    uint32_t digest, count, original_crc;
    const unsigned char *data;
    REQUIRE(initialize());
    REQUIRE(make_clip());
    digest = clip_digest();
    count = controller.replay_count;
    for (index = 0U; index < sizeof(bad_names) / sizeof(bad_names[0]); ++index) {
        send_name(bad_names[index]);
        CHECK(state_token("ui", "NAME") && state_number("unsaved") == 1U);
        CHECK(controller.replay_count == count && clip_digest() == digest);
        CHECK(!fatfs_mock_exists("0:/MOTION/R0001.BIN"));
    }
    send_frame("!REC,NAME,MenuBlocked\r", 1);
    CHECK(state_token("ui", "NAME") && writes == 0U);
    send_name("A");
    service_save();
    REQUIRE(state_token("ui", "MENU") && state_number("entries") == 1U);
    data = fatfs_mock_data("0:/MOTION/R0001.BIN", &length);
    REQUIRE(data != NULL);
    original_crc = crc_bytes(data, length);
    send_frame("!REC,CANCEL\r", 0);
    REQUIRE(make_clip());
    digest = clip_digest();
    count = controller.replay_count;
    send_name("A");
    CHECK(state_token("ui", "NAME") && state_number("unsaved") == 1U);
    CHECK(controller.replay_count == count && clip_digest() == digest);
    data = fatfs_mock_data("0:/MOTION/R0001.BIN", &length);
    REQUIRE(data != NULL);
    CHECK(crc_bytes(data, length) == original_crc);
    send_name("Abc_123-abcdefghijklmnop");
    service_save();
    CHECK(state_token("ui", "MENU") && state_number("entries") == 2U && state_number("unsaved") == 0U);
    data = fatfs_mock_data("0:/MOTION/R0002.TXT", &length);
    CHECK(data != NULL && length == MOTION_LIBRARY_NAME_MAX);
    send_frame("!REC,DELETE,1\r", 0);
    CHECK(state_number("delete") == 1U && state_number("entries") == 2U);
    send_frame("!REC,CANCEL\r", 0);
    CHECK(state_number("delete") == 0U && state_token("ui", "IDLE"));
    CHECK(fatfs_mock_exists("0:/MOTION/R0001.BIN") && fatfs_mock_exists("0:/MOTION/R0002.BIN"));
}

static void test_save_failure_atomicity(void)
{
    unsigned fault, length;
    uint32_t original_crc, digest, count;
    const unsigned char *data;
    REQUIRE(initialize());
    REQUIRE(make_clip());
    send_name("existing");
    service_save();
    data = fatfs_mock_data("0:/MOTION/R0001.BIN", &length);
    REQUIRE(data != NULL);
    original_crc = crc_bytes(data, length);
    send_frame("!REC,CANCEL\r", 0);
    REQUIRE(make_clip());
    digest = clip_digest();
    count = controller.replay_count;
    for (fault = 0U; fault < 8U; ++fault) {
        if (fault == 0U) fail_write = 1;
        send_name("retry_ok");
        if (fault == 1U) fail_write = 1;
        if (fault == 2U) fail_sync = 1;
        if (fault == 3U) fail_read = 1;
        if (fault == 4U) corrupt_write = 1;
        if (fault == 5U) fail_rename = 1;
        if (fault == 6U) fail_rename_after = 0;
        if (fault == 7U) fail_rename_after = 1;
        service_save();
        CHECK(state_token("ui", "NAME") && state_number("unsaved") == 1U);
        CHECK(controller.replay_count == count && clip_digest() == digest);
        CHECK(!fatfs_mock_exists("0:/MOTION/R0002.BIN") && !fatfs_mock_exists("0:/MOTION/R0002.TXT"));
        CHECK(!fatfs_mock_exists("0:/MOTION/R0002.NEW") && !fatfs_mock_exists("0:/MOTION/R0002.NTX"));
        data = fatfs_mock_data("0:/MOTION/R0001.BIN", &length);
        REQUIRE(data != NULL);
        CHECK(crc_bytes(data, length) == original_crc);
        fail_write = fail_sync = fail_read = corrupt_write = fail_rename = 0;
        fail_rename_after = -1;
    }
    send_name("retry_ok");
    service_save();
    CHECK(state_token("ui", "MENU") && state_number("unsaved") == 0U && state_number("entries") == 2U);
}

static void test_cancel_transitions(void)
{
    uint32_t digest;
    REQUIRE(initialize());
    REQUIRE(make_clip());
    digest = clip_digest();
    send_frame("!REC,CANCEL\r", 0);
    CHECK(state_token("ui", "IDLE") && state_number("unsaved") == 0U);
    CHECK(controller.mode == MOTION_RR_LIVE && motion_library_input_allowed());
    CHECK(clip_digest() == digest && writes == 0U);
    event('P');
    CHECK(state_token("ui", "STOP_MENU") && !motion_library_input_allowed());
    REQUIRE(foreground_tick(0U));
    REQUIRE(foreground_tick(0U));
    REQUIRE(foreground_tick(0U));
    CHECK(state_token("ui", "MENU"));
    send_frame("!REC,CANCEL\r", 0);
    CHECK(state_token("ui", "IDLE") && controller.mode == MOTION_RR_LIVE);
}

static void test_mrp1_compatibility_and_saved_payload(void)
{
    unsigned length, index;
    uint32_t count, digest;
    const unsigned char *data;
    unsigned char header[24] = { 'M', 'R', 'P', '1', 1, 0, 0, 0, 0x20, 0x4E, 0, 0,
        3, 0, 0, 0, 0xBB, 0xE3, 0xD4, 0x77, 0, 0, 0, 0 };
    MotionSample legacy[3] = { {90.0f, 90.0f, 90.0f, 90.0f, .5f},
        {90.0f, 90.0f, 90.0f, 90.0f, .5f}, {90.0f, 90.0f, 90.0f, 90.0f, .5f} };
    uint32_t crc = crc_bytes((const unsigned char *)legacy, sizeof(legacy));
    FIL file;
    UINT written;
    REQUIRE(initialize());
    event('X');
    for (index = 0U; index < 4U; ++index) header[20U + index] = (unsigned char)(crc >> (index * 8U));
    REQUIRE(f_open(&file, "0:/MOTION.BIN", FA_WRITE | FA_CREATE_NEW) == FR_OK);
    REQUIRE(f_write(&file, header, sizeof(header), &written) == FR_OK && written == sizeof(header));
    REQUIRE(f_write(&file, legacy, sizeof(legacy), &written) == FR_OK && written == sizeof(legacy));
    REQUIRE(f_close(&file) == FR_OK);
    REQUIRE(motion_sd_load(&controller) == MOTION_SD_OK);
    CHECK(controller.replay_count == 3U && motion_record_replay_validate_replay(&controller) == MOTION_RR_REASON_NONE);
    for (index = 0U; index < 3U; ++index) {
        MotionSample sample;
        CHECK(motion_record_replay_get_replay_sample(&controller, index, &sample));
        CHECK(memcmp(&sample, &legacy[index], sizeof(sample)) == 0);
    }
    event('E');
    REQUIRE(make_clip());
    count = controller.replay_count;
    digest = clip_digest();
    send_name("MRP1_compatible");
    service_save();
    data = fatfs_mock_data("0:/MOTION/R0001.BIN", &length);
    REQUIRE(data != NULL && length == 24U + count * sizeof(MotionSample));
    CHECK(memcmp(data, header, 12U) == 0 && memcmp(data + 16U, header + 16U, 4U) == 0);
    CHECK(crc_bytes(data + 24U, length - 24U) == digest);
    send_frame("!REC,CANCEL\r", 0);
    REQUIRE(motion_sd_load_path(&controller, "0:/MOTION/R0001.BIN") == MOTION_SD_OK);
    CHECK(controller.replay_count == count && clip_digest() == digest);
    header[20] ^= 1U;
    REQUIRE(f_open(&file, "0:/MOTION/CRC.BIN", FA_WRITE | FA_CREATE_NEW) == FR_OK);
    event('X');
    REQUIRE(f_write(&file, header, sizeof(header), &written) == FR_OK);
    REQUIRE(f_write(&file, legacy, sizeof(legacy), &written) == FR_OK);
    REQUIRE(f_close(&file) == FR_OK);
    CHECK(motion_sd_load_path(&controller, "0:/MOTION/CRC.BIN") == MOTION_SD_CRC);
}

static void test_lower_level_safety_remains(void)
{
    unsigned index;
    MotionSample unsafe[3] = { {90.0f, 90.0f, 90.0f, 90.0f, .5f},
        {90.0f, 90.0f, 90.0f, 90.0f, .5f}, {90.0f, 90.0f, 90.0f, 90.0f, .5f} };
    REQUIRE(initialize());
    target_offset(35.0f, 0.0f, 0.0f, .05f);
    for (index = 0U; index < 15U; ++index) REQUIRE(foreground_tick(0U));
    REQUIRE(motion_record_replay_start_record(&controller));
    for (index = 0U; index < 10U; ++index) REQUIRE(foreground_tick(0U));
    REQUIRE(motion_record_replay_stop_record(&controller));
    REQUIRE(motion_record_replay_copy_record_to_replay(&controller));
    CHECK(motion_record_replay_validate_replay(&controller) == MOTION_RR_REASON_ACCELERATION);
    CHECK(!motion_record_replay_start_play(&controller, &pipeline, 0U));
    unsafe[1].elbow_roll_deg += 1.0f;
    REQUIRE(motion_record_replay_load_replay(&controller, unsafe, 3U));
    CHECK(motion_record_replay_validate_replay(&controller) == MOTION_RR_REASON_DELTA);
    unsafe[1].elbow_roll_deg = 90.3f;
    REQUIRE(motion_record_replay_load_replay(&controller, unsafe, 3U));
    CHECK(motion_record_replay_validate_replay(&controller) == MOTION_RR_REASON_ACCELERATION);
    unsafe[1] = unsafe[0];
    unsafe[1].gripper_norm = .6f;
    REQUIRE(motion_record_replay_load_replay(&controller, unsafe, 3U));
    CHECK(motion_record_replay_validate_replay(&controller) == MOTION_RR_REASON_GRIPPER_DELTA);
    unsafe[1] = unsafe[0];
    unsafe[1].elbow_roll_deg = NAN;
    REQUIRE(motion_record_replay_load_replay(&controller, unsafe, 3U));
    CHECK(motion_record_replay_validate_replay(&controller) == MOTION_RR_REASON_INVALID_COMMAND);
    unsafe[1] = unsafe[0];
    unsafe[1].elbow_roll_deg = 180.0f;
    REQUIRE(motion_record_replay_load_replay(&controller, unsafe, 3U));
    CHECK(motion_record_replay_validate_replay(&controller) == MOTION_RR_REASON_RANGE);
    unsafe[0] = (MotionSample){90.0f, 90.0f, 160.0f, 90.0f, .5f};
    {
        ForearmJointCommand command = command_from_sample(&unsafe[0]);
        CHECK(!forearm_safety_check_apply(&command, NULL));
    }
    REQUIRE(motion_record_replay_load_replay(&controller, unsafe, 1U));
    CHECK(motion_record_replay_validate_replay(&controller) == MOTION_RR_REASON_SAFETY);
    unsafe[0] = (MotionSample){90.0f, 90.0f, 90.0f, 90.0f, .5f};
    unsafe[1] = unsafe[2] = unsafe[0];
    unsafe[1].elbow_roll_deg += .04f;
    unsafe[2].elbow_roll_deg += .12f;
    REQUIRE(motion_record_replay_load_replay(&controller, unsafe, 3U));
    CHECK(motion_record_replay_validate_replay(&controller) == MOTION_RR_REASON_ACCELERATION);
}

void harness_iteration_begin(void)
{
    CHECK(loop_active);
    ++loop_iteration;
    if (loop_iteration == 1U) CHECK(cnn_app_control_event_post_uart('E'));
    if (loop_iteration == 2U) CHECK(cnn_app_control_event_post_uart('A'));
    if (loop_iteration == 3U) {
        ForearmJointCommand target = active_pipeline->output;
        target.elbow_roll_deg += 30.0f;
        forearm_calibration_set_target(&active_pipeline->motion, &target);
    }
    if (loop_iteration == 4U) CHECK(cnn_app_control_event_post_uart('R'));
    if (loop_resume && loop_iteration == 5U) CHECK(cnn_app_control_event_post_uart('A'));
    if (loop_iteration >= 4U) {
#ifdef ROBOT_STEREO_LEFT
        mock.target = (HumanForearmTarget){0};
        mock.target.valid = 1U;
        mock.target.frame_id = loop_iteration;
        mock.target.elbow_roll_deg = -20.0f;
        mock.target.elbow_pitch_deg = 40.0f;
        mock.target.wrist_valid = 1U;
        mock.target_ready = 1;
        mock.wrist_fresh = 1;
#else
        harness_make_frame(loop_iteration, 1);
#endif
        mock.gripper_ready = 1;
    }
    if (!loop_resume && loop_iteration == 18U) CHECK(cnn_app_control_event_post_uart('R'));
    if (loop_fill && loop_gated) CHECK(!motion_library_input_allowed());
}

void harness_iteration_end(void)
{
    CHECK(loop_active);
    audit_registers();
    servo_pwm_driver_mock_clear_log();
    if (loop_resume) {
        if (loop_iteration >= 4U && motion_library_record_preparing()) {
            CHECK(!motion_library_input_allowed() && follow_enabled());
            CHECK(loop_a2_calls == 0U && loop_gripper_calls == 0U);
            if (loop_iteration == 5U) CHECK(strstr(mock.last_result, "rejected_") == mock.last_result);
        }
        if (!loop_resumed && loop_iteration > 4U && motion_library_input_allowed()) {
            MotionSample first, second;
            CHECK(active_controller->mode == MOTION_RR_RECORDING && active_controller->record_count == 2U);
            CHECK(loop_a2_calls == 0U && loop_gripper_calls == 0U && follow_enabled());
            CHECK(motion_record_replay_get_record_sample(active_controller, 0U, &first));
            CHECK(motion_record_replay_get_record_sample(active_controller, 1U, &second));
            CHECK(memcmp(&first, &second, sizeof(first)) == 0);
            loop_resumed = 1;
        } else if (loop_resumed && !loop_fill) {
            CHECK(loop_a2_calls == 1U && loop_gripper_calls == 1U && follow_enabled());
            CHECK(active_controller->record_count == 3U && writes == 0U);
            CHECK(mock.foreign_request == 1);
            longjmp(loop_exit, 1);
        }
        if (loop_fill && state_token("ui", "STOP_RECORD") && !loop_gated) {
            loop_gated = 1;
            loop_gated_a2 = loop_a2_calls;
            loop_gated_gripper = loop_gripper_calls;
            CHECK(active_controller->record_count < MOTION_RECORD_REPLAY_MAX_SAMPLES);
        } else if (loop_fill && loop_gated) {
            CHECK(loop_a2_calls == loop_gated_a2 && loop_gripper_calls == loop_gated_gripper);
            CHECK(!follow_enabled() && !motion_library_input_allowed());
        }
        if (loop_fill && state_token("ui", "NAME")) {
            CHECK(loop_gated && writes == 0U && mock.foreign_request == 1);
            CHECK(active_controller->mode == MOTION_RR_HOLDING && state_number("unsaved") == 1U);
            controller = *active_controller;
            pipeline = *active_pipeline;
            audit_clip();
            longjmp(loop_exit, 1);
        }
        if (loop_iteration >= (loop_fill ? MOTION_RECORD_REPLAY_MAX_SAMPLES + 700U : 300U)) {
            CHECK(0);
            longjmp(loop_exit, 1);
        }
        return;
    }
    if (loop_iteration >= 4U && loop_iteration < 18U) {
        CHECK(motion_library_record_preparing() && !motion_library_input_allowed());
        CHECK(active_controller->record_count == 0U && follow_enabled());
        CHECK(loop_a2_calls == 0U && loop_gripper_calls == 0U);
    }
    if (loop_iteration == 18U) {
        CHECK(!motion_library_record_preparing() && motion_library_input_allowed());
        CHECK(active_controller->record_count == 0U && writes == 0U);
        CHECK(loop_a2_calls == 1U && loop_gripper_calls == 1U);
        CHECK(mock.foreign_request == 1);
        longjmp(loop_exit, 1);
    }
}

static void test_actual_main_preparation_gate(void)
{
    clear_transcript();
    harness_mock_reset();
    fatfs_mock_reset();
    mock.foreign_request = 1;
    loop_iteration = loop_a2_calls = loop_gripper_calls = 0U;
    loop_resume = loop_resumed = loop_fill = loop_gated = 0;
    loop_active = 1;
    if (setjmp(loop_exit) == 0) {
        int result = regression_firmware_main();
        CHECK(result == 0);
        CHECK(0);
    }
    loop_active = 0;
    CHECK(loop_iteration == 18U);
}

static void test_actual_main_seamless_record_resume(void)
{
    clear_transcript();
    harness_mock_reset();
    fatfs_mock_reset();
    mock.foreign_request = 1;
    loop_iteration = loop_a2_calls = loop_gripper_calls = 0U;
    loop_resume = 1;
    loop_resumed = loop_fill = loop_gated = 0;
    loop_active = 1;
    if (setjmp(loop_exit) == 0) {
        int result = regression_firmware_main();
        CHECK(result == 0);
        CHECK(0);
    }
    loop_active = 0;
    CHECK(loop_resumed && loop_iteration < 300U);
}

static void test_actual_main_limit_blocks_fresh_input(void)
{
    clear_transcript();
    harness_mock_reset();
    fatfs_mock_reset();
    mock.foreign_request = 1;
    loop_iteration = loop_a2_calls = loop_gripper_calls = 0U;
    loop_resume = loop_fill = 1;
    loop_resumed = loop_gated = 0;
    loop_active = 1;
    if (setjmp(loop_exit) == 0) {
        int result = regression_firmware_main();
        CHECK(result == 0);
        CHECK(0);
    }
    loop_active = 0;
    CHECK(loop_resumed && loop_gated && controller.replay_count > 0U);
}

static void test_actual_main_rejected_async_preserves_state(void)
{
    unsigned index, resets_before;
    REQUIRE(initialize());
    mock.foreign_request = 1;
    event('R');
    REQUIRE(motion_library_record_preparing() && controller.mode == MOTION_RR_LIVE);
    check_rejected_async_preserves_state("START_RECORD");
    for (index = 0U; index < 3U; ++index) REQUIRE(foreground_tick(0U));
    REQUIRE(motion_library_record_preparing() && controller.mode == MOTION_RR_RECORDING);
    check_rejected_async_preserves_state("START_RECORD");
    REQUIRE(foreground_tick(0U));
    REQUIRE(foreground_tick(0U));
    REQUIRE(motion_library_input_allowed() && controller.record_count == 2U);
    target_offset(16.0f, 8.0f, -6.0f, .55f);
    for (index = 0U; index < 12U; ++index) REQUIRE(foreground_tick(0U));
    event('R');
    REQUIRE(state_token("ui", "STOP_RECORD"));
    check_rejected_async_preserves_state("STOP_RECORD");
    for (index = 0U; index < 700U && !state_token("ui", "NAME"); ++index) REQUIRE(foreground_tick(0U));
    REQUIRE(state_token("ui", "NAME") && controller.mode == MOTION_RR_HOLDING);
    check_rejected_async_preserves_state("NAME");
    send_name("rejected_A_preserves");
    REQUIRE(state_token("ui", "SAVE"));
    check_rejected_async_preserves_state("SAVE");
    service_save();
    REQUIRE(state_token("ui", "MENU"));
    check_rejected_async_preserves_state("MENU");
    send_frame("!REC,CANCEL\r", 0);
    REQUIRE(motion_library_input_allowed() && controller.mode == MOTION_RR_LIVE);
    event('P');
    REQUIRE(state_token("ui", "STOP_MENU"));
    check_rejected_async_preserves_state("STOP_MENU");
    for (index = 0U; index < 3U; ++index) REQUIRE(foreground_tick(0U));
    REQUIRE(state_token("ui", "MENU"));
    send_frame("!REC,CANCEL\r", 0);
    REQUIRE(state_token("ui", "IDLE") && motion_library_input_allowed());
    seed_input_history(&pipeline);
    resets_before = mock.reset_calls;
    event('A');
    CHECK(strstr(mock.last_result, "enabled_") == mock.last_result);
    CHECK(mock.reset_calls == resets_before + 1U && follow_enabled());
    CHECK(!pipeline.gripper_latch.latched && !pipeline.gripper_motion_hold.locked);
    CHECK(!pipeline.elbow_reentry.seen && !pipeline.elbow_return.valid);
    CHECK(!pipeline.wrist_return[0].valid && !pipeline.wrist_return[1].valid);
    CHECK(!pipeline.wrist_observation_fresh && mock.foreign_request == 1);
}

static void test_board_motion_and_gripper_contract(void)
{
    HumanForearmTarget target = {0};
    ForearmJointCommand mapped;
    uint16_t pwm, previous = 65535U;
    unsigned index;
#ifdef ROBOT_STEREO_LEFT
    const int expected_direction = -1;
    const float expected_acceleration = 60.0f;
    const unsigned expected_open = 1722U, expected_close = 2400U;
#else
    const int expected_direction = 1;
    const float expected_acceleration = 120.0f;
    const unsigned expected_open = 1500U, expected_close = 2500U;
#endif
    CHECK(forearm_calibration_config.elbow_roll.direction == expected_direction);
    CHECK(forearm_calibration_config.elbow_pitch.direction == -1);
    CHECK(forearm_calibration_config.elbow_roll.zero_offset_deg == 90.0f);
    CHECK(forearm_calibration_config.elbow_pitch.zero_offset_deg == 120.0f);
    CHECK(forearm_calibration_config.amax_deg_s2[1] == expected_acceleration);
    CHECK(forearm_calibration_config.elbow_roll.max_delta_deg == .60f);
    CHECK(forearm_calibration_config.elbow_pitch.max_delta_deg == .60f);
    CHECK(forearm_calibration_config.wrist_pitch.max_delta_deg == .70f);
    CHECK(forearm_calibration_config.wrist_roll.max_delta_deg == .70f);
    target.valid = 1U;
    target.elbow_roll_deg = 40.0f;
    target.elbow_pitch_deg = 50.0f;
    target.wrist_pitch_deg = 20.0f;
    target.wrist_roll_deg = -3.0f;
    forearm_motion_control_map_target(&target, &mapped);
    CHECK(mapped.elbow_roll_deg == 90.0f + 40.0f * (float)expected_direction);
    CHECK(mapped.elbow_pitch_deg == 70.0f);
    CHECK(mapped.wrist_pitch_deg == 100.0f);
    CHECK(mapped.wrist_roll_deg == 90.0f);
    REQUIRE(servo_control_convert_channel(SERVO_GRIPPER, 1.0f, &pwm));
    CHECK(pwm == expected_open);
    REQUIRE(servo_control_convert_channel(SERVO_GRIPPER, 0.0f, &pwm));
    CHECK(pwm == expected_close);
    for (index = 0U; index <= 1000U; ++index) {
        REQUIRE(servo_control_convert_channel(SERVO_GRIPPER, (float)index / 1000.0f, &pwm));
        CHECK(pwm >= expected_open && pwm <= expected_close && pwm <= previous);
        previous = pwm;
    }
    pwm = 1234U;
    CHECK(!servo_control_convert_channel(SERVO_GRIPPER, NAN, &pwm));
    CHECK(pwm == 1234U);
}

int main(int argc, char **argv)
{
    static const struct { const char *name; void (*run)(void); } tests[] = {
        { "board_motion_and_gripper_contract", test_board_motion_and_gripper_contract },
        { "status_schema", test_status_schema },
        { "prepare_hal_settle_and_seed", test_prepare_hal_settle_and_seed },
        { "prepare_cancel_and_pwm_off", test_prepare_cancel_and_pwm_off },
        { "prepare_hal_failure", test_prepare_hal_failure },
        { "manual_stop_and_name_survives_controls", test_manual_stop_and_name_survives_controls },
        { "automatic_limit_safe_tail", test_automatic_limit_safe_tail },
        { "names_and_duplicate_preserve_ram", test_names_and_duplicate_preserve_ram },
        { "save_failure_atomicity", test_save_failure_atomicity },
        { "cancel_transitions", test_cancel_transitions },
        { "mrp1_compatibility_and_saved_payload", test_mrp1_compatibility_and_saved_payload },
        { "lower_level_safety_remains", test_lower_level_safety_remains },
        { "actual_main_preparation_gate", test_actual_main_preparation_gate },
        { "actual_main_seamless_record_resume", test_actual_main_seamless_record_resume },
        { "actual_main_limit_blocks_fresh_input", test_actual_main_limit_blocks_fresh_input },
        { "actual_main_rejected_async_preserves_state", test_actual_main_rejected_async_preserves_state }
    };
    unsigned index;
    for (index = 0U; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        unsigned before = failures;
        if (argc > 1 && strcmp(argv[1], tests[index].name) != 0) continue;
        case_name = tests[index].name;
        ++cases;
        tests[index].run();
        printf("CASE %s %s %s\n", ROLE, case_name, failures == before ? "PASS" : "FAIL");
    }
    if (!cases) { puts("Unknown case name"); return 2; }
    printf("RESULT role=%s cases=%u checks=%u failures=%u audited_ticks=%u\n", ROLE, cases, checks, failures, audited_ticks);
    return failures ? 1 : 0;
}
