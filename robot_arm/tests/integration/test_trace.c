/*
 * UART 디버그 로그(trace) 호스트 테스트.
 *
 * 포맷터, 링버퍼(감김/가득 참), 레코드 형식, 거부 사유 재계산, 이벤트, 시간 처리, 그리고
 * 실제 Agent1/2/3과 함께 도는 통합 흐름을 검증한다. UART와 시간은 가짜 플랫폼 경계로 대신한다.
 *
 * trace.c는 이 파일이 #include 해서 정적 함수를 직접 검사한다(따로 컴파일하지 않는다).
 * ROBOT_TRACE를 모든 파일에 줘야 AgentPipelineContext의 구조가 같아진다.
 *
 * 빌드/실행 (robot_arm/ 에서):
 *   python tests/robot_calibration/run_tests.py
 * 러너가 새 forearm 소스 목록 전체에 -DROBOT_TRACE를 적용한다.
 */

#ifndef ROBOT_TRACE
#error "test_trace.c는 -DROBOT_TRACE로 빌드해야 한다"
#endif

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "robot_config.h"
#include "drivers/servo_pwm_driver.h"
#include "output_controller/servo_hal.h"
#include "../../src/integration/trace.c"

/* ---- 가짜 플랫폼 경계 ---- */
static uint32_t g_time_us;
static TracePlatformStats g_stats;
static uint32_t g_tx_budget;
static char g_out[1 << 20];
static uint32_t g_out_len;

uint32_t platform_trace_time_us(void)
{
    return g_time_us;
}

void platform_trace_stats(TracePlatformStats *out)
{
    *out = g_stats;
}

uint32_t platform_trace_tx(const uint8_t *data, uint32_t len)
{
    uint32_t n = (len < g_tx_budget) ? len : g_tx_budget;

    assert(g_out_len + n < sizeof(g_out) - 1U);
    memcpy(g_out + g_out_len, data, n);
    g_out_len += n;
    g_out[g_out_len] = '\0';
    return n;
}

/* ---- 도우미 ---- */
static void reset_all(void)
{
    memset(&g_stats, 0, sizeof(g_stats));
    g_time_us = 0U;
    g_tx_budget = 0xFFFFFFFFU;
    g_out_len = 0U;
    g_out[0] = '\0';
    trace_init();
}

/* 링버퍼를 TX 예산이 허락하는 만큼 끝까지 비운다. */
static void drain(void)
{
    while (s_head != s_tail) {
        uint32_t before = s_tail;

        pump();
        if (s_tail == before) break;
    }
}

static unsigned count_lines(const char *prefix)
{
    const char *p = g_out;
    size_t plen = strlen(prefix);
    unsigned n = 0U;

    while (*p != '\0') {
        const char *e = strstr(p, "\r\n");

        assert(e != NULL); /* 줄이 반쪽으로 끝나면 안 된다 */
        if ((size_t)(e - p) >= plen && strncmp(p, prefix, plen) == 0) ++n;
        p = e + 2;
    }
    return n;
}

/* prefix로 시작하는 n번째 줄을 dst에 복사한다(줄 끝 제외). 없으면 0. */
static int get_line(const char *prefix, unsigned nth, char *dst, size_t cap)
{
    const char *p = g_out;
    size_t plen = strlen(prefix);
    unsigned seen = 0U;

    while (*p != '\0') {
        const char *e = strstr(p, "\r\n");
        size_t n;

        if (e == NULL) break;
        n = (size_t)(e - p);
        if (n >= plen && strncmp(p, prefix, plen) == 0) {
            if (seen == nth) {
                assert(n < cap);
                memcpy(dst, p, n);
                dst[n] = '\0';
                return 1;
            }
            ++seen;
        }
        p = e + 2;
    }
    return 0;
}

static unsigned field_count(const char *line)
{
    unsigned n = 1U;

    for (; *line != '\0'; ++line) {
        if (*line == ',') ++n;
    }
    return n;
}

/* idx번째 필드(0은 태그)를 dst에 복사한다. */
static void get_field(const char *line, unsigned idx, char *dst, size_t cap)
{
    unsigned i;
    size_t n;

    for (i = 0U; i < idx; ++i) {
        line = strchr(line, ',');
        assert(line != NULL);
        ++line;
    }
    n = strcspn(line, ",");
    assert(n < cap);
    memcpy(dst, line, n);
    dst[n] = '\0';
}

static int field_is(const char *line, unsigned idx, const char *expect)
{
    char f[64];

    get_field(line, idx, f, sizeof(f));
    return strcmp(f, expect) == 0;
}

static double field_num(const char *line, unsigned idx)
{
    char f[64];

    get_field(line, idx, f, sizeof(f));
    assert(f[0] != '\0');
    return strtod(f, NULL);
}

/* 출력의 모든 데이터 줄이 컬럼 정의(# 줄)와 같은 필드 수인지 확인한다. */
static void check_all_lines_match_schema(void)
{
    const char *p = g_out;
    unsigned i;

    assert(g_out_len == 0U || (g_out_len >= 2U && g_out[g_out_len - 2U] == '\r' && g_out[g_out_len - 1U] == '\n'));
    while (*p != '\0') {
        const char *e = strstr(p, "\r\n");
        char line[TRACE_LINE_MAX];
        char tag[8];
        size_t n = (size_t)(e - p);

        assert(e != NULL && n < sizeof(line));
        memcpy(line, p, n);
        line[n] = '\0';
        p = e + 2;
        if (line[0] == '#') continue;

        get_field(line, 0U, tag, sizeof(tag));
        for (i = 0U; i < TRACE_SCHEMA_COUNT; ++i) {
            if (strncmp(k_schema[i] + 1, tag, strlen(tag)) == 0 && k_schema[i][1 + strlen(tag)] == ',') break;
        }
        assert(i < TRACE_SCHEMA_COUNT);                       /* 모르는 태그 */
        assert(field_count(line) == field_count(k_schema[i])); /* 컬럼 수 불일치 */
    }
}

static Point2D pt(float x, float y)
{
    Point2D p;

    p.x = PM_CAMERA_CX + (PM_CAMERA_FX / 554.0f) * (x - 319.5f);
    p.y = PM_CAMERA_CY + (PM_CAMERA_FY / 554.0f) * (y - 239.5f);
    p.valid = 1U;
    return p;
}

static HumanPose2D make_pose(uint32_t fid, float wobble)
{
    HumanPose2D p;

    memset(&p, 0, sizeof(p));
    p.shoulder_l = pt(250.0f, 190.0f);
    p.shoulder_r = pt(390.0f, 190.0f);
    p.elbow = pt(445.0f, 225.0f + wobble);
    p.wrist = pt(500.0f, 255.0f + wobble);
    p.finger1 = pt(528.0f, 238.0f + wobble);
    p.finger2 = pt(535.0f, 278.0f + wobble);
    p.frame_id = fid;
    p.valid = 1U;
    return p;
}

static void pipeline_init(AgentPipelineContext *ctx)
{
    servo_pwm_driver_mock_reset();
    servo_hal_init();
    assert(agent_pipeline_init(ctx) == 0);
}

/* 현재 클램프 범위는 항상 안전하다. 정상 입력은 명시적인 4관절 fixture로 둔다. */
static HumanForearmTarget safe_target(void)
{
    const HumanForearmTarget target = {
        .elbow_roll_deg = 10.0f, .elbow_pitch_deg = -20.0f,
        .wrist_pitch_deg = 30.0f, .wrist_roll_deg = -40.0f,
        .gripper_norm = 0.5f, .valid = 1U,
        .elbow_roll_observable = 1U, .hand_fresh = 1U
    };
    return target;
}

/* ---- 1) 포맷터 ---- */
static void check_fx(float v, unsigned dec, const char *expect)
{
    TraceLine l;

    line_begin(&l, "");
    put_fx(&l, v, dec);
    l.buf[l.len] = '\0';
    if (strcmp(l.buf, expect) != 0) {
        fprintf(stderr, "put_fx(%g, %u) = \"%s\" (기대 \"%s\")\n", (double)v, dec, l.buf, expect);
        assert(0);
    }
}

static void test_formatter(void)
{
    TraceLine l;

    check_fx(0.0f, 1U, "0.0");
    check_fx(-0.04f, 1U, "0.0");     /* -0.0 방지 */
    check_fx(12.34f, 1U, "12.3");
    check_fx(12.36f, 1U, "12.4");
    check_fx(-5.0f, 1U, "-5.0");
    check_fx(0.999f, 1U, "1.0");     /* 반올림 올림 */
    check_fx(0.5f, 2U, "0.50");
    check_fx(1.0f, 2U, "1.00");
    check_fx(-0.213f, 3U, "-0.213");
    check_fx(123.456f, 3U, "123.456");
    check_fx(0.0004f, 3U, "0.000");
    check_fx(2.5e6f, 1U, "inf");     /* 너무 큰 값 */
    check_fx(-2.5e6f, 1U, "-inf");
    check_fx(NAN, 2U, "nan");

    line_begin(&l, "");
    put_u32(&l, 0U);
    put_ch(&l, ' ');
    put_u32(&l, 4294967295U);
    put_ch(&l, ' ');
    put_i32(&l, -1);
    put_ch(&l, ' ');
    put_i32(&l, (int32_t)(-2147483647 - 1));
    put_ch(&l, ' ');
    put_hex(&l, 0U);
    put_ch(&l, ' ');
    put_hex(&l, 0x10U);
    put_ch(&l, ' ');
    put_hex(&l, 0xDEADBEEFU);
    l.buf[l.len] = '\0';
    assert(strcmp(l.buf, "0 4294967295 -1 -2147483648 0x0 0x10 0xdeadbeef") == 0);

    /* 너무 긴 줄은 잘리고, 그 줄은 링에 들어가지 않는다. */
    reset_all();
    drain();
    g_out_len = 0U;
    g_out[0] = '\0';
    line_begin(&l, "X");
    while (l.over == 0U) put_str(&l, "0123456789");
    {
        uint32_t dropped_before = s_dropped;

        line_end(&l);
        assert(s_dropped == dropped_before + 1U);
    }
    drain();
    assert(g_out_len == 0U);

    assert(sec_to_ms(0.05f) == 50U);
    assert(sec_to_ms(-1.0f) == 0U);
    assert(sec_to_ms(NAN) == 0U);
    printf("  formatter OK\n");
}

/* ---- 2) 링버퍼 ---- */
static void test_ring(void)
{
    unsigned i;
    unsigned sent = 0U;
    char line[64];
    char f[32];
    uint32_t last;

    /* 감김: 8KB를 여러 번 넘겨 쓰되 조금씩 비운다. 순서와 내용이 유지돼야 한다. */
    reset_all();
    g_tx_budget = 13U; /* 한 번에 13바이트만 받는 FIFO */
    for (i = 0U; i < 900U; ++i) {
        emit_ev(i, "SEQ", i * 3U);
        ++sent;
        if ((i % 7U) == 6U) drain();
    }
    drain();
    assert(s_dropped == 0U);
    assert(s_head > TRACE_RING_SIZE); /* 실제로 감겼다 */
    assert(count_lines("EV,") == sent + 1U); /* BOOT 포함 */
    for (i = 0U; i < 900U; ++i) {
        assert(get_line("EV,", i + 1U, line, sizeof(line)));
        get_field(line, 1U, f, sizeof(f));
        assert((unsigned)atoi(f) == i);
        get_field(line, 3U, f, sizeof(f));
        assert((unsigned)atoi(f) == i * 3U);
    }

    /* 가득 참: TX가 막힌 채로 계속 쌓으면 줄 단위로 버리고 drop을 센다. 저장된 줄은 온전해야 한다. */
    reset_all();
    g_tx_budget = 0U;
    for (i = 0U; i < 1500U; ++i) emit_ev(i, "SEQ", i);
    assert(s_dropped == 998U); /* 새 스키마 길이로 실행한 포화 실측값 */
    assert(s_head - s_tail <= TRACE_RING_SIZE);
    assert(s_hi > TRACE_RING_SIZE - 40U); /* 거의 가득 찼었다 */
    g_tx_budget = 0xFFFFFFFFU;
    drain();
    assert(s_head == s_tail);
    check_all_lines_match_schema();        /* 줄이 반쪽으로 끊기지 않았다 */
    last = 0U;
    for (i = 0U; get_line("EV,", i, line, sizeof(line)); ++i) {
        get_field(line, 1U, f, sizeof(f));
        assert((uint32_t)atoi(f) >= last);  /* 순서 유지(BOOT는 0) */
        last = (uint32_t)atoi(f);
    }
    assert(i + s_dropped == 1500U + 1U);    /* 저장된 줄 + 버린 줄 = 쓴 줄(BOOT 포함) */
    printf("  ring OK (감김 확인, 버린 줄 %u)\n", (unsigned)s_dropped);
}

/* ---- 3) 부팅: 스키마와 BOOT ---- */
static void test_boot(void)
{
    char line[TRACE_LINE_MAX];
    unsigned i;

    reset_all();
    drain();
    for (i = 0U; i < TRACE_SCHEMA_COUNT; ++i) {
        const char *p = g_out;
        unsigned k;

        for (k = 0U; k < i; ++k) p = strstr(p, "\r\n") + 2; /* i번째 줄로 이동 */
        assert(strncmp(p, k_schema[i], strlen(k_schema[i])) == 0);
    }
    assert(get_line("EV,", 0U, line, sizeof(line)));
    assert(strcmp(line, "EV,0,BOOT,921600") == 0);
    printf("  boot OK (스키마 %u줄 + BOOT)\n", TRACE_SCHEMA_COUNT);
}

/* ---- 4) A1/P3: 실제 Agent1 ---- */
static void test_a1_p3(void)
{
    AgentPipelineContext ctx;
    HumanPose2D p;
    char a1[TRACE_LINE_MAX];
    char p3[TRACE_LINE_MAX];
    const PoseMappingContext *c;
    unsigned i;

    assert(agent1_forearm_stage_debug_context() != NULL);
    reset_all();
    pipeline_init(&ctx);
    trace_init();
    g_time_us = 1000U;
    trace_mark();
    g_time_us = 1250U;
    p = make_pose(42U, 0.0f);
    agent1_run(&ctx, &p, 0.05f);
    trace_a1(&ctx);
    drain();

    assert(get_line("A1,", 0U, a1, sizeof(a1)));
    assert(field_is(a1, 1U, "42"));       /* fid */
    assert(field_is(a1, 2U, "1"));        /* t_ms: 1250us -> 1ms */
    assert(field_is(a1, 3U, "250"));      /* dur_us: mark부터 */
    assert(field_is(a1, 4U, "50"));       /* dt_ms */
    assert(field_is(a1, 5U, "1"));        /* pv */
    assert(field_is(a1, 6U, "63"));       /* vm: 6점 모두 유효 */
    assert(field_is(a1, 7U, "1"));        /* rc: 새 타겟 */
    assert(field_is(a1, 8U, "1"));        /* ov */
    assert(fabs(field_num(a1, 9U) - (double)ctx.target.elbow_roll_deg) <= 0.051);
    assert(fabs(field_num(a1, 10U) - (double)ctx.target.elbow_pitch_deg) <= 0.051);
    assert(fabs(field_num(a1, 11U) - (double)ctx.target.wrist_pitch_deg) <= 0.051);
    assert(fabs(field_num(a1, 12U) - (double)ctx.target.wrist_roll_deg) <= 0.051);
    assert(strcmp(a1, "A1,42,1,250,50,1,63,1,1,63.3,-22.6,-22.5,-71.8,1.00") == 0);
    printf("  sample %s\n", a1);
    assert(fabs(field_num(a1, 13U) - (double)ctx.target.gripper_norm) <= 0.0051);

    /* P3: getter로 읽은 Point3D 값과 같다 */
    c = &agent1_forearm_stage_debug_context()->pose;
    assert(get_line("P3,", 0U, p3, sizeof(p3)));
    assert(field_is(p3, 1U, "42"));
    assert(field_is(p3, 2U, "63"));       /* pm */
    assert(field_is(p3, 3U, "7"));        /* fl: major|finger|body */
    assert(field_is(p3, 4U, "0"));        /* age_ms: 방금 계산 */
    assert(fabs(field_num(p3, 5U) - (double)c->shoulder_l_3d.x) <= 0.00051);
    assert(fabs(field_num(p3, 7U) - (double)c->shoulder_l_3d.z) <= 0.00051);
    assert(fabs(field_num(p3, 11U) - (double)c->elbow_3d.x) <= 0.00051);
    assert(fabs(field_num(p3, 22U) - (double)c->finger2_3d.z) <= 0.00051);
    check_all_lines_match_schema();

    /* 랜드마크가 끊기면 HOLD: rc=0, 값은 남고 age가 늘어난다. */
    g_out_len = 0U;
    g_out[0] = '\0';
    p = make_pose(43U, 0.0f);
    p.shoulder_l.valid = 0U;
    g_time_us = 51250U;
    trace_mark();
    agent1_run(&ctx, &p, 0.05f);
    trace_a1(&ctx);
    drain();
    assert(get_line("A1,", 0U, a1, sizeof(a1)));
    assert(field_is(a1, 6U, "47"));       /* vm: 63에서 shoulder_l(16)만 빠진다 */
    assert(field_is(a1, 7U, "0"));        /* rc: HOLD */
    assert(field_is(a1, 8U, "1"));        /* ov: 마지막 정상값을 유지 */
    assert(get_line("P3,", 0U, p3, sizeof(p3)));
    assert(field_is(p3, 4U, "50"));       /* age_ms */
    assert(count_lines("EV,") == 0U);     /* 아직 유효하므로 이벤트 없음 */

    /* 오래 끊기면 유효 타겟이 사라진다(A1_LOST). 다시 잡히면 A1_BACK. 이벤트는 바뀔 때 한 번씩만 나온다. */
    g_out_len = 0U;
    g_out[0] = '\0';
    for (i = 0U; i < 10U; ++i) {
        p = make_pose(44U + i, 0.0f);
        p.shoulder_l.valid = 0U;
        g_time_us += 50000U;
        trace_mark();
        agent1_run(&ctx, &p, 0.05f);
        trace_a1(&ctx);
    }
    p = make_pose(60U, 0.0f);
    g_time_us += 50000U;
    trace_mark();
    agent1_run(&ctx, &p, 0.05f);
    trace_a1(&ctx);
    drain();
    assert(count_lines("A1,") == 11U);
    assert(count_lines("P3,") == 11U);
    assert(count_lines("EV,") == 2U);
    assert(get_line("EV,", 0U, a1, sizeof(a1)) && field_is(a1, 2U, "A1_LOST"));
    assert(get_line("EV,", 1U, a1, sizeof(a1)) && field_is(a1, 2U, "A1_BACK") && field_is(a1, 3U, "60"));
    for (i = 0U; get_line("A1,", i, a1, sizeof(a1)); ++i) {
        if (field_is(a1, 8U, "0")) break;
    }
    assert(i < 11U);                      /* ov=0인 줄이 있고, 각도 칸은 비어 있다 */
    assert(field_is(a1, 9U, "") && field_is(a1, 13U, ""));
    check_all_lines_match_schema();
    printf("  A1/P3 OK\n");
}

/* ---- 5) A2: 결과 종류, 거부 사유, 이벤트 ---- */
static void test_a2(void)
{
    AgentPipelineContext ctx;
    HumanForearmTarget safe;
    ForearmJointCommand exp_cmd;
    ForearmJointCommand copy;
    ForearmSafetyCheckFlags expect = FOREARM_SAFETY_CHECK_OK;
    char a2[TRACE_LINE_MAX];
    char want[16];

    safe = safe_target();
    reset_all();
    pipeline_init(&ctx);
    trace_init();
    ctx.pose.frame_id = 100U;

    /* N: 새 목표 */
    ctx.target = safe;
    ctx.target_ready = 1U;
    g_time_us = 10000U;
    trace_mark();
    g_time_us = 10150U;
    assert(agent2_run(&ctx) == 1);
    trace_a2(&ctx);
    drain();
    assert(get_line("A2,", 0U, a2, sizeof(a2)));
    assert(field_is(a2, 1U, "100") && field_is(a2, 3U, "150") && field_is(a2, 4U, "N") && field_is(a2, 5U, "0x0"));
    assert(fabs(field_num(a2, 6U) - (double)ctx.target.elbow_roll_deg) <= 0.051);          /* unwrap 뒤 타겟 */
    assert(fabs(field_num(a2, 11U) - (double)ctx.a2_mapped.elbow_pitch_deg) <= 0.051);  /* 매핑된 명령 */
    assert(fabs(field_num(a2, 7U) - (double)ctx.target.elbow_pitch_deg) <= 0.051);
    assert(fabs(field_num(a2, 8U) - (double)ctx.target.wrist_pitch_deg) <= 0.051);
    assert(fabs(field_num(a2, 9U) - (double)ctx.target.wrist_roll_deg) <= 0.051);
    assert(fabs(field_num(a2, 10U) - (double)ctx.a2_mapped.elbow_roll_deg) <= 0.051);
    assert(fabs(field_num(a2, 12U) - (double)ctx.a2_mapped.wrist_pitch_deg) <= 0.051);
    assert(fabs(field_num(a2, 13U) - (double)ctx.a2_mapped.wrist_roll_deg) <= 0.051);
    assert(fabs(field_num(a2, 14U) - (double)ctx.a2_mapped.gripper_norm) <= 0.0051);
    assert(strcmp(a2, "A2,100,10,150,N,0x0,10.0,-20.0,30.0,-40.0,100.0,70.0,120.0,50.0,0.50") == 0);
    printf("  sample %s\n", a2);

    /* S: 같은 명령이면 재계획하지 않는다 */
    ctx.pose.frame_id = 101U;
    ctx.target = safe;
    assert(agent2_run(&ctx) == 1);
    trace_a2(&ctx);

    /* R 포맷/이벤트 경계 주입: [20,160] 안에서는 테이블 충돌이 불가능하다.
     * 범위 밖 명령으로 테이블 충돌 사유 재계산만 검사한다. 파이프라인에
     * 정상 입력을 넣어 거부된 것으로 취급하지 않는다. elbow_pitch=-13이면
     * wrist.z=-5.399로 테이블(-5cm) 아래다(직접 실행으로 확인). */
    ctx.pose.frame_id = 102U;
    exp_cmd = (ForearmJointCommand){
        .elbow_roll_deg = 90.0f, .elbow_pitch_deg = -13.0f,
        .wrist_pitch_deg = 90.0f, .wrist_roll_deg = 90.0f,
        .gripper_norm = 0.5f, .valid = 0U
    };
    ctx.a2_mapped = exp_cmd;
    ctx.a2_result = A2_RESULT_REJECT_SAFETY;
    assert(ctx.a2_mapped.valid == 0U);    /* 거부되면 valid만 0이고 각도는 남는다 */
    copy = exp_cmd;
    copy.valid = 1U;
    (void)forearm_safety_check_apply(&copy, &expect);
    assert(expect == FOREARM_SAFETY_CHECK_TABLE_COLLISION);
    assert(expect != FOREARM_SAFETY_CHECK_INVALID_COMMAND);
    copy.valid = 0U;
    {
        ForearmSafetyCheckFlags naive = FOREARM_SAFETY_CHECK_OK;

        (void)forearm_safety_check_apply(&copy, &naive);
        assert(naive == FOREARM_SAFETY_CHECK_INVALID_COMMAND); /* valid=0 그대로면 이 값뿐이라 복사본에서 1로 바꾼다 */
    }
    trace_a2(&ctx);
    snprintf(want, sizeof(want), "0x%x", (unsigned)expect);

    /* R 한 번 더: 같은 상태라 이벤트가 또 나오면 안 된다 */
    ctx.pose.frame_id = 103U;
    trace_a2(&ctx);

    /* V: 입력 검증 실패 */
    ctx.pose.frame_id = 104U;
    ctx.target = safe;
    ctx.target.elbow_roll_deg = NAN;
    assert(agent2_run(&ctx) == 0);
    assert(ctx.a2_result == A2_RESULT_REJECT_VALIDATE);
    trace_a2(&ctx);

    /* -: Agent1 타겟이 없어 실행하지 않음 */
    ctx.pose.frame_id = 105U;
    ctx.target_ready = 0U;
    assert(agent2_run(&ctx) == 0);
    assert(ctx.a2_result == A2_RESULT_NONE);
    trace_a2(&ctx);

    /* 다시 정상: 직전 승인 명령과 같아서 S, 그리고 A2_BACK */
    ctx.pose.frame_id = 106U;
    ctx.target = safe;
    ctx.target_ready = 1U;
    assert(agent2_run(&ctx) == 1);
    trace_a2(&ctx);
    drain();

    assert(count_lines("A2,") == 7U);
    assert(get_line("A2,", 1U, a2, sizeof(a2)) && field_is(a2, 4U, "S") && field_is(a2, 5U, "0x0"));
    assert(get_line("A2,", 2U, a2, sizeof(a2)) && field_is(a2, 4U, "R") && field_is(a2, 5U, want));
    assert(fabs(field_num(a2, 11U) - (double)exp_cmd.elbow_pitch_deg) <= 0.051);        /* 거부된 명령의 각도 */
    assert(get_line("A2,", 3U, a2, sizeof(a2)) && field_is(a2, 4U, "R") && field_is(a2, 5U, want));
    assert(get_line("A2,", 4U, a2, sizeof(a2)) && field_is(a2, 4U, "V") && field_is(a2, 5U, "0x0"));
    assert(field_is(a2, 6U, "") && field_is(a2, 14U, ""));                           /* 값 칸이 비어 있다 */
    assert(get_line("A2,", 5U, a2, sizeof(a2)) && field_is(a2, 4U, "-"));
    assert(get_line("A2,", 6U, a2, sizeof(a2)) && field_is(a2, 4U, "S"));
    assert(count_lines("EV,") == 3U);                                                 /* BOOT + A2_REJECT + A2_BACK */
    assert(get_line("EV,", 1U, a2, sizeof(a2)) && field_is(a2, 2U, "A2_REJECT") && field_is(a2, 3U, "102"));
    assert(get_line("EV,", 2U, a2, sizeof(a2)) && field_is(a2, 2U, "A2_BACK") && field_is(a2, 3U, "106"));
    check_all_lines_match_schema();
    printf("  A2 OK (거부 사유 %s)\n", want);
}

/* ---- 6) TK, SM, EV, 시간 ---- */
static void test_tick_sm_ev(void)
{
    AgentPipelineContext ctx;
    char line[TRACE_LINE_MAX];
    unsigned i;

    /* TK 한 줄 */
    reset_all();
    memset(&ctx, 0, sizeof(ctx));
    ctx.ticks = 7U;
    ctx.output.valid = 1U;
    ctx.output.elbow_roll_deg = 90.0f;
    ctx.output.elbow_pitch_deg = 45.25f;
    ctx.output.wrist_pitch_deg = -3.04f;
    ctx.output.wrist_roll_deg = 0.04f;
    ctx.output.gripper_norm = 0.5f;
    ctx.pwm.elbow_roll_pwm_us = 1500U;
    ctx.pwm.elbow_pitch_pwm_us = 1250U;
    ctx.pwm.wrist_pitch_pwm_us = 1400U;
    ctx.pwm.wrist_roll_pwm_us = 1600U;
    ctx.pwm.gripper_pwm_us = 1550U;
    ctx.motion.has_target = 1;
    ctx.motion.axes[0].target = 93.2;
    ctx.motion.axes[0].q = 90.0;
    ctx.servo_writes = 5U;
    g_time_us = 5000U;
    trace_mark();
    g_time_us = 5009U;
    trace_tick(&ctx);
    trace_tick(&ctx); /* 카운터가 그대로면 w=0 */
    ctx.servo_errors = 1U;
    trace_tick(&ctx); /* 오류가 늘면 er=1 + SERVO_ERR 이벤트 */
    drain();
    assert(get_line("TK,", 0U, line, sizeof(line)));
    assert(field_is(line, 1U, "7") && field_is(line, 2U, "5") && field_is(line, 3U, "9"));
    assert(field_is(line, 4U, "90.0") && field_is(line, 5U, "45.3") && field_is(line, 6U, "-3.0"));
    assert(field_is(line, 7U, "0.0") && field_is(line, 8U, "0.50"));
    assert(field_is(line, 9U, "1500") && field_is(line, 10U, "1250") &&
           field_is(line, 11U, "1400") && field_is(line, 12U, "1600") && field_is(line, 13U, "1550"));
    printf("  sample %s\n", line);
    assert(field_is(line, 14U, "3.2") && field_is(line, 15U, "1") && field_is(line, 16U, "0"));
    assert(get_line("TK,", 1U, line, sizeof(line)) && field_is(line, 15U, "0") && field_is(line, 16U, "0"));
    assert(get_line("TK,", 2U, line, sizeof(line)) && field_is(line, 15U, "0") && field_is(line, 16U, "1"));
    assert(get_line("EV,", 1U, line, sizeof(line)) && field_is(line, 2U, "SERVO_ERR") && field_is(line, 3U, "1"));
    ctx.output.valid = 0U;
    ctx.motion.has_target = 0;
    g_out_len = 0U;
    g_out[0] = '\0';
    trace_tick(&ctx);
    drain();
    assert(get_line("TK,", 0U, line, sizeof(line)));
    assert(field_is(line, 4U, "") && field_is(line, 8U, "") && field_is(line, 14U, ""));
    check_all_lines_match_schema();

    /* SM: 1초마다, 스키마는 10초마다 */
    reset_all();
    memset(&ctx, 0, sizeof(ctx));
    ctx.frames_in = 10U;
    ctx.targets_valid = 9U;
    ctx.commands_accepted = 8U;
    ctx.commands_rejected = 1U;
    ctx.retargets = 3U;
    ctx.ticks = 50U;
    ctx.servo_writes = 50U;
    g_time_us = 400000U;
    for (i = 0U; i < 1024U; ++i) trace_poll(&ctx);
    drain();
    assert(count_lines("SM,") == 0U);      /* 1초 전에는 없다 */
    g_time_us = 1000500U;
    for (i = 0U; i < 256U; ++i) trace_poll(&ctx);
    drain();
    assert(count_lines("SM,") == 1U);
    assert(get_line("SM,", 0U, line, sizeof(line)));
    assert(field_is(line, 1U, "1000") && field_is(line, 2U, "10") && field_is(line, 3U, "9") &&
           field_is(line, 4U, "8") && field_is(line, 5U, "1") && field_is(line, 6U, "3") &&
           field_is(line, 7U, "50") && field_is(line, 8U, "50") && field_is(line, 9U, "0"));
    assert(field_num(line, 16U) > 0.0);    /* hi: 부팅 줄이 쌓였던 만큼 */
    g_time_us = 1500000U;
    for (i = 0U; i < 256U; ++i) trace_poll(&ctx);
    drain();
    assert(count_lines("SM,") == 1U);      /* 0.5초 뒤에는 아직 없다 */
    g_time_us = 2100000U;
    for (i = 0U; i < 256U; ++i) trace_poll(&ctx);
    drain();
    assert(count_lines("SM,") == 2U);
    assert(count_lines("#A1,") == 1U);
    g_time_us = 10600000U;
    for (i = 0U; i < 256U; ++i) trace_poll(&ctx);
    drain();
    assert(count_lines("#A1,") == 2U && count_lines("#EV,") == 2U); /* 스키마 재전송 */

    /* 플랫폼 통계 이벤트: 늘었을 때만 한 번 */
    g_stats.tick_overruns = 3U;
    g_stats.uart_crc_errors = 2U;
    for (i = 0U; i < 512U; ++i) trace_poll(&ctx);
    drain();
    {
        unsigned ov = 0U;
        unsigned ue = 0U;

        for (i = 0U; get_line("EV,", i, line, sizeof(line)); ++i) {
            if (field_is(line, 2U, "TICK_OVERRUN")) { ++ov; assert(field_is(line, 3U, "3")); }
            if (field_is(line, 2U, "UART_ERR")) { ++ue; assert(field_is(line, 3U, "2")); }
        }
        assert(ov == 1U && ue == 1U);
    }

    /* 링버퍼가 차서 줄을 버리면 TRACE_DROP 이벤트 */
    g_tx_budget = 0U;
    for (i = 0U; i < 1500U; ++i) emit_ev(i, "FILL", i);
    assert(s_dropped > 0U);
    g_tx_budget = 0xFFFFFFFFU;
    drain();
    for (i = 0U; i < 256U; ++i) trace_poll(&ctx);
    drain();
    for (i = 0U; get_line("EV,", i, line, sizeof(line)); ++i) {
        if (field_is(line, 2U, "TRACE_DROP")) break;
    }
    assert(field_is(line, 2U, "TRACE_DROP"));

    /* 마이크로초 카운터가 한 바퀴 돌아도 밀리초가 이어진다 */
    memset(&g_stats, 0, sizeof(g_stats));
    g_out_len = 0U;
    g_out[0] = '\0';
    g_tx_budget = 0xFFFFFFFFU;
    g_time_us = 0xFFFFFFF0U;
    trace_init();
    g_time_us = 0x00000FF0U;               /* 0xFFFFFFF0 + 0x1000 = 4096us 뒤 */
    memset(&ctx, 0, sizeof(ctx));
    trace_tick(&ctx);
    drain();
    assert(get_line("TK,", 0U, line, sizeof(line)) && field_is(line, 2U, "4"));
    printf("  TK/SM/EV/time OK\n");
}

/* ---- 7) main과 같은 순서로 도는 통합 흐름(실제 Agent1/2/3) ---- */
static unsigned line_index(const char *prefix, uint32_t fid)
{
    const char *p = g_out;
    unsigned idx = 0U;
    char line[TRACE_LINE_MAX];
    char want[16];
    size_t plen = strlen(prefix);

    snprintf(want, sizeof(want), "%u", (unsigned)fid);
    while (*p != '\0') {
        const char *e = strstr(p, "\r\n");
        size_t n = (size_t)(e - p);

        assert(n < sizeof(line));
        memcpy(line, p, n);
        line[n] = '\0';
        if (strncmp(line, prefix, plen) == 0 && field_is(line, 1U, want)) return idx;
        ++idx;
        p = e + 2;
    }
    assert(0);
    return 0U;
}

static void test_pipeline_flow(void)
{
    AgentPipelineContext ctx;
    HumanPose2D pose;
    char line[TRACE_LINE_MAX];
    unsigned t;
    unsigned frames = 0U;
    unsigned ticks = 0U;
    unsigned lost = 0U;
    unsigned back = 0U;
    unsigned i;
    uint32_t fid = 0U;

    reset_all();
    g_tx_budget = 64U; /* TX FIFO 64바이트를 흉내 낸다 */
    pipeline_init(&ctx);
    trace_init();

    for (t = 1U; t <= 3000U; ++t) {          /* 3초, 1ms 간격 */
        g_time_us = t * 1000U;
        if ((t % 50U) == 0U) {               /* 프레임 20Hz */
            ++fid;
            ++frames;
            pose = make_pose(fid, 10.0f * sinf((float)fid * 0.3f));
            if (fid >= 20U && fid <= 32U) pose.shoulder_l.valid = 0U; /* 13프레임(0.65초) 끊김 */
            TRACE_MARK();
            agent1_run(&ctx, &pose, 0.05f);
            TRACE_A1(&ctx);
            agent2_run(&ctx);
            TRACE_A2(&ctx);
        }
        if ((t % 20U) == 0U) {               /* 제어 틱 50Hz */
            ++ticks;
            if (servo_pwm_driver_mock_get_log_count() > 100U) {
                /* mock의 쓰기 로그(128칸)가 차면 이후 쓰기가 실패한다. 실제 하드웨어에는 없는 제한이라 비워 준다. */
                servo_pwm_driver_mock_reset();
                servo_hal_init();
                (void)servo_hal_enable();
            }
            TRACE_MARK();
            agent2_tick(&ctx);
            agent3_run(&ctx);
            TRACE_TK(&ctx);
        }
        TRACE_POLL(&ctx);
    }
    g_tx_budget = 0xFFFFFFFFU;
    drain();

    assert(s_dropped == 0U);
    assert(s_hi == 323U);                    /* 새 레코드로 실행한 최대 사용량(motion.c 도입 후 재측정) */
    assert(count_lines("A1,") == frames && count_lines("P3,") == frames && count_lines("A2,") == frames);
    assert(count_lines("TK,") == ticks);
    assert(count_lines("SM,") >= 2U);
    check_all_lines_match_schema();
    for (i = 1U; i <= frames; ++i) {         /* 프레임마다 A1 -> P3 -> A2 순서 */
        unsigned a = line_index("A1,", i);
        unsigned p3 = line_index("P3,", i);
        unsigned b = line_index("A2,", i);

        assert(a < p3 && p3 < b);
    }
    for (i = 0U; i < ticks; ++i) {           /* tick 번호가 끊김 없이 이어진다 */
        assert(get_line("TK,", i, line, sizeof(line)));
        assert((unsigned)field_num(line, 1U) == i + 1U);
    }
    for (i = 0U; get_line("EV,", i, line, sizeof(line)); ++i) {
        if (field_is(line, 2U, "A1_LOST")) ++lost;
        if (field_is(line, 2U, "A1_BACK")) ++back;
    }
    assert(lost == 1U && back == 1U);        /* 끊김 구간에서 한 번, 복귀에서 한 번 */
    assert(ctx.servo_writes == 150U && ctx.servo_errors == 0U);
    printf("  pipeline flow OK (프레임 %u, 틱 %u, 최대 링 사용량 %u B)\n", frames, ticks, (unsigned)s_hi);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("test_trace:\n");
    test_formatter();
    test_ring();
    test_boot();
    test_a1_p3();
    test_a2();
    test_tick_sm_ev();
    test_pipeline_flow();
    printf("test_trace: PASS\n");
    return 0;
}
