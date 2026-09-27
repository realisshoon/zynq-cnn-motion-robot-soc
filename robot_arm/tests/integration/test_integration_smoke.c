/*
 * 통합 스모크 테스트 (호스트, mock 서보 드라이버).
 * UART 패킷 -> Agent1 -> Agent2 -> Agent3 -> servo_hal(mock 로그)까지 검증한다.
 *
 * 빌드/실행 (robot_arm/ 에서):
 *   python tests/robot_calibration/run_tests.py
 * 새 forearm 소스 목록과 -Werror 설정은 위 러너를 따른다.
 */

/* MinGW의 SEH 스택 해제 경로 대신 순수 C 점프를 쓴다.
 * 호스트 setjmp.h가 제공하는 선택지이며 GCC/UCRT의 탈출 시 충돌을 피한다. */
#if defined(__MINGW32__)
#define __USE_MINGW_SETJMP_NON_SEH
#endif

#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "integration/agent_pipeline.h"
#include "integration/input_pose.h"
#include "integration/platform.h"
#include "human_target_angle/agent1_forearm_stage.h"
#include "drivers/servo_pwm_driver.h"
#include "output_controller/servo_hal.h"
#include "robot_calibration/forearm_safety_check.h"
#include "uart_pose/uart_pose_protocol.h"

#ifdef NDEBUG
#error "이 테스트는 assert가 활성화된 빌드가 필요합니다."
#endif

enum { SCRIPT_CAPACITY = 256, CHANNELS = SERVO_COUNT, JOINTS = FOREARM_MOTION_JOINT_COUNT, POSE_POINTS = 6 };

typedef struct {
    unsigned at_ms;
    HumanPose2D pose;
    int corrupt;
    int noise;
} Frame;

typedef struct {
    uint16_t previous[CHANNELS];
    uint16_t minimum[CHANNELS];
    uint16_t maximum[CHANNELS];
    unsigned max_step[JOINTS];
} PwmTrace;

static Frame script[SCRIPT_CAPACITY];
static unsigned script_count, script_next, sim_ms, last_frame_ms;
static unsigned delivered, arrivals, due_ticks, platform_calls, input_calls;
static unsigned observed_frames, observed_valid, main_frame_ms[SCRIPT_CAPACITY];
static unsigned main_log_boot, main_log_ticks;
static int run_robot_main;
static jmp_buf main_exit;
static PoseUartParser parser;
static const float max_delta_deg[JOINTS] = {0.6f, 0.6f, 0.6f, 0.6f};

/* 통합 진입점의 호출 순서와 본문을 그대로 실행한다. */
#define main robot_main
#include "../../src/integration/main_integration.c"
#undef main

static void pwm_values(const ServoPwmCommand *pwm, uint16_t values[CHANNELS])
{
    values[0] = pwm->elbow_roll_pwm_us;
    values[1] = pwm->elbow_pitch_pwm_us;
    values[2] = pwm->wrist_pitch_pwm_us;
    values[3] = pwm->wrist_roll_pwm_us;
    values[4] = pwm->gripper_pwm_us;
}

static void joints(const ForearmJointCommand *cmd, float values[JOINTS])
{
    values[0] = cmd->elbow_roll_deg;
    values[1] = cmd->elbow_pitch_deg;
    values[2] = cmd->wrist_pitch_deg;
    values[3] = cmd->wrist_roll_deg;
}

static void command_values(const ForearmJointCommand *cmd, float values[CHANNELS])
{
    joints(cmd, values);
    values[JOINTS] = cmd->gripper_norm;
}

static void assert_same_pwm(const ServoPwmCommand *a, const ServoPwmCommand *b)
{
    uint16_t av[CHANNELS], bv[CHANNELS];
    unsigned i;
    pwm_values(a, av);
    pwm_values(b, bv);
    for (i = 0; i < CHANNELS; ++i) assert(av[i] == bv[i]);
}

static void assert_home(const AgentPipelineContext *ctx)
{
    /* agent_pipeline.c의 k_home_pose(90,70,100,90,gripper=0.7 -- PR #57, 실측
     * wrist_roll 캘리브레이션 반영). PWM은 servo_control_convert()로 실제
     * 계산해 확인한 값이다(0deg=500us, 90deg=1500us, 180deg=2500us 선형매핑,
     * uint16_t 절삭 포함). */
    static const uint16_t expected_pwm[CHANNELS] = {1500U, 1277U, 1611U, 1500U, 1900U};
    static const float expected_deg[CHANNELS] = {90.0f, 70.0f, 100.0f, 90.0f, 0.7f};
    uint16_t values[CHANNELS];
    float angles[CHANNELS];
    unsigned i;
    pwm_values(&ctx->pwm, values);
    command_values(&ctx->output, angles);
    for (i = 0; i < CHANNELS; ++i) {
        assert(values[i] == expected_pwm[i]);
        assert(angles[i] == expected_deg[i]);
    }
    assert(ctx->output.valid && ctx->servo_errors == 0U);
}

static void trace_init(PwmTrace *trace, const AgentPipelineContext *ctx)
{
    memset(trace, 0, sizeof(*trace));
    pwm_values(&ctx->pwm, trace->previous);
    memcpy(trace->minimum, trace->previous, sizeof(trace->minimum));
    memcpy(trace->maximum, trace->previous, sizeof(trace->maximum));
}

/* mock 쓰기 로그는 128건이 차면 이후 쓰기가 실패한다. 그래서 틱마다 순서를 검증하고 비운다. */
static void servo_log_clear(void)
{
    servo_pwm_driver_mock_reset();
    servo_hal_init();
}

/*
 * 쓰기 로그를 순서대로 검사한 뒤 비운다.
 * boot=1이면 부팅 순서(shadow 5개 -> UPDATE -> ENABLE), 0이면 한 틱(shadow 5개 -> UPDATE)이다.
 * expected가 NULL이면 값은 PWM 유효 범위만 확인한다.
 */
static void servo_log_check(const ServoPwmCommand *expected, int boot)
{
    static const uint32_t offsets[7] = {0x00U, 0x04U, 0x08U, 0x0CU, 0x10U, 0x1CU, 0x18U};
    const unsigned count = boot ? 7U : 6U;
    ServoPwmDriverMockWrite entry;
    uint16_t values[CHANNELS] = {0};
    unsigned i;
    assert(servo_pwm_driver_mock_get_log_count() == count);
    if (expected != NULL) pwm_values(expected, values);
    for (i = 0; i < count; ++i) {
        assert(servo_pwm_driver_mock_get_log(i, &entry) == 1);
        assert(entry.offset == offsets[i]);
        if (i < CHANNELS) {
            if (expected != NULL) assert(entry.value == values[i]);
            else assert(entry.value >= 500U && entry.value <= 2500U);
        } else {
            assert(entry.value == 1U);
        }
    }
    servo_log_clear();
}

static void checked_tick(AgentPipelineContext *ctx, PwmTrace *trace)
{
    uint16_t values[CHANNELS];
    float angles[CHANNELS];
    unsigned i;
    const uint32_t ticks_before = ctx->ticks;
    const uint32_t writes_before = ctx->servo_writes;
    assert(agent2_tick(ctx) == 1);
    assert(agent3_run(ctx) == 1);
    servo_log_check(&ctx->pwm, 0);
    assert(ctx->ticks == ticks_before + 1U);
    assert(ctx->servo_writes == writes_before + 1U);
    assert(ctx->servo_writes == ctx->ticks && ctx->servo_errors == 0U);
    pwm_values(&ctx->pwm, values);
    command_values(&ctx->output, angles);
    for (i = 0; i < CHANNELS; ++i) {
        const float expected = i < JOINTS
            ? 500.0f + angles[i] * (1000.0f / 90.0f)
            : 500.0f + angles[i] * 2000.0f;
        assert(isfinite(angles[i]));
        assert(values[i] >= 500U && values[i] <= 2500U);
        /* 정수 PWM의 버림과 부동소수점 연산 순서 차이는 1us 이내다. */
        assert(fabsf((float)values[i] - expected) <= 1.0f);
        if (i < JOINTS) {
            const unsigned step = (unsigned)abs((int)values[i] - (int)trace->previous[i]);
            const unsigned limit = (unsigned)ceilf(max_delta_deg[i] * 1000.0f / 90.0f) + 1U;
            assert(step <= limit);
            if (step > trace->max_step[i]) trace->max_step[i] = step;
        }
        if (values[i] < trace->minimum[i]) trace->minimum[i] = values[i];
        if (values[i] > trace->maximum[i]) trace->maximum[i] = values[i];
        trace->previous[i] = values[i];
    }
}

static Point2D camera_point(float x, float y)
{
    Point2D point;
    point.x = roundf(639.5f + (1108.0f / 554.0f) * (x - 319.5f));
    point.y = roundf(359.5f + (1108.0f / 554.0f) * (y - 239.5f));
    assert(point.x >= 0.0f && point.x < 1280.0f);
    assert(point.y >= 0.0f && point.y < 720.0f);
    point.valid = 1U;
    return point;
}

static HumanPose2D front_pose(float ex, float ey, float k)
{
    HumanPose2D pose;
    const float wx = ex + 40.0f + 15.0f * k;
    const float wy = ey + 60.0f - 20.0f * k;
    memset(&pose, 0, sizeof(pose));
    pose.shoulder_l = camera_point(250.0f, 190.0f);
    pose.shoulder_r = camera_point(390.0f, 190.0f);
    pose.elbow = camera_point(ex, ey);
    pose.wrist = camera_point(wx, wy);
    pose.finger1 = camera_point(wx + 20.0f, wy - 10.0f);
    pose.finger2 = camera_point(wx + 25.0f, wy + 25.0f);
    pose.valid = 1U;
    return pose;
}

static void add_frame(unsigned at_ms, HumanPose2D pose)
{
    Frame *frame;
    assert(script_count < SCRIPT_CAPACITY);
    if (script_count != 0U) assert(at_ms > script[script_count - 1U].at_ms);
    frame = &script[script_count];
    memset(frame, 0, sizeof(*frame));
    frame->at_ms = at_ms;
    pose.frame_id = UINT32_C(0x12345600) + script_count;
    frame->pose = pose;
    ++script_count;
}

static void put_u16_le(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
}

static void encode_packet(const HumanPose2D *pose, uint8_t packet[POSE_UART_PACKET_SIZE])
{
    const Point2D points[POSE_POINTS] = {pose->finger1, pose->finger2, pose->elbow,
                                    pose->wrist, pose->shoulder_l, pose->shoulder_r};
    unsigned i;
    memset(packet, 0, POSE_UART_PACKET_SIZE);
    packet[0] = 0xA5U;
    packet[1] = 0x5AU;
    packet[2] = 1U;
    packet[3] = 30U;
    for (i = 0; i < 4U; ++i) packet[4U + i] = (uint8_t)(pose->frame_id >> (8U * i));
    packet[8] = pose->valid;
    for (i = 0; i < POSE_POINTS; ++i) {
        if (points[i].valid) packet[9] |= (uint8_t)(1U << i);
        put_u16_le(&packet[10U + 4U * i], (uint16_t)points[i].x);
        put_u16_le(&packet[12U + 4U * i], (uint16_t)points[i].y);
    }
    put_u16_le(&packet[34], pose_uart_crc16_ccitt(&packet[2], 32U));
}

static void assert_decoded_pose(const HumanPose2D *actual, const HumanPose2D *expected)
{
    const Point2D a[POSE_POINTS] = {actual->finger1, actual->finger2, actual->elbow,
                                actual->wrist, actual->shoulder_l, actual->shoulder_r};
    const Point2D e[POSE_POINTS] = {expected->finger1, expected->finger2, expected->elbow,
                                expected->wrist, expected->shoulder_l, expected->shoulder_r};
    unsigned i;
    assert(actual->frame_id == expected->frame_id && actual->valid == expected->valid);
    for (i = 0; i < POSE_POINTS; ++i) {
        assert(a[i].x == e[i].x && a[i].y == e[i].y);
        assert(a[i].valid == (uint8_t)(expected->valid && e[i].valid));
    }
}

int platform_init(void)
{
    ++platform_calls;
    servo_pwm_driver_mock_reset();
    servo_hal_init();
    return 0;
}

void input_pose_init(void)
{
    ++input_calls;
    pose_uart_parser_init(&parser);
    script_next = sim_ms = last_frame_ms = delivered = arrivals = due_ticks = 0U;
    observed_frames = observed_valid = 0U;
}

int input_pose_ready(void)
{
    return script_next < script_count && script[script_next].at_ms <= sim_ms;
}

int input_pose_take(HumanPose2D *pose, float *dt_sec)
{
    static const uint8_t garbage[] = {0x00U, 0x5AU, 0xFFU, 0xA5U, 0x01U, 0xA5U};
    uint8_t packet[POSE_UART_PACKET_SIZE];
    const Frame *frame;
    unsigned i;
    int result = 0;
    if (!input_pose_ready()) return 0;
    assert(pose != NULL && dt_sec != NULL);
    frame = &script[script_next++];
    assert(sim_ms == frame->at_ms);
    ++arrivals;
    encode_packet(&frame->pose, packet);
    if (frame->corrupt) packet[10] ^= 1U;
    if (frame->noise) {
        for (i = 0; i < sizeof(garbage); ++i) {
            assert(pose_uart_parser_push(&parser, garbage[i], pose) == 0);
        }
    }
    for (i = 0; i < POSE_UART_PACKET_SIZE; ++i) {
        result = pose_uart_parser_push(&parser, packet[i], pose);
        if (i + 1U < POSE_UART_PACKET_SIZE) assert(result == 0);
    }
    if (frame->corrupt) {
        assert(result == -1);
        return 0;
    }
    assert(result == 1);
    assert_decoded_pose(pose, &frame->pose);
    *dt_sec = delivered == 0U ? 0.05f : (float)(sim_ms - last_frame_ms) / 1000.0f;
    last_frame_ms = sim_ms;
    if (run_robot_main) main_frame_ms[delivered] = sim_ms;
    ++delivered;
    return 1;
}

int platform_tick_due(void)
{
    /* robot_main 경로: 직전 틱의 서보 쓰기 순서를 검증하고 로그를 비운다(부팅 로그는 첫 호출에서 1회). */
    if (run_robot_main && servo_pwm_driver_mock_get_log_count() != 0U) {
        const int boot = servo_pwm_driver_mock_get_log_count() == 7U;
        servo_log_check(NULL, boot);
        if (boot) ++main_log_boot; else ++main_log_ticks;
    }
    /* main의 프레임 처리 직후 공개 Agent1 출력을 읽되 상태는 바꾸지 않는다. */
    if (run_robot_main && observed_frames < delivered) {
        assert(observed_frames + 1U == delivered);
        ++observed_frames;
        if (agent1_forearm_stage_output()->valid) ++observed_valid;
    }
    ++sim_ms;
    /* 250번째 틱의 Agent2/3 호출이 끝난 다음 폴링에서 탈출한다. */
    if (run_robot_main && due_ticks == 250U) longjmp(main_exit, 1);
    if (sim_ms % 20U == 0U) {
        ++due_ticks;
        return 1;
    }
    return 0;
}

static void start_pipeline(AgentPipelineContext *ctx, PwmTrace *trace)
{
    assert(platform_init() == 0);
    input_pose_init();
    assert(agent_pipeline_init(ctx) == 0);
    assert_home(ctx);
    servo_log_check(&ctx->pwm, 1);
    trace_init(trace, ctx);
}

static void advance_to(AgentPipelineContext *ctx, PwmTrace *trace, unsigned end_ms)
{
    while (sim_ms < end_ms) {
        HumanPose2D pose;
        float dt_sec;
        if (input_pose_ready() && input_pose_take(&pose, &dt_sec)) {
            agent1_run(ctx, &pose, dt_sec);
            agent2_run(ctx);
        }
        if (platform_tick_due()) checked_tick(ctx, trace);
    }
}

static void print_stats(const char *name, const AgentPipelineContext *ctx)
{
    printf("%s PASS frames=%" PRIu32 " valid=%" PRIu32 " accepted=%" PRIu32
           " rejected=%" PRIu32 " retargets=%" PRIu32 " ticks=%" PRIu32
           " writes=%" PRIu32 " errors=%" PRIu32 "\n", name, ctx->frames_in,
           ctx->targets_valid, ctx->commands_accepted, ctx->commands_rejected,
           ctx->retargets, ctx->ticks, ctx->servo_writes, ctx->servo_errors);
}

static void test_boot(void)
{
    AgentPipelineContext ctx;
    PwmTrace trace;
    script_count = 0U;
    start_pipeline(&ctx, &trace);
    assert(ctx.frames_in == 0U && ctx.ticks == 0U && ctx.servo_writes == 0U);
    assert(!ctx.target_ready && !ctx.command_valid);
    print_stats("S1 home=k_home_pose boot_log=shadow5,UPDATE,ENABLE", &ctx);
}

static void test_uart(void)
{
    AgentPipelineContext ctx;
    PwmTrace trace;
    HumanPose2D unused_pose;
    float unused_dt;
    unsigned i;
    script_count = 0U;
    for (i = 0; i < 10U; ++i) add_frame(i * 50U, front_pose(460.0f, 250.0f, 1.0f));
    script[4].corrupt = 1;
    script[0].noise = script[5].noise = script[9].noise = 1;
    start_pipeline(&ctx, &trace);
    advance_to(&ctx, &trace, 200U);
    assert(ctx.frames_in == 4U && parser.packets_ok == 4U);
    advance_to(&ctx, &trace, 250U);
    assert(ctx.frames_in == 4U && parser.packets_ok == 4U && parser.crc_errors == 1U);
    advance_to(&ctx, &trace, 251U);
    assert(ctx.frames_in == 5U && ctx.dt_sec == 0.1f);
    advance_to(&ctx, &trace, 500U);
    assert(arrivals == 10U && delivered == 9U);
    assert(parser.packets_ok == 9U && parser.crc_errors == 1U);
    assert(parser.format_errors == 0U && parser.range_errors == 0U);
    assert(ctx.frames_in == 9U && ctx.pose.frame_id == script[9].pose.frame_id);
    assert(ctx.targets_valid == 9U && ctx.commands_accepted == 9U);
    assert(ctx.commands_rejected == 0U && ctx.retargets == 1U);
    assert(!input_pose_ready() && !input_pose_take(&unused_pose, &unused_dt));
    print_stats("S2 packets_ok=9 crc_errors=1 noise_recovery=3", &ctx);
}

static void test_front_main(void)
{
    AgentPipelineContext ctx;
    PwmTrace trace;
    unsigned i;
    unsigned main_valid;
    script_count = 0U;
    for (i = 0; i < 100U; ++i) {
        const float phase = 6.28318530718f * (float)i / 100.0f;
        add_frame(i * 50U, front_pose(460.0f + 60.0f * sinf(phase),
                                     260.0f + 60.0f * cosf(phase), 1.0f));
    }
    platform_calls = input_calls = 0U;
    main_log_boot = main_log_ticks = 0U;
    run_robot_main = 1;
    /* longjmp 뒤에 읽는 실행 중 관찰값은 모두 정적 저장소에 둔다. */
    if (setjmp(main_exit) == 0) {
        const int result = robot_main();
        fprintf(stderr, "robot_main returned unexpectedly: %d\n", result);
        assert(0);
    }
    run_robot_main = 0;
    assert(platform_calls == 1U && input_calls == 1U);
    assert(due_ticks == 250U && sim_ms == 5001U);
    assert(arrivals == 100U && delivered == 100U && observed_frames == 100U);
    assert(observed_valid == 100U && parser.packets_ok == 100U);
    assert(parser.crc_errors == 0U && parser.range_errors == 0U);
    main_valid = observed_valid;
    for (i = 0; i < 100U; ++i) assert(main_frame_ms[i] == script[i].at_ms);
    /* 실제 main 루프에서도 부팅 1회와 250틱 모두 shadow 5개 -> UPDATE 순서로 쓰였는지 로그로 확인한다. */
    assert(main_log_boot == 1U && main_log_ticks == 250U);
    printf("S3 robot_main PASS frames=%u valid=%u ticks=%u packets_ok=%" PRIu32
           " servo_log(boot=%u ticks=%u)\n",
           delivered, observed_valid, due_ticks, parser.packets_ok, main_log_boot, main_log_ticks);

    /* main의 지역 컨텍스트 대신 동일 패킷/시각을 독립 재생하여 내부를 검사한다.
     * Agent1은 전역 상태이므로 두 실행은 반드시 순차 실행하고 초기화한다. */
    start_pipeline(&ctx, &trace);
    advance_to(&ctx, &trace, 5000U);
    assert(ctx.frames_in == 100U && ctx.frames_in == delivered);
    assert(ctx.targets_valid == main_valid && ctx.targets_valid == 100U);
    assert(ctx.commands_accepted == 100U && ctx.servo_errors == 0U);
    assert(ctx.ticks == 250U && ctx.servo_writes == ctx.ticks);
    /* 호스트 재생 실측값. 네 관절 모두 움직이며 손목도 scale=1이다. */
    {
        /* motion.c(SPEED_ACCEL) + 새 home(90,70,100,90,0.7 -- PR #57, 실측
         * wrist_roll 캘리브레이션 반영) 기준 실행으로 확인한 값(never guessed). */
        const uint16_t minimum[CHANNELS] = {1500U, 1214U, 1408U, 722U, 1900U};
        const uint16_t maximum[CHANNELS] = {1637U, 1320U, 1611U, 1500U, 2500U};
        const unsigned max_step[JOINTS] = {7U, 6U, 7U, 7U};
        for (i = 0; i < CHANNELS; ++i) {
            assert(trace.minimum[i] == minimum[i] && trace.maximum[i] == maximum[i]);
            if (i < JOINTS) assert(trace.max_step[i] == max_step[i]);
        }
    }
    assert(ctx.commands_rejected == 0U && ctx.retargets == 99U);
    print_stats("S3 replay", &ctx);
    printf("S3 max_delta_us=%u,%u,%u,%u limits_us=8,8,8,8\n",
           trace.max_step[0], trace.max_step[1], trace.max_step[2],
           trace.max_step[3]);
    printf("S3 pwm_ranges=%u..%u,%u..%u,%u..%u,%u..%u,%u..%u\n",
           trace.minimum[0], trace.maximum[0], trace.minimum[1], trace.maximum[1],
           trace.minimum[2], trace.maximum[2], trace.minimum[3], trace.maximum[3],
           trace.minimum[4], trace.maximum[4]);
}

static void set_direct_target(AgentPipelineContext *ctx, float elbow_roll,
                              float wrist_pitch, float wrist_roll)
{
    const HumanForearmTarget target = {
        .elbow_roll_deg = elbow_roll, .elbow_pitch_deg = -60.0f,
        .wrist_pitch_deg = wrist_pitch, .wrist_roll_deg = wrist_roll,
        .gripper_norm = 0.25f, .valid = 1U,
        .elbow_roll_observable = 1U, .hand_fresh = 1U
    };
    ctx->target = target;
    ctx->target_ready = 1U;
    assert(agent2_run(ctx) == 1);
}

static void test_first_ramp(void)
{
    AgentPipelineContext ctx;
    PwmTrace trace;
    unsigned i;
    float first_angle;
    script_count = 0U;
    start_pipeline(&ctx, &trace);
    set_direct_target(&ctx, 30.0f, 20.0f, -30.0f);
    assert(fabsf(ctx.command.elbow_roll_deg - 120.0f) < 0.001f);
    assert_home(&ctx);
    checked_tick(&ctx, &trace);
    first_angle = ctx.output.elbow_roll_deg;
    printf("S4 first_joints_deg=%.6f,%.6f,%.6f,%.6f\n",
           ctx.output.elbow_roll_deg, ctx.output.elbow_pitch_deg,
           ctx.output.wrist_pitch_deg, ctx.output.wrist_roll_deg);
    {
        /* home(90,70,100,90)에서 SPEED_ACCEL로 첫 틱 가속 출발 -- 실행해서
         * 확인한 값(never guessed). */
        const float expected[JOINTS] = {90.047997f, 69.952003f, 100.047997f, 89.952003f};
        float actual[JOINTS];
        joints(&ctx.output, actual);
        for (i = 0; i < JOINTS; ++i) assert(fabsf(actual[i] - expected[i]) < 0.00001f);
    }
    assert(first_angle > 90.0f && first_angle <= 90.6f);
    /* At this slower profile the first fractional microsecond can quantize away. */
    assert(ctx.pwm.elbow_roll_pwm_us >= 1500U && ctx.pwm.elbow_roll_pwm_us <= 1502U);
    assert(ctx.output.gripper_norm == 0.25f && ctx.pwm.gripper_pwm_us == 1000U);
    for (i = 1U; i < 200U; ++i) checked_tick(&ctx, &trace);
    assert(fabsf(ctx.output.elbow_roll_deg - 120.0f) < 0.001f && ctx.pwm.elbow_roll_pwm_us == 1833U);
    {
        const float expected[JOINTS] = {120.0f, 30.0f, 110.0f, 60.0f};
        float output[JOINTS], command[JOINTS];
        joints(&ctx.output, output);
        joints(&ctx.command, command);
        for (i = 0; i < JOINTS; ++i) {
            assert(command[i] == expected[i]);
            assert(output[i] == command[i]);
        }
    }
    printf("S4 final_joints_deg=%.1f,%.1f,%.1f,%.1f\n",
           ctx.output.elbow_roll_deg, ctx.output.elbow_pitch_deg,
           ctx.output.wrist_pitch_deg, ctx.output.wrist_roll_deg);
    assert(trace.max_step[0] == 7U);
    printf("S4 first_elbow_roll_deg=%.6f final_elbow_roll_deg=%.1f max_elbow_roll_delta_us=%u\n",
           first_angle, ctx.output.elbow_roll_deg, trace.max_step[0]);
    print_stats("S4", &ctx);
}

/* 측면 입력 6프레임: 현재 [20,160] 범위에서는 테이블/자기충돌 없이 승인된다. */
static void test_side_accepted(void)
{
    static const float side[6][12] = {
        {533.978f,299.440f,516.981f,231.975f,488.153f,271.493f,417.723f,284.086f,395.496f,287.197f,391.758f,282.566f},
        {534.942f,295.828f,522.914f,232.741f,503.240f,275.551f,423.033f,286.616f,395.643f,288.870f,391.946f,285.292f},
        {535.207f,300.815f,530.472f,234.638f,515.028f,278.878f,440.319f,303.765f,413.518f,303.157f,413.916f,297.446f},
        {535.553f,299.481f,525.837f,238.611f,499.612f,278.934f,437.990f,295.194f,398.702f,304.526f,400.541f,298.495f},
        {532.234f,296.834f,525.777f,244.764f,508.443f,310.186f,441.649f,357.462f,417.290f,391.762f,419.277f,390.926f},
        {532.181f,298.331f,529.262f,241.015f,517.266f,311.772f,470.162f,360.625f,449.499f,365.877f,452.006f,361.752f}
    };
    AgentPipelineContext ctx;
    PwmTrace trace;
    unsigned i;
    script_count = 0U;
    for (i = 0; i < 6U; ++i) {
        HumanPose2D pose;
        memset(&pose, 0, sizeof(pose));
        pose.shoulder_l = camera_point(side[i][0], side[i][1]);
        pose.shoulder_r = camera_point(side[i][2], side[i][3]);
        pose.elbow = camera_point(side[i][4], side[i][5]);
        pose.wrist = camera_point(side[i][6], side[i][7]);
        pose.finger1 = camera_point(side[i][8], side[i][9]);
        pose.finger2 = camera_point(side[i][10], side[i][11]);
        pose.valid = 1U;
        add_frame(i * 50U, pose);
    }
    start_pipeline(&ctx, &trace);
    for (i = 0; i < 6U; ++i) {
        ForearmJointCommand mapped;
        ForearmSafetyCheckFlags issues = FOREARM_SAFETY_CHECK_OK;
        advance_to(&ctx, &trace, (i + 1U) * 50U);
        assert(ctx.frames_in == i + 1U && ctx.targets_valid == i + 1U);
        assert(ctx.commands_accepted == i + 1U && ctx.commands_rejected == 0U);
        assert(ctx.command_valid);
        forearm_motion_control_map_target(&ctx.target, &mapped);
        forearm_motion_control_apply_limits(&mapped);
        mapped.valid = 1U;
        assert(forearm_safety_check_apply(&mapped, &issues) == 1);
        assert(issues == FOREARM_SAFETY_CHECK_OK);
    }
    print_stats("S5 side-view accepted=6/6", &ctx);
}

static unsigned test_dropout(void)
{
    AgentPipelineContext ctx;
    PwmTrace trace;
    ServoPwmCommand held;
    uint32_t valid_before, accepted_before, rejected_before, retargets_before;
    unsigned i, first_invalid_ms = 0U;
    float output[CHANNELS], command[CHANNELS];
    const HumanPose2D good = front_pose(460.0f, 250.0f, 1.0f);
    HumanPose2D missing = good;
    /* pose_mapping의 major_all_fresh를 실패시켜 전체 타겟 HOLD를 유도한다. */
    missing.elbow.valid = 0U;
    script_count = 0U;
    /* Allow the initial 30deg/s smooth ramp to settle before testing HOLD. */
    for (i = 0; i < 100U; ++i) add_frame(i * 50U, good);
    for (i = 100U; i < 112U; ++i) add_frame(i * 50U, missing);
    for (i = 112U; i < 152U; ++i) add_frame(i * 50U, front_pose(510.0f, 300.0f, 2.0f));
    start_pipeline(&ctx, &trace);
    advance_to(&ctx, &trace, 5000U);
    assert(ctx.frames_in == 100U && ctx.commands_accepted == 100U);
    /* 정지 유지 판정의 전제: 이전에 승인된 램프가 이미 끝나 있어야 한다. */
    command_values(&ctx.output, output);
    command_values(&ctx.command, command);
    for (i = 0; i < CHANNELS; ++i) assert(output[i] == command[i]);
    held = ctx.pwm;
    valid_before = ctx.targets_valid;
    accepted_before = ctx.commands_accepted;
    rejected_before = ctx.commands_rejected;
    retargets_before = ctx.retargets;
    for (i = 1U; i <= 12U; ++i) {
        const uint32_t accepted = ctx.commands_accepted;
        const uint32_t rejected = ctx.commands_rejected;
        advance_to(&ctx, &trace, 5000U + i * 50U);
        if (!ctx.target_ready && first_invalid_ms == 0U) first_invalid_ms = i * 50U;
        if (i <= 4U) {
            assert(ctx.target_ready == 1U);
            assert(ctx.targets_valid == valid_before + i);
            assert(ctx.commands_accepted == accepted_before + i);
        }
        if (i >= 8U) {
            assert(ctx.target_ready == 0U);
            assert(ctx.commands_accepted == accepted && ctx.commands_rejected == rejected);
        }
        assert(ctx.commands_rejected == rejected_before);
        assert(ctx.retargets == retargets_before);
        assert_same_pwm(&ctx.pwm, &held);
    }
    assert(ctx.commands_accepted - accepted_before == 6U);
    printf("S6 dropout short_accepted=4 short_retargets=0 total_hold=%" PRIu32
           " first_invalid_age_ms=%u pwm_held=1\n",
           ctx.commands_accepted - accepted_before, first_invalid_ms);
    accepted_before = ctx.commands_accepted;
    advance_to(&ctx, &trace, 7600U);
    assert(ctx.target_ready == 1U && ctx.commands_accepted == accepted_before + 40U);
    assert(ctx.retargets > retargets_before);
    assert(ctx.pwm.elbow_roll_pwm_us != held.elbow_roll_pwm_us ||
           ctx.pwm.elbow_pitch_pwm_us != held.elbow_pitch_pwm_us ||
           ctx.pwm.wrist_pitch_pwm_us != held.wrist_pitch_pwm_us ||
           ctx.pwm.wrist_roll_pwm_us != held.wrist_roll_pwm_us);
    assert(ctx.frames_in == 152U && ctx.servo_errors == 0U);
    assert(ctx.targets_valid == 146U && ctx.commands_accepted == 146U);
    assert(ctx.commands_rejected == 0U && ctx.retargets == 41U);
    print_stats("S6 200ms HOLD / 400..600ms invalid / recovery", &ctx);
    printf("S6 HOLD boundary first_invalid_age_ms=%u\n", first_invalid_ms);
    return first_invalid_ms;
}

static void test_wrap(void)
{
    AgentPipelineContext ctx;
    PwmTrace trace;
    unsigned i;
    script_count = 0U;
    start_pipeline(&ctx, &trace);
    /* 좌표 대신 ctx.target에 직접 주입하여 양방향 경계 통과를 검증한다.
     * 2026-09-22: wrist_pitch=90=90도 굽힘 기준으로 바뀌면서, elbow_roll과
     * 같이 179도로 클램프(->160)하면 자기충돌(wrist_pitch>155)에 걸린다.
     * 이 시나리오는 elbow_roll/wrist_roll의 wrap 경계 통과가 핵심이라
     * wrist_pitch는 안전한 고정값(0, wrap 없음)으로 분리했다 -- wrist_pitch
     * 자체의 wrap은 test_forearm_calibration.c의 test_unwrap이 이미 다룬다. */
    set_direct_target(&ctx, 179.0f, 0.0f, -179.0f);
    assert(ctx.target.elbow_roll_deg == 179.0f);
    assert(ctx.target.wrist_pitch_deg == 0.0f && ctx.target.wrist_roll_deg == -179.0f);
    for (i = 0; i < 60U; ++i) checked_tick(&ctx, &trace);
    set_direct_target(&ctx, -179.0f, 0.0f, 179.0f);
    assert(ctx.target.elbow_roll_deg == 181.0f && ctx.unwrap.yaw_deg == 181.0f);
    assert(ctx.target.wrist_pitch_deg == 0.0f);
    assert(ctx.target.wrist_roll_deg == -181.0f && ctx.unwrap.wrist_roll_deg == -181.0f);
    assert(ctx.target.elbow_pitch_deg == -60.0f); /* pitch는 unwrap 대상이 아니다. */
    assert(ctx.retargets == 1U); /* 풀린 각도도 같은 한계값으로 클램프되므로 재계획하지 않는다. */
    printf("S7 crossing_unwrapped=%.1f,%.1f,%.1f pitch=%.1f retargets=%u\n",
           ctx.target.elbow_roll_deg, ctx.target.wrist_pitch_deg, ctx.target.wrist_roll_deg,
           ctx.target.elbow_pitch_deg, (unsigned)ctx.retargets);
    checked_tick(&ctx, &trace);
    set_direct_target(&ctx, 179.0f, 0.0f, -179.0f);
    assert(ctx.target.elbow_roll_deg == 179.0f && ctx.unwrap.yaw_deg == 179.0f);
    assert(ctx.target.wrist_pitch_deg == 0.0f);
    assert(ctx.target.wrist_roll_deg == -179.0f && ctx.unwrap.wrist_roll_deg == -179.0f);
    assert(ctx.target.elbow_pitch_deg == -60.0f && ctx.retargets == 1U);
    {
        const float expected[JOINTS] = {160.0f, 30.0f, 90.0f, 20.0f};
        float command[JOINTS];
        joints(&ctx.command, command);
        for (i = 0; i < JOINTS; ++i) assert(command[i] == expected[i]);
    }
    printf("S7 wrists_unwrapped=%.1f,%.1f command=%.1f,%.1f,%.1f,%.1f\n",
           ctx.target.wrist_pitch_deg, ctx.target.wrist_roll_deg,
           ctx.command.elbow_roll_deg, ctx.command.elbow_pitch_deg,
           ctx.command.wrist_pitch_deg, ctx.command.wrist_roll_deg);
    checked_tick(&ctx, &trace);
    assert(ctx.commands_accepted == 3U && ctx.commands_rejected == 0U);
    print_stats("S7 raw=179,-179,179 unwrapped=179,181,179", &ctx);
}

int main(void)
{
    unsigned dropout_first_invalid_ms;
    /* assert 실패 직전까지의 시나리오 결과도 호스트 출력에 남긴다. */
    setvbuf(stdout, NULL, _IONBF, 0);
    test_boot();
    test_uart();
    test_front_main();
    test_first_ramp();
    test_side_accepted();
    dropout_first_invalid_ms = test_dropout();
    test_wrap();
    /* 새 Agent1 실측: 0.05f 누적값이 HOLD 경계를 넘어 350ms에서 무효가 된다. */
    assert(dropout_first_invalid_ms == 350U);
    puts("All integration smoke scenarios PASS");
    return 0;
}
