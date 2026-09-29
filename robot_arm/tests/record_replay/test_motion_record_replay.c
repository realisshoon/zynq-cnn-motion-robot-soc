#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "drivers/servo_pwm_driver.h"
#include "record_replay/motion_record_replay.h"
#include "output_controller/servo_hal.h"
#include "robot_calibration/forearm_calibration_config.h"

static ForearmJointCommand command(float roll, float pitch, float wrist_pitch,
                                   float wrist_roll, float gripper)
{
    ForearmJointCommand value = {
        roll, pitch, wrist_pitch, wrist_roll, gripper, 1U
    };
    return value;
}

static MotionSample sample(float roll, float pitch, float wrist_pitch,
                           float wrist_roll, float gripper)
{
    MotionSample value = {roll, pitch, wrist_pitch, wrist_roll, gripper};
    return value;
}

static MotionSample sample_from_command(const ForearmJointCommand *value)
{
    return sample(value->elbow_roll_deg, value->elbow_pitch_deg,
                  value->wrist_pitch_deg, value->wrist_roll_deg,
                  value->gripper_norm);
}

static void assert_near(float actual, float expected)
{
    assert(fabsf(actual - expected) < 0.0002f);
}

static void assert_sample(const MotionSample *actual, const MotionSample *expected)
{
    assert_near(actual->elbow_roll_deg, expected->elbow_roll_deg);
    assert_near(actual->elbow_pitch_deg, expected->elbow_pitch_deg);
    assert_near(actual->wrist_pitch_deg, expected->wrist_pitch_deg);
    assert_near(actual->wrist_roll_deg, expected->wrist_roll_deg);
    assert_near(actual->gripper_norm, expected->gripper_norm);
}

static void assert_command_sample(const ForearmJointCommand *actual,
                                  const MotionSample *expected)
{
    MotionSample converted = sample_from_command(actual);
    assert_sample(&converted, expected);
}

static void assert_transition_acceleration(
    const ForearmJointCommand *previous,
    const ForearmJointCommand *last,
    const ForearmJointCommand *next)
{
    const float previous_value[FOREARM_MOTION_JOINT_COUNT] = {
        previous->elbow_roll_deg, previous->elbow_pitch_deg,
        previous->wrist_pitch_deg, previous->wrist_roll_deg
    };
    const float last_value[FOREARM_MOTION_JOINT_COUNT] = {
        last->elbow_roll_deg, last->elbow_pitch_deg,
        last->wrist_pitch_deg, last->wrist_roll_deg
    };
    const float next_value[FOREARM_MOTION_JOINT_COUNT] = {
        next->elbow_roll_deg, next->elbow_pitch_deg,
        next->wrist_pitch_deg, next->wrist_roll_deg
    };
    unsigned i;

    for (i = 0U; i < FOREARM_MOTION_JOINT_COUNT; ++i) {
        float velocity_before = (last_value[i] - previous_value[i]) / 0.020f;
        float velocity_after = (next_value[i] - last_value[i]) / 0.020f;
        float acceleration = (velocity_after - velocity_before) / 0.020f;
        assert(fabsf(acceleration) <=
               forearm_calibration_config.amax_deg_s2[i] + 0.1f);
    }
}

static int same_pwm(const ServoPwmCommand *a, const ServoPwmCommand *b)
{
    return a->elbow_roll_pwm_us == b->elbow_roll_pwm_us &&
           a->elbow_pitch_pwm_us == b->elbow_pitch_pwm_us &&
           a->wrist_pitch_pwm_us == b->wrist_pitch_pwm_us &&
           a->wrist_roll_pwm_us == b->wrist_roll_pwm_us &&
           a->gripper_pwm_us == b->gripper_pwm_us;
}

static void reset_mock(void)
{
    servo_pwm_driver_mock_reset();
    servo_hal_init();
}

static void init_context(AgentPipelineContext *pipeline,
                         const ForearmJointCommand *initial)
{
    memset(pipeline, 0, sizeof(*pipeline));
    pipeline->output_enabled = 1U;
    forearm_motion_control_unwrap_state_init(&pipeline->unwrap);
    forearm_calibration_state_init(&pipeline->motion);
    forearm_calibration_set_target(&pipeline->motion, initial);
    forearm_calibration_step(&pipeline->motion, &pipeline->output);
    reset_mock();
    assert(agent3_apply_command(pipeline, &pipeline->output));
    reset_mock();
}

static int controller_tick(MotionRecordReplay *controller,
                           AgentPipelineContext *pipeline,
                           uint32_t overruns)
{
    reset_mock();
    return motion_record_replay_control_tick(controller, pipeline, overruns);
}

static unsigned drive_align(MotionRecordReplay *controller,
                            AgentPipelineContext *pipeline,
                            uint32_t overruns)
{
    unsigned ticks = 0U;
    while (motion_record_replay_mode(controller) == MOTION_RR_ALIGNING) {
        uint32_t ticks_before = pipeline->ticks;
        assert(controller_tick(controller, pipeline, overruns));
        assert(pipeline->ticks == ticks_before + 1U);
        assert(++ticks < 1000U);
    }
    assert(motion_record_replay_mode(controller) == MOTION_RR_PLAYING);
    return ticks;
}

static void configure(MotionRecordReplay *controller, float gripper_step)
{
    motion_record_replay_init(controller);
    assert(motion_record_replay_configure_align(controller, gripper_step, 800U));
}

static void test_record_tick_trajectory(void)
{
    AgentPipelineContext pipeline;
    MotionRecordReplay controller;
    ForearmJointCommand initial = command(90.0f, 90.0f, 90.0f, 90.0f, 0.2f);
    ForearmJointCommand target = command(92.0f, 91.0f, 89.0f, 90.5f, 0.8f);
    MotionSample expected[50];
    MotionSample actual;
    uint32_t ticks_before;
    uint32_t i;

    init_context(&pipeline, &initial);
    motion_record_replay_init(&controller);
    forearm_calibration_set_target(&pipeline.motion, &target);
    assert(motion_record_replay_start_record(&controller));
    ticks_before = pipeline.ticks;
    for (i = 0U; i < 50U; ++i) {
        pipeline.retargets += (i % 7U == 0U) ? 3U : 0U;
        assert(controller_tick(&controller, &pipeline, 0U));
        expected[i] = sample_from_command(&pipeline.output);
    }
    assert(motion_record_replay_record_count(&controller) == 50U);
    assert(pipeline.ticks == ticks_before + 50U);
    assert(pipeline.retargets != 50U);
    for (i = 0U; i < 50U; ++i) {
        assert(motion_record_replay_get_record_sample(&controller, i, &actual));
        assert_sample(&actual, &expected[i]);
    }
    assert(!motion_record_replay_get_record_sample(&controller, 50U, &actual));
    assert(!motion_record_replay_start_play(&controller, &pipeline, 0U));
    assert(motion_record_replay_reason(&controller) == MOTION_RR_REASON_BUSY);
    assert(motion_record_replay_stop_record(&controller));
    assert(motion_record_replay_copy_record_to_replay(&controller));
    assert(motion_record_replay_replay_count(&controller) == 50U);
}

static void test_record_failure_sources_and_buffer_full(void)
{
    AgentPipelineContext pipeline;
    MotionRecordReplay controller;
    ForearmJointCommand initial = command(90.0f, 90.0f, 90.0f, 90.0f, 0.5f);
    ServoPwmCommand before;
    MotionSample first, replayed, last_before, last_after;
    uint32_t i;

    init_context(&pipeline, &initial);
    motion_record_replay_init(&controller);
    assert(motion_record_replay_start_record(&controller));
    assert(controller_tick(&controller, &pipeline, 0U));
    assert(motion_record_replay_record_count(&controller) == 1U);
    assert(motion_record_replay_get_record_sample(&controller, 0U, &first));
    before = pipeline.pwm;
    reset_mock();
    for (i = 0U; i < 127U; ++i) assert(servo_hal_disable());
    assert(!motion_record_replay_control_tick(&controller, &pipeline, 0U));
    assert(motion_record_replay_record_count(&controller) == 1U);
    assert(motion_record_replay_replay_count(&controller) == 1U);
    assert(motion_record_replay_mode(&controller) == MOTION_RR_LIVE);
    assert(motion_record_replay_reason(&controller) ==
           MOTION_RR_REASON_AGENT3_FAILURE);
    assert(motion_record_replay_record_source(&controller) ==
           MOTION_RR_APPLIED_HAL);
    assert(motion_record_replay_replay_source(&controller) ==
           MOTION_RR_APPLIED_HAL);
    assert(motion_record_replay_get_replay_sample(&controller, 0U, &replayed));
    assert_sample(&replayed, &first);
    assert(same_pwm(&pipeline.pwm, &before));
    assert(pipeline.servo_errors == 1U);

    /* PWM-disabled recording is explicitly marked as software output. */
    init_context(&pipeline, &initial);
    pipeline.output_enabled = 0U;
    motion_record_replay_init(&controller);
    assert(motion_record_replay_start_record(&controller));
    assert(controller_tick(&controller, &pipeline, 0U));
    assert(motion_record_replay_record_source(&controller) ==
           MOTION_RR_APPLIED_SOFTWARE_OUTPUT);
    assert(motion_record_replay_on_record_button_pulse(&controller));
    assert(motion_record_replay_replay_source(&controller) ==
           MOTION_RR_APPLIED_SOFTWARE_OUTPUT);

    /* Buffer full automatically finalizes all 1024 successful samples. */
    init_context(&pipeline, &initial);
    motion_record_replay_init(&controller);
    assert(motion_record_replay_start_record(&controller));
    for (i = 0U; i < MOTION_RECORD_REPLAY_MAX_SAMPLES; ++i) {
        assert(controller_tick(&controller, &pipeline, 0U));
    }
    assert(motion_record_replay_record_count(&controller) ==
           MOTION_RECORD_REPLAY_MAX_SAMPLES);
    assert(motion_record_replay_mode(&controller) == MOTION_RR_LIVE);
    assert(motion_record_replay_reason(&controller) ==
           MOTION_RR_REASON_BUFFER_FULL);
    assert(motion_record_replay_replay_count(&controller) ==
           MOTION_RECORD_REPLAY_MAX_SAMPLES);
    assert(motion_record_replay_replay_source(&controller) ==
           MOTION_RR_APPLIED_HAL);
    assert(motion_record_replay_get_record_sample(
        &controller, MOTION_RECORD_REPLAY_MAX_SAMPLES - 1U, &last_before));
    assert(motion_record_replay_get_replay_sample(
        &controller, MOTION_RECORD_REPLAY_MAX_SAMPLES - 1U, &last_after));
    assert_sample(&last_after, &last_before);

    /* Full 종료 뒤 R을 다시 눌러도 확정된 replay는 지워지지 않는다. */
    assert(motion_record_replay_on_record_button_pulse(&controller));
    assert(motion_record_replay_record_count(&controller) == 0U);
    assert(motion_record_replay_replay_count(&controller) ==
           MOTION_RECORD_REPLAY_MAX_SAMPLES);
    assert(motion_record_replay_get_replay_sample(
        &controller, MOTION_RECORD_REPLAY_MAX_SAMPLES - 1U, &last_after));
    assert_sample(&last_after, &last_before);
    assert(motion_record_replay_on_record_button_pulse(&controller));
}

static void test_record_timing_and_record_button(void)
{
    AgentPipelineContext pipeline;
    MotionRecordReplay controller;
    ForearmJointCommand initial = command(90.0f, 90.0f, 90.0f, 90.0f, 0.5f);
    ForearmJointCommand target = command(91.0f, 90.5f, 90.0f, 90.0f, 0.6f);
    MotionSample recorded[2], replayed;
    uint32_t ticks_before;

    init_context(&pipeline, &initial);
    motion_record_replay_init(&controller);
    assert(motion_record_replay_on_record_button_pulse(&controller));
    assert(motion_record_replay_mode(&controller) == MOTION_RR_RECORDING);
    ticks_before = pipeline.ticks;
    assert(controller_tick(&controller, &pipeline, 4U));
    assert(pipeline.ticks == ticks_before + 1U);
    assert(motion_record_replay_record_count(&controller) == 1U);
    assert(controller_tick(&controller, &pipeline, 5U));
    assert(pipeline.ticks == ticks_before + 2U);
    assert(motion_record_replay_record_count(&controller) == 1U);
    assert(motion_record_replay_mode(&controller) == MOTION_RR_LIVE);
    assert(motion_record_replay_reason(&controller) ==
           MOTION_RR_REASON_TICK_OVERRUN);
    assert(motion_record_replay_replay_count(&controller) == 1U);

    /* A failed Agent2 control step terminates recording without a hole. */
    assert(motion_record_replay_on_record_button_pulse(&controller));
    pipeline.motion.has_target = 0;
    assert(!controller_tick(&controller, &pipeline, 5U));
    assert(motion_record_replay_record_count(&controller) == 0U);
    assert(motion_record_replay_mode(&controller) == MOTION_RR_LIVE);
    assert(motion_record_replay_reason(&controller) ==
           MOTION_RR_REASON_INVALID_COMMAND);

    /* A normal second REC pulse stops and copies all five-axis samples. */
    init_context(&pipeline, &initial);
    motion_record_replay_init(&controller);
    forearm_calibration_set_target(&pipeline.motion, &target);
    assert(motion_record_replay_on_record_button_pulse(&controller));
    assert(controller_tick(&controller, &pipeline, 0U));
    assert(controller_tick(&controller, &pipeline, 0U));
    assert(motion_record_replay_get_record_sample(&controller, 0U, &recorded[0]));
    assert(motion_record_replay_get_record_sample(&controller, 1U, &recorded[1]));
    assert(motion_record_replay_on_record_button_pulse(&controller));
    assert(motion_record_replay_mode(&controller) == MOTION_RR_LIVE);
    assert(motion_record_replay_replay_count(&controller) == 2U);
    assert(motion_record_replay_get_replay_sample(&controller, 0U, &replayed));
    assert_sample(&replayed, &recorded[0]);
    assert(motion_record_replay_get_replay_sample(&controller, 1U, &replayed));
    assert_sample(&replayed, &recorded[1]);

    /* Empty REC stop keeps the previous usable replay instead of destroying it. */
    assert(motion_record_replay_on_record_button_pulse(&controller));
    assert(motion_record_replay_on_record_button_pulse(&controller));
    assert(motion_record_replay_record_count(&controller) == 0U);
    assert(motion_record_replay_replay_count(&controller) == 2U);
}

static void set_target_from_robot_sample(AgentPipelineContext *pipeline,
                                         const MotionSample *value)
{
    memset(&pipeline->target, 0, sizeof(pipeline->target));
    pipeline->target.elbow_roll_deg =
        (value->elbow_roll_deg - forearm_calibration_config.elbow_roll.zero_offset_deg) /
        (forearm_calibration_config.elbow_roll.scale *
         (float)forearm_calibration_config.elbow_roll.direction);
    pipeline->target.elbow_pitch_deg =
        (value->elbow_pitch_deg - forearm_calibration_config.elbow_pitch.zero_offset_deg) /
        (forearm_calibration_config.elbow_pitch.scale *
         (float)forearm_calibration_config.elbow_pitch.direction);
    pipeline->target.wrist_pitch_deg =
        (value->wrist_pitch_deg - forearm_calibration_config.wrist_pitch.zero_offset_deg) /
        (forearm_calibration_config.wrist_pitch.scale *
         (float)forearm_calibration_config.wrist_pitch.direction);
    pipeline->target.wrist_roll_deg =
        (value->wrist_roll_deg - forearm_calibration_config.wrist_roll.zero_offset_deg) /
        (forearm_calibration_config.wrist_roll.scale *
         (float)forearm_calibration_config.wrist_roll.direction);
    pipeline->target.gripper_norm = value->gripper_norm;
    pipeline->target.valid = 1U;
    pipeline->target_ready = 1U;
}

static void test_replay_loops_with_safe_return_align(void)
{
    AgentPipelineContext pipeline;
    MotionRecordReplay controller;
    ForearmJointCommand initial = command(90.0f, 90.0f, 90.0f, 90.0f, 0.0f);
    MotionSample samples[] = {
        {90.0f, 90.0f, 90.0f, 90.0f, 0.3f},
        {90.04f, 90.02f, 90.0f, 90.0f, 0.34f},
        {90.08f, 90.04f, 90.0f, 90.0f, 0.38f}
    };
    float previous_gripper = 0.0f;
    unsigned i, loop, align_ticks = 0U;

    init_context(&pipeline, &initial);
    configure(&controller, 0.05f);
    assert(motion_record_replay_load_replay(&controller, samples, 3U));
    assert(motion_record_replay_on_play_button_pulse(
        &controller, &pipeline, 0U));
    assert(!motion_record_replay_agent2_run_allowed(&controller));
    while (motion_record_replay_mode(&controller) == MOTION_RR_ALIGNING) {
        assert(controller_tick(&controller, &pipeline, 0U));
        assert(fabsf(pipeline.applied_command.gripper_norm - previous_gripper) <=
               0.0501f);
        previous_gripper = pipeline.applied_command.gripper_norm;
        assert(++align_ticks < 100U);
    }
    assert(align_ticks > 1U);
    assert(motion_record_replay_mode(&controller) == MOTION_RR_PLAYING);
    assert(motion_record_replay_replay_index(&controller) == 0U);
    for (loop = 0U; loop < 2U; ++loop) {
        double direct_q[FOREARM_MOTION_JOINT_COUNT];
        ForearmJointCommand previous_play = command(
            samples[1].elbow_roll_deg, samples[1].elbow_pitch_deg,
            samples[1].wrist_pitch_deg, samples[1].wrist_roll_deg,
            samples[1].gripper_norm);
        ForearmJointCommand last_play = command(
            samples[2].elbow_roll_deg, samples[2].elbow_pitch_deg,
            samples[2].wrist_pitch_deg, samples[2].wrist_roll_deg,
            samples[2].gripper_norm);

        for (i = 0U; i < FOREARM_MOTION_JOINT_COUNT; ++i)
            direct_q[i] = pipeline.motion.axes[i].q;
        for (i = 0U; i < 3U; ++i) {
            uint32_t ticks_before = pipeline.ticks;
            assert(controller_tick(&controller, &pipeline, 0U));
            assert(pipeline.ticks == ticks_before + 1U);
            assert_command_sample(&pipeline.agent3_command, &samples[i]);
            assert_command_sample(&pipeline.applied_command, &samples[i]);
            if (i + 1U < 3U) {
                unsigned axis;
                assert(motion_record_replay_mode(&controller) ==
                       MOTION_RR_PLAYING);
                for (axis = 0U; axis < FOREARM_MOTION_JOINT_COUNT; ++axis)
                    assert(pipeline.motion.axes[axis].q == direct_q[axis]);
            }
        }
        assert(motion_record_replay_mode(&controller) == MOTION_RR_ALIGNING);
        assert(motion_record_replay_reason(&controller) == MOTION_RR_REASON_NONE);
        assert(motion_record_replay_replay_index(&controller) == 3U);
        assert_command_sample(&pipeline.applied_command, &samples[2]);
        assert_near((float)pipeline.motion.axes[0].q,
                    samples[2].elbow_roll_deg);
        assert_near((float)pipeline.motion.axes[0].v,
                    (samples[2].elbow_roll_deg -
                     samples[1].elbow_roll_deg) / 0.020f);

        /* PLAY 마지막 두 명령의 속도까지 이어받아 첫 ALIGN 가속도를 제한한다. */
        assert(controller_tick(&controller, &pipeline, 0U));
        assert(motion_record_replay_mode(&controller) == MOTION_RR_ALIGNING);
        assert_transition_acceleration(
            &previous_play, &last_play, &pipeline.applied_command);
        assert(fabsf(pipeline.applied_command.elbow_roll_deg -
                     samples[2].elbow_roll_deg) <=
               forearm_calibration_config.elbow_roll.max_delta_deg + 0.0001f);
        (void)drive_align(&controller, &pipeline, 0U);
        assert(motion_record_replay_replay_index(&controller) == 0U);
    }
}

static void start_and_align(MotionRecordReplay *controller,
                            AgentPipelineContext *pipeline,
                            const MotionSample *samples,
                            uint32_t count)
{
    configure(controller, 0.2f);
    assert(motion_record_replay_load_replay(controller, samples, count));
    assert(motion_record_replay_start_play(controller, pipeline, 0U));
    (void)drive_align(controller, pipeline, 0U);
}

static void test_invalid_samples(void)
{
    AgentPipelineContext pipeline;
    MotionRecordReplay controller;
    ForearmJointCommand initial = command(90.0f, 90.0f, 90.0f, 90.0f, 0.5f);
    MotionSample bad;

    init_context(&pipeline, &initial);
    configure(&controller, 0.1f);
    bad = sample(NAN, 90.0f, 90.0f, 90.0f, 0.5f);
    assert(motion_record_replay_load_replay(&controller, &bad, 1U));
    assert(!motion_record_replay_start_play(&controller, &pipeline, 0U));
    assert(motion_record_replay_reason(&controller) == MOTION_RR_REASON_INVALID_COMMAND);

    bad = sample(INFINITY, 90.0f, 90.0f, 90.0f, 0.5f);
    assert(motion_record_replay_load_replay(&controller, &bad, 1U));
    assert(!motion_record_replay_start_play(&controller, &pipeline, 0U));
    assert(motion_record_replay_reason(&controller) == MOTION_RR_REASON_INVALID_COMMAND);

    bad = sample(19.0f, 90.0f, 90.0f, 90.0f, 0.5f);
    assert(motion_record_replay_load_replay(&controller, &bad, 1U));
    assert(!motion_record_replay_start_play(&controller, &pipeline, 0U));
    assert(motion_record_replay_reason(&controller) == MOTION_RR_REASON_RANGE);

    bad = sample(90.0f, 90.0f, 90.0f, 90.0f, 1.01f);
    assert(motion_record_replay_load_replay(&controller, &bad, 1U));
    assert(!motion_record_replay_start_play(&controller, &pipeline, 0U));
    assert(motion_record_replay_reason(&controller) == MOTION_RR_REASON_RANGE);

    bad = sample(90.0f, 90.0f, 156.0f, 90.0f, 0.5f);
    assert(motion_record_replay_load_replay(&controller, &bad, 1U));
    assert(!motion_record_replay_start_play(&controller, &pipeline, 0U));
    assert(motion_record_replay_reason(&controller) == MOTION_RR_REASON_SAFETY);
}

static void assert_preflight_rejected(AgentPipelineContext *pipeline,
                                      MotionRecordReplay *controller,
                                      const MotionSample *samples,
                                      MotionRecordReplayReason reason)
{
    configure(controller, 0.2f);
    assert(motion_record_replay_load_replay(controller, samples, 2U));
    assert(!motion_record_replay_start_play(controller, pipeline, 0U));
    assert(motion_record_replay_mode(controller) == MOTION_RR_LIVE);
    assert(motion_record_replay_reason(controller) == reason);
    assert(motion_record_replay_replay_index(controller) == 0U);
}

static void test_full_preflight_and_play_abort_conditions(void)
{
    AgentPipelineContext pipeline;
    MotionRecordReplay controller;
    ForearmJointCommand initial = command(90.0f, 90.0f, 90.0f, 90.0f, 0.5f);
    MotionSample safety_samples[] = {
        {90.0f, 90.0f, 90.0f, 90.0f, 0.5f},
        {90.0f, 90.0f, 156.0f, 90.0f, 0.5f}
    };
    MotionSample delta_samples[] = {
        {90.0f, 90.0f, 90.0f, 90.0f, 0.5f},
        {91.0f, 90.0f, 90.0f, 90.0f, 0.5f}
    };
    MotionSample finite_samples[] = {
        {90.0f, 90.0f, 90.0f, 90.0f, 0.5f},
        {NAN, 90.0f, 90.0f, 90.0f, 0.5f}
    };
    MotionSample range_samples[] = {
        {90.0f, 90.0f, 90.0f, 90.0f, 0.5f},
        {90.0f, 90.0f, 90.0f, 90.0f, 1.1f}
    };
    MotionSample acceleration_samples[] = {
        {90.0f, 90.0f, 90.0f, 90.0f, 0.5f},
        {90.1f, 90.0f, 90.0f, 90.0f, 0.5f}
    };
    MotionSample gripper_samples[] = {
        {90.0f, 90.0f, 90.0f, 90.0f, 0.5f},
        {90.0f, 90.0f, 90.0f, 90.0f, 0.8f}
    };
    MotionSample safe_samples[] = {
        {90.0f, 90.0f, 90.0f, 90.0f, 0.5f},
        {90.04f, 90.0f, 90.0f, 90.0f, 0.5f},
        {90.08f, 90.0f, 90.0f, 90.0f, 0.5f}
    };

    /* Sample 0 is valid in every case. A bad later sample must prevent ALIGN
     * from starting, rather than failing only when PLAY reaches that sample. */
    init_context(&pipeline, &initial);
    assert_preflight_rejected(&pipeline, &controller, finite_samples,
                              MOTION_RR_REASON_INVALID_COMMAND);
    init_context(&pipeline, &initial);
    assert_preflight_rejected(&pipeline, &controller, range_samples,
                              MOTION_RR_REASON_RANGE);
    init_context(&pipeline, &initial);
    assert_preflight_rejected(&pipeline, &controller, safety_samples,
                              MOTION_RR_REASON_SAFETY);
    init_context(&pipeline, &initial);
    assert_preflight_rejected(&pipeline, &controller, delta_samples,
                              MOTION_RR_REASON_DELTA);
    init_context(&pipeline, &initial);
    assert_preflight_rejected(&pipeline, &controller, acceleration_samples,
                              MOTION_RR_REASON_ACCELERATION);
    init_context(&pipeline, &initial);
    assert_preflight_rejected(&pipeline, &controller, gripper_samples,
                              MOTION_RR_REASON_GRIPPER_DELTA);

    /* Position, gripper, and acceleration limits all pass this trajectory. */
    init_context(&pipeline, &initial);
    start_and_align(&controller, &pipeline, safe_samples, 3U);
    assert(controller_tick(&controller, &pipeline, 0U));
    assert(controller_tick(&controller, &pipeline, 0U));
    assert(controller_tick(&controller, &pipeline, 0U));
    assert(motion_record_replay_mode(&controller) == MOTION_RR_ALIGNING);
    assert(motion_record_replay_reason(&controller) == MOTION_RR_REASON_NONE);

    init_context(&pipeline, &initial);
    start_and_align(&controller, &pipeline, safe_samples, 3U);
    assert(!controller_tick(&controller, &pipeline, 1U));
    assert(motion_record_replay_mode(&controller) == MOTION_RR_HOLDING);
    assert(motion_record_replay_reason(&controller) ==
           MOTION_RR_REASON_TICK_OVERRUN);
    assert_command_sample(&pipeline.output, &safe_samples[0]);
}

static void test_decreasing_gripper_align_and_timeout_hold(void)
{
    AgentPipelineContext pipeline;
    MotionRecordReplay controller;
    ForearmJointCommand initial = command(90.0f, 90.0f, 90.0f, 90.0f, 1.0f);
    MotionSample target = {90.0f, 90.0f, 90.0f, 90.0f, 0.4f};
    float previous = initial.gripper_norm;
    unsigned ticks = 0U;

    init_context(&pipeline, &initial);
    configure(&controller, 0.05f);
    assert(motion_record_replay_load_replay(&controller, &target, 1U));
    assert(motion_record_replay_start_play(&controller, &pipeline, 0U));
    while (motion_record_replay_mode(&controller) == MOTION_RR_ALIGNING) {
        assert(controller_tick(&controller, &pipeline, 0U));
        assert(fabsf(pipeline.applied_command.gripper_norm - previous) <= 0.0501f);
        previous = pipeline.applied_command.gripper_norm;
        assert(++ticks < 100U);
    }
    assert(ticks > 1U);
    assert(motion_record_replay_mode(&controller) == MOTION_RR_PLAYING);

    /* A deliberately short budget cannot finish a distant ALIGN. */
    initial = command(90.0f, 90.0f, 90.0f, 90.0f, 0.5f);
    target = sample(110.0f, 90.0f, 90.0f, 90.0f, 0.5f);
    init_context(&pipeline, &initial);
    motion_record_replay_init(&controller);
    assert(motion_record_replay_configure_align(&controller, 0.1f, 1U));
    assert(motion_record_replay_load_replay(&controller, &target, 1U));
    assert(motion_record_replay_start_play(&controller, &pipeline, 0U));
    assert(controller_tick(&controller, &pipeline, 0U));
    assert(!controller_tick(&controller, &pipeline, 0U));
    assert(motion_record_replay_mode(&controller) == MOTION_RR_HOLDING);
    assert(motion_record_replay_reason(&controller) ==
           MOTION_RR_REASON_ALIGN_TIMEOUT);

    init_context(&pipeline, &initial);
    configure(&controller, 0.1f);
    assert(motion_record_replay_load_replay(&controller, &target, 1U));
    assert(motion_record_replay_start_play(&controller, &pipeline, 0U));
    {
        uint32_t ticks_before = pipeline.ticks;
        assert(!controller_tick(&controller, &pipeline, 1U));
        assert(pipeline.ticks == ticks_before + 1U);
    }
    assert(motion_record_replay_mode(&controller) == MOTION_RR_HOLDING);
    assert(motion_record_replay_reason(&controller) ==
           MOTION_RR_REASON_TICK_OVERRUN);
}

static void test_record_start_while_moving_is_rejected_at_replay(void)
{
    AgentPipelineContext pipeline;
    MotionRecordReplay controller;
    ForearmJointCommand initial = command(90.0f, 90.0f, 90.0f, 90.0f, 0.5f);
    ForearmJointCommand target = command(110.0f, 90.0f, 90.0f, 90.0f, 0.5f);
    MotionSample first, second;
    unsigned i;

    init_context(&pipeline, &initial);
    motion_record_replay_init(&controller);
    forearm_calibration_set_target(&pipeline.motion, &target);
    for (i = 0U; i < 8U; ++i) {
        assert(controller_tick(&controller, &pipeline, 0U));
    }
    assert(fabs(pipeline.motion.axes[0].v) > 0.05);

    assert(motion_record_replay_on_record_button_pulse(&controller));
    assert(controller_tick(&controller, &pipeline, 0U));
    assert(controller_tick(&controller, &pipeline, 0U));
    assert(motion_record_replay_on_record_button_pulse(&controller));
    assert(motion_record_replay_get_replay_sample(&controller, 0U, &first));
    assert(motion_record_replay_get_replay_sample(&controller, 1U, &second));
    assert(fabsf(second.elbow_roll_deg - first.elbow_roll_deg) >
           forearm_calibration_config.amax_deg_s2[0] * 0.020f * 0.020f);

    assert(motion_record_replay_configure_align(&controller, 0.1f, 800U));
    assert(!motion_record_replay_start_play(&controller, &pipeline, 0U));
    assert(motion_record_replay_mode(&controller) == MOTION_RR_LIVE);
    assert(motion_record_replay_reason(&controller) ==
           MOTION_RR_REASON_ACCELERATION);
}

static void test_record_stationary_lead_in_is_replayable(void)
{
    AgentPipelineContext pipeline;
    MotionRecordReplay controller;
    ForearmJointCommand initial = command(90.0f, 90.0f, 90.0f, 90.0f, 0.5f);
    ForearmJointCommand target = command(92.0f, 90.0f, 90.0f, 90.0f, 0.5f);
    unsigned i;

    init_context(&pipeline, &initial);
    motion_record_replay_init(&controller);
    assert(motion_record_replay_on_record_button_pulse(&controller));
    /* Sample0을 정지 자세로 확보한 뒤 움직이면 ALIGN의 v=0 경계와 일치한다. */
    assert(controller_tick(&controller, &pipeline, 0U));
    forearm_calibration_set_target(&pipeline.motion, &target);
    for (i = 0U; i < 20U; ++i) {
        assert(controller_tick(&controller, &pipeline, 0U));
    }
    assert(motion_record_replay_on_record_button_pulse(&controller));
    assert(motion_record_replay_configure_align(&controller, 0.1f, 800U));
    assert(motion_record_replay_start_play(&controller, &pipeline, 0U));
    assert(motion_record_replay_mode(&controller) == MOTION_RR_ALIGNING);
}

static void test_play_after_hal_failure_reseeds_from_last_success(void)
{
    AgentPipelineContext pipeline;
    MotionRecordReplay controller;
    ForearmJointCommand initial = command(90.0f, 90.0f, 90.0f, 90.0f, 0.5f);
    ForearmJointCommand target = command(100.0f, 90.0f, 90.0f, 90.0f, 0.5f);
    ForearmJointCommand previous_success, last_success;
    MotionSample replay = {91.0f, 90.0f, 90.0f, 90.0f, 0.5f};
    uint32_t i;

    init_context(&pipeline, &initial);
    configure(&controller, 0.1f);
    assert(motion_record_replay_load_replay(&controller, &replay, 1U));
    forearm_calibration_set_target(&pipeline.motion, &target);
    assert(controller_tick(&controller, &pipeline, 0U));
    assert(controller_tick(&controller, &pipeline, 0U));
    previous_success = controller.previous_applied_replay_command;
    last_success = controller.last_applied_replay_command;
    assert(fabsf(last_success.elbow_roll_deg -
                 previous_success.elbow_roll_deg) > 0.001f);

    reset_mock();
    for (i = 0U; i < 127U; ++i) assert(servo_hal_disable());
    assert(!motion_record_replay_control_tick(&controller, &pipeline, 0U));
    assert_near(pipeline.applied_command.elbow_roll_deg,
                last_success.elbow_roll_deg);

    assert(motion_record_replay_start_play(&controller, &pipeline, 0U));
    assert(motion_record_replay_mode(&controller) == MOTION_RR_ALIGNING);
    assert(controller.last_applied_replay_source == MOTION_RR_APPLIED_HAL);
    assert_near(pipeline.output.elbow_roll_deg, last_success.elbow_roll_deg);
    assert_near((float)pipeline.motion.axes[0].q,
                last_success.elbow_roll_deg);
    assert_near((float)pipeline.motion.axes[0].v,
                (last_success.elbow_roll_deg -
                 previous_success.elbow_roll_deg) / 0.020f);
    assert(controller_tick(&controller, &pipeline, 0U));
    assert_transition_acceleration(
        &previous_success, &last_success, &pipeline.applied_command);
}

static void test_moving_live_to_align_preserves_command_velocity(void)
{
    AgentPipelineContext pipeline;
    MotionRecordReplay controller;
    ForearmJointCommand initial = command(90.0f, 90.0f, 90.0f, 90.0f, 0.5f);
    ForearmJointCommand target = command(100.0f, 90.0f, 90.0f, 90.0f, 0.5f);
    ForearmJointCommand previous_success, last_success;
    MotionSample replay = {89.0f, 90.0f, 90.0f, 90.0f, 0.5f};

    init_context(&pipeline, &initial);
    configure(&controller, 0.1f);
    assert(motion_record_replay_load_replay(&controller, &replay, 1U));
    forearm_calibration_set_target(&pipeline.motion, &target);
    assert(controller_tick(&controller, &pipeline, 0U));
    previous_success = controller.previous_applied_replay_command;
    last_success = controller.last_applied_replay_command;
    assert(last_success.elbow_roll_deg > previous_success.elbow_roll_deg);

    assert(motion_record_replay_start_play(&controller, &pipeline, 0U));
    assert_near((float)pipeline.motion.axes[0].v,
                (last_success.elbow_roll_deg -
                 previous_success.elbow_roll_deg) / 0.020f);
    assert(controller_tick(&controller, &pipeline, 0U));
    assert_transition_acceleration(
        &previous_success, &last_success, &pipeline.applied_command);
}

static void test_agent3_failure_preserves_pwm_and_aborts_play(void)
{
    AgentPipelineContext pipeline;
    MotionRecordReplay controller;
    ForearmJointCommand initial = command(90.0f, 90.0f, 90.0f, 90.0f, 0.5f);
    MotionSample samples[] = {
        {90.0f, 90.0f, 90.0f, 90.0f, 0.5f},
        {90.04f, 90.0f, 90.0f, 90.0f, 0.5f}
    };
    ServoPwmCommand before;
    uint32_t i;

    init_context(&pipeline, &initial);
    start_and_align(&controller, &pipeline, samples, 2U);
    assert(controller_tick(&controller, &pipeline, 0U));
    before = pipeline.pwm;

    reset_mock();
    for (i = 0U; i < 127U; ++i) assert(servo_hal_disable());
    assert(!motion_record_replay_control_tick(&controller, &pipeline, 0U));
    assert(motion_record_replay_reason(&controller) ==
           MOTION_RR_REASON_AGENT3_FAILURE);
    assert(motion_record_replay_mode(&controller) == MOTION_RR_HOLDING);
    assert(same_pwm(&pipeline.pwm, &before));
    assert_command_sample(&pipeline.output, &samples[0]);
}

static void test_agent3_command_freshness(void)
{
    AgentPipelineContext pipeline;
    ForearmJointCommand initial = command(90.0f, 90.0f, 90.0f, 90.0f, 0.5f);

    init_context(&pipeline, &initial);
    assert(agent3_command_is_current_tick(&pipeline));
    ++pipeline.ticks; /* A control step that never invokes Agent3. */
    assert(!agent3_command_is_current_tick(&pipeline));
    assert(agent3_apply_command(&pipeline, &pipeline.output));
    assert(agent3_command_is_current_tick(&pipeline));
}

static void test_play_button_toggle_and_button_exclusion(void)
{
    AgentPipelineContext pipeline;
    MotionRecordReplay controller;
    ForearmJointCommand initial = command(90.0f, 90.0f, 90.0f, 90.0f, 0.5f);
    MotionSample samples[] = {
        {90.0f, 90.0f, 90.0f, 90.0f, 0.5f},
        {90.04f, 90.0f, 90.0f, 90.0f, 0.5f}
    };
    uint32_t retargets;

    /* ALIGNING + PLAY pulse returns to LIVE and clears stale camera input. */
    init_context(&pipeline, &initial);
    configure(&controller, 0.1f);
    assert(motion_record_replay_load_replay(&controller, samples, 2U));
    assert(motion_record_replay_on_play_button_pulse(
        &controller, &pipeline, 0U));
    pipeline.target_ready = 1U;
    assert(motion_record_replay_on_play_button_pulse(
        &controller, &pipeline, 0U));
    assert(motion_record_replay_mode(&controller) == MOTION_RR_LIVE);
    assert(!pipeline.command_valid && !pipeline.target_ready);
    assert(!pipeline.unwrap.has_reference);

    set_target_from_robot_sample(&pipeline, &samples[1]);
    retargets = pipeline.retargets;
    assert(agent2_run(&pipeline));
    assert(pipeline.retargets == retargets + 1U);

    /* PLAYING + PLAY pulse uses the last successful replay command. */
    init_context(&pipeline, &initial);
    start_and_align(&controller, &pipeline, samples, 2U);
    assert(controller_tick(&controller, &pipeline, 0U));
    pipeline.target_ready = 1U;
    assert(motion_record_replay_on_play_button_pulse(
        &controller, &pipeline, 0U));
    assert(motion_record_replay_mode(&controller) == MOTION_RR_LIVE);
    assert(!pipeline.target_ready && !pipeline.command_valid);
    assert_command_sample(&pipeline.output, &samples[0]);

    /* Error HOLD + PLAY releases HOLD. REC is a no-op and preserves reason. */
    init_context(&pipeline, &initial);
    start_and_align(&controller, &pipeline, samples, 2U);
    assert(!controller_tick(&controller, &pipeline, 1U));
    assert(motion_record_replay_mode(&controller) == MOTION_RR_HOLDING);
    assert(motion_record_replay_reason(&controller) ==
           MOTION_RR_REASON_TICK_OVERRUN);
    assert(!motion_record_replay_on_record_button_pulse(&controller));
    assert(motion_record_replay_reason(&controller) ==
           MOTION_RR_REASON_TICK_OVERRUN);
    {
        uint32_t ticks_before = pipeline.ticks;
        assert(controller_tick(&controller, &pipeline, 1U));
        assert(pipeline.ticks == ticks_before + 1U);
        assert(motion_record_replay_mode(&controller) == MOTION_RR_HOLDING);
        assert(motion_record_replay_reason(&controller) ==
               MOTION_RR_REASON_TICK_OVERRUN);
    }
    pipeline.target_ready = 1U;
    retargets = pipeline.retargets;
    assert(motion_record_replay_on_play_button_pulse(
        &controller, &pipeline, 1U));
    assert(motion_record_replay_mode(&controller) == MOTION_RR_LIVE);
    assert(!pipeline.target_ready && !pipeline.command_valid);
    assert(pipeline.retargets == retargets);
    set_target_from_robot_sample(&pipeline, &samples[1]);
    assert(agent2_run(&pipeline));
    assert(pipeline.retargets == retargets + 1U);

    /* PLAY cannot interrupt RECORD. REC cannot interrupt ALIGN or PLAY. */
    init_context(&pipeline, &initial);
    motion_record_replay_init(&controller);
    assert(motion_record_replay_on_record_button_pulse(&controller));
    assert(!motion_record_replay_on_play_button_pulse(
        &controller, &pipeline, 0U));
    assert(motion_record_replay_mode(&controller) == MOTION_RR_RECORDING);
    assert(motion_record_replay_reason(&controller) == MOTION_RR_REASON_BUSY);
    assert(motion_record_replay_on_record_button_pulse(&controller));

    configure(&controller, 0.1f);
    assert(motion_record_replay_load_replay(&controller, samples, 2U));
    assert(motion_record_replay_on_play_button_pulse(
        &controller, &pipeline, 0U));
    assert(!motion_record_replay_on_record_button_pulse(&controller));
    assert(motion_record_replay_mode(&controller) == MOTION_RR_ALIGNING);
    (void)drive_align(&controller, &pipeline, 0U);
    assert(!motion_record_replay_on_record_button_pulse(&controller));
    assert(motion_record_replay_mode(&controller) == MOTION_RR_PLAYING);
}

static void test_configuration_and_mode_exclusion(void)
{
    AgentPipelineContext pipeline;
    MotionRecordReplay controller;
    ForearmJointCommand initial = command(90.0f, 90.0f, 90.0f, 90.0f, 0.5f);
    MotionSample one = {90.0f, 90.0f, 90.0f, 90.0f, 0.5f};
    uint32_t ticks_before;

    init_context(&pipeline, &initial);
    motion_record_replay_init(&controller);
    assert(sizeof(MotionSample) == 20U);
    assert(motion_record_replay_load_replay(&controller, &one, 1U));
    assert(!motion_record_replay_start_play(&controller, &pipeline, 0U));
    assert(motion_record_replay_reason(&controller) == MOTION_RR_REASON_CONFIG);
    assert(strcmp(motion_record_replay_reason_name(
                      motion_record_replay_reason(&controller)),
                  "ALIGN_CONFIG_REQUIRED_REPLAY_DISABLED") == 0);
    assert(motion_record_replay_start_record(&controller));
    assert(!motion_record_replay_start_record(&controller));
    assert(!motion_record_replay_start_play(&controller, &pipeline, 0U));
    assert(motion_record_replay_stop_record(&controller));

    motion_record_replay_init(&controller);
    assert(motion_record_replay_configure_align(&controller, 0.1f, 10U));
    assert(!motion_record_replay_on_play_button_pulse(
        &controller, &pipeline, 0U));
    assert(motion_record_replay_mode(&controller) == MOTION_RR_LIVE);
    assert(motion_record_replay_reason(&controller) == MOTION_RR_REASON_EMPTY);

    /* LIVE도 platform control tick 한 번당 ctx->ticks가 정확히 한 번 센다. */
    ticks_before = pipeline.ticks;
    assert(controller_tick(&controller, &pipeline, 0U));
    assert(pipeline.ticks == ticks_before + 1U);
}

int main(void)
{
    test_record_tick_trajectory();
    test_record_failure_sources_and_buffer_full();
    test_record_timing_and_record_button();
    test_replay_loops_with_safe_return_align();
    test_invalid_samples();
    test_full_preflight_and_play_abort_conditions();
    test_decreasing_gripper_align_and_timeout_hold();
    test_record_start_while_moving_is_rejected_at_replay();
    test_record_stationary_lead_in_is_replayable();
    test_play_after_hal_failure_reseeds_from_last_success();
    test_moving_live_to_align_preserves_command_velocity();
    test_agent3_failure_preserves_pwm_and_aborts_play();
    test_agent3_command_freshness();
    test_play_button_toggle_and_button_exclusion();
    test_configuration_and_mode_exclusion();
    puts("test_motion_record_replay: PASS (50 Hz record, button pulses, loop align, hold)");
    return 0;
}
