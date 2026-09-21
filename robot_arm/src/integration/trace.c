/*
 * UART 디버그 로그(trace) 본체. 인터페이스는 integration/trace.h.
 *
 * Xilinx 헤더를 쓰지 않는다. UART와 시간은 플랫폼 경계 3개(platform_trace_*)로만 만지므로
 * 호스트 테스트가 가짜 구현을 붙여 그대로 실행할 수 있다. ROBOT_TRACE가 없으면 빈 파일이다.
 *
 * 줄 형식: 한 줄이 한 레코드이고 첫 토큰이 태그다. 쉼표로 나눈다. 값이 없으면 빈 칸이다.
 * 실수는 %f 없이 정수 연산으로 만든 고정소수점 텍스트다(각도 소수 1자리, 그리퍼 2자리, Point3D 3자리).
 * 나눗셈은 전부 상수 나눗셈이다(Cortex-A9에는 정수 나눗셈 명령이 없다).
 *
 *   A1  agent1_run 직후(프레임마다)  fid,t_ms,dur_us,dt_ms,pv,vm,rc,ov,base,sh,el,wp,wr,grip
 *   P3  A1 바로 뒤(Agent1 내부)      fid,pm,fl,age_ms, 6점 x,y,z (sl, sr, e, w, f1, f2)
 *   A2  agent2_run 직후              fid,t_ms,dur_us,st,fg, 타겟(unwrap 후) 5개, 매핑된 명령 6개
 *   TK  제어 틱마다                  tick,t_ms,dur_us, 출력 6개, PWM 6개, rem,w,er
 *   SM  1초마다                      t_ms,fr,tv,acc,rej,rt,tk,sw,se,ovr,crc,fmt,rng,ow,drop,hi
 *   EV  상태가 바뀔 때만             t_ms,code,arg
 *
 * '#'로 시작하는 줄은 컬럼 정의(스키마)다. 부팅 때와 10초마다 다시 보낸다.
 */

#include "integration/trace.h"

#ifdef ROBOT_TRACE

/* [TRACE] 빌드 로그에 남는 표시: trace를 켜면 UART 속도가 바뀐다(trace.h의 ROBOT_TRACE_UART_BAUD와 같은 값). */
#pragma message("[TRACE] ROBOT_TRACE enabled: UART runs at 921600 baud (115200 when disabled)")

#include <stddef.h>
#include <stdint.h>

#include "human_target_angle/agent1_stage.h"
#include "robot_calibration/safety_check.h"

/* ---- 설정값 ---- */
#define TRACE_RING_SIZE         8192U       /* 2의 거듭제곱이어야 한다(인덱스를 마스크로 자른다) */
#define TRACE_RING_MASK         (TRACE_RING_SIZE - 1U)
#define TRACE_LINE_MAX          320U        /* 한 줄의 최대 길이(줄 끝 "\r\n" 포함). 넘으면 그 줄을 버린다 */
#define TRACE_SM_PERIOD_MS      1000U
#define TRACE_SCHEMA_PERIOD_MS  10000U
#define TRACE_POLL_CHECK_MASK   0xFFU       /* poll 256회에 한 번만 시간을 읽어 주기 작업을 확인한다 */
#define TRACE_FX_LIMIT          2000000.0f  /* 이 크기를 넘는 실수는 inf로 찍는다 */

/* 에지 검출용 상태 */
enum { TRACE_STATE_UNKNOWN = 0, TRACE_STATE_OK = 1, TRACE_STATE_BAD = 2 };

/* 컬럼 정의. 아래 레코드 작성 코드와 항상 같이 고친다(테스트가 컬럼 수를 비교한다). */
static const char *const k_schema[] = {
    "#A1,fid,t_ms,dur_us,dt_ms,pv,vm,rc,ov,base,sh,el,wp,wr,grip",
    "#P3,fid,pm,fl,age_ms,slx,sly,slz,srx,sry,srz,ex,ey,ez,wx,wy,wz,f1x,f1y,f1z,f2x,f2y,f2z",
    "#A2,fid,t_ms,dur_us,st,fg,ub,ush,ue,uwp,uwr,cb,csh,ce,cwp,cwr,cg",
    "#TK,tick,t_ms,dur_us,b,sh,e,wp,wr,g,pb,psh,pe,pwp,pwr,pg,rem,w,er",
    "#SM,t_ms,fr,tv,acc,rej,rt,tk,sw,se,ovr,crc,fmt,rng,ow,drop,hi",
    "#EV,t_ms,code,arg"
};
#define TRACE_SCHEMA_COUNT ((unsigned)(sizeof(k_schema) / sizeof(k_schema[0])))

/* ---- 상태 ---- */
static uint8_t s_ring[TRACE_RING_SIZE];
static uint32_t s_head;          /* 다음에 쓸 위치(누적 바이트) */
static uint32_t s_tail;          /* 다음에 보낼 위치(누적 바이트) */
static uint32_t s_dropped;       /* 통째로 버린 줄 수 */
static uint32_t s_hi;            /* 최근 SM 주기 동안 링버퍼 사용량의 최대(바이트) */
static uint32_t s_poll_count;

static uint32_t s_us_last;       /* 마지막으로 읽은 마이크로초 */
static uint32_t s_us_rem;        /* 밀리초로 못 채운 마이크로초 */
static uint32_t s_ms;            /* trace_init() 이후 밀리초. 마이크로초가 한 바퀴 돌아도 이어진다 */
static uint32_t s_mark_us;       /* 실행시간 측정 시작 시각 */
static uint32_t s_next_sm_ms;
static uint32_t s_next_schema_ms;

static uint8_t s_a1_state;
static uint8_t s_a2_state;
static uint32_t s_prev_servo_writes;
static uint32_t s_prev_servo_errors;
static uint32_t s_prev_overruns;
static uint32_t s_prev_uart_errors;
static uint32_t s_prev_dropped;

/* ---- 줄 만들기 ---- */
typedef struct {
    char buf[TRACE_LINE_MAX];
    uint32_t len;
    uint8_t over;                /* 줄이 너무 길어 잘렸으면 1. 그 줄은 버린다 */
} TraceLine;

static void put_ch(TraceLine *l, char c)
{
    /* 줄 끝 "\r\n" 2바이트를 남겨 둔다. */
    if (l->len + 2U < TRACE_LINE_MAX) {
        l->buf[l->len++] = c;
    } else {
        l->over = 1U;
    }
}

static void put_str(TraceLine *l, const char *s)
{
    while (*s != '\0') put_ch(l, *s++);
}

static void put_u32(TraceLine *l, uint32_t v)
{
    char t[10];
    unsigned n = 0U;

    do {
        t[n++] = (char)('0' + (v % 10U));
        v /= 10U;
    } while (v != 0U);
    while (n != 0U) put_ch(l, t[--n]);
}

static void put_i32(TraceLine *l, int32_t v)
{
    if (v < 0) {
        put_ch(l, '-');
        put_u32(l, (uint32_t)(-(v + 1)) + 1U); /* INT32_MIN도 안전하다 */
    } else {
        put_u32(l, (uint32_t)v);
    }
}

static void put_hex(TraceLine *l, uint32_t v)
{
    char t[8];
    unsigned n = 0U;

    put_str(l, "0x");
    do {
        t[n++] = "0123456789abcdef"[v & 0xFU];
        v >>= 4;
    } while (v != 0U);
    while (n != 0U) put_ch(l, t[--n]);
}

/* 실수를 dec자리(1~3) 고정소수점 텍스트로 찍는다. NaN은 nan, 너무 큰 값은 inf. -0.0은 0.0으로 찍는다. */
static void put_fx(TraceLine *l, float v, unsigned dec)
{
    uint32_t a;
    unsigned neg = 0U;
    float y;

    if (v != v) {
        put_str(l, "nan");
        return;
    }
    if (v > TRACE_FX_LIMIT) {
        put_str(l, "inf");
        return;
    }
    if (v < -TRACE_FX_LIMIT) {
        put_str(l, "-inf");
        return;
    }

    y = v;
    if (y < 0.0f) {
        neg = 1U;
        y = -y;
    }
    if (dec == 1U) y *= 10.0f;
    else if (dec == 2U) y *= 100.0f;
    else if (dec == 3U) y *= 1000.0f;

    a = (uint32_t)(y + 0.5f); /* 0에서 먼 쪽으로 반올림 */
    if (a == 0U) neg = 0U;
    if (neg != 0U) put_ch(l, '-');

    if (dec == 1U) {
        put_u32(l, a / 10U);
        put_ch(l, '.');
        put_ch(l, (char)('0' + (a % 10U)));
    } else if (dec == 2U) {
        put_u32(l, a / 100U);
        put_ch(l, '.');
        put_ch(l, (char)('0' + ((a / 10U) % 10U)));
        put_ch(l, (char)('0' + (a % 10U)));
    } else if (dec == 3U) {
        put_u32(l, a / 1000U);
        put_ch(l, '.');
        put_ch(l, (char)('0' + ((a / 100U) % 10U)));
        put_ch(l, (char)('0' + ((a / 10U) % 10U)));
        put_ch(l, (char)('0' + (a % 10U)));
    } else {
        put_u32(l, a);
    }
}

/* 필드 = 쉼표 + 값 */
static void f_u32(TraceLine *l, uint32_t v)            { put_ch(l, ','); put_u32(l, v); }
static void f_i32(TraceLine *l, int32_t v)             { put_ch(l, ','); put_i32(l, v); }
static void f_hex(TraceLine *l, uint32_t v)            { put_ch(l, ','); put_hex(l, v); }
static void f_str(TraceLine *l, const char *s)         { put_ch(l, ','); put_str(l, s); }
static void f_fx(TraceLine *l, float v, unsigned dec)  { put_ch(l, ','); put_fx(l, v, dec); }
static void f_empty(TraceLine *l, unsigned n)          { while (n-- != 0U) put_ch(l, ','); }

static void line_begin(TraceLine *l, const char *tag)
{
    l->len = 0U;
    l->over = 0U;
    put_str(l, tag);
}

/* ---- 링버퍼 ---- */

/* 줄 하나를 통째로 넣는다. 자리가 모자라면 넣지 않고 drop을 센다(줄이 반쪽만 들어가는 일은 없다). */
static void ring_put(const char *data, uint32_t len)
{
    uint32_t used = s_head - s_tail;
    uint32_t i;

    if (len > TRACE_RING_SIZE - used) {
        s_dropped++;
        return;
    }
    for (i = 0U; i < len; ++i) {
        s_ring[(s_head + i) & TRACE_RING_MASK] = (uint8_t)data[i];
    }
    s_head += len;
    used += len;
    if (used > s_hi) s_hi = used;
}

static void line_end(TraceLine *l)
{
    if (l->over != 0U) {
        s_dropped++;
        return;
    }
    l->buf[l->len++] = '\r'; /* put_ch가 2바이트를 남겨 뒀다 */
    l->buf[l->len++] = '\n';
    ring_put(l->buf, l->len);
}

/* 링버퍼의 연속 구간을 TX FIFO에 들어가는 만큼만 보낸다. 기다리지 않는다. */
static void pump(void)
{
    uint32_t used = s_head - s_tail;
    uint32_t idx;
    uint32_t chunk;

    if (used == 0U) return;
    idx = s_tail & TRACE_RING_MASK;
    chunk = TRACE_RING_SIZE - idx;
    if (chunk > used) chunk = used;
    s_tail += platform_trace_tx(&s_ring[idx], chunk);
}

/* ---- 시간 ---- */

/* 마이크로초(32비트, 한 바퀴 돎)로 밀리초를 이어서 센다. 71분보다 자주 불러야 한다. */
static uint32_t ms_update(uint32_t us_now)
{
    uint32_t delta = us_now - s_us_last;

    s_us_last = us_now;
    s_ms += delta / 1000U;
    s_us_rem += delta % 1000U;
    if (s_us_rem >= 1000U) {
        s_us_rem -= 1000U;
        s_ms++;
    }
    return s_ms;
}

static uint32_t sec_to_ms(float sec)
{
    if (!(sec > 0.0f)) return 0U; /* 0 이하와 NaN */
    if (sec > 4000000.0f) sec = 4000000.0f;
    return (uint32_t)(sec * 1000.0f + 0.5f);
}

static uint32_t bit_of(uint8_t valid, unsigned pos)
{
    return (valid != 0U) ? (1U << pos) : 0U;
}

/* ---- 스키마, 이벤트 ---- */

static void emit_schema(void)
{
    TraceLine l;
    unsigned i;

    for (i = 0U; i < TRACE_SCHEMA_COUNT; ++i) {
        line_begin(&l, k_schema[i]);
        line_end(&l);
    }
}

static void emit_ev(uint32_t t_ms, const char *code, uint32_t arg)
{
    TraceLine l;

    line_begin(&l, "EV");
    f_u32(&l, t_ms);
    f_str(&l, code);
    f_u32(&l, arg);
    line_end(&l);
}

/* ---- 레코드 ---- */

static void emit_a1_line(const AgentPipelineContext *ctx, uint32_t t_ms, uint32_t dur)
{
    TraceLine l;
    uint32_t vm;

    vm = bit_of(ctx->pose.finger1.valid, 0U) | bit_of(ctx->pose.finger2.valid, 1U) |
         bit_of(ctx->pose.elbow.valid, 2U) | bit_of(ctx->pose.wrist.valid, 3U) |
         bit_of(ctx->pose.shoulder_l.valid, 4U) | bit_of(ctx->pose.shoulder_r.valid, 5U);

    line_begin(&l, "A1");
    f_u32(&l, ctx->pose.frame_id);
    f_u32(&l, t_ms);
    f_u32(&l, dur);
    f_u32(&l, sec_to_ms(ctx->dt_sec));
    f_u32(&l, ctx->pose.valid);
    f_u32(&l, vm);
    f_i32(&l, ctx->a1_rc);
    f_u32(&l, ctx->target_ready);
    if (ctx->target_ready != 0U) {
        f_fx(&l, ctx->target.base_deg, 1U);
        f_fx(&l, ctx->target.shoulder_deg, 1U);
        f_fx(&l, ctx->target.elbow_deg, 1U);
        f_fx(&l, ctx->target.wrist_pitch_deg, 1U);
        f_fx(&l, ctx->target.wrist_roll_deg, 1U);
        f_fx(&l, ctx->target.gripper_norm, 2U);
    } else {
        f_empty(&l, 6U);
    }
    line_end(&l);
}

static void put_point3(TraceLine *l, const Point3D *p)
{
    f_fx(l, p->x, 3U);
    f_fx(l, p->y, 3U);
    f_fx(l, p->z, 3U);
}

/* Agent1 내부의 Point3D 6점. agent1_stage_debug_context()가 돌려주는 const 포인터로만 읽는다. */
static void emit_p3_line(uint32_t fid)
{
    const PoseMappingContext *c = agent1_stage_debug_context();
    TraceLine l;
    uint32_t pm;
    uint32_t fl;

    if (c == NULL) return;

    pm = bit_of(c->shoulder_l_3d.valid, 0U) | bit_of(c->shoulder_r_3d.valid, 1U) |
         bit_of(c->elbow_3d.valid, 2U) | bit_of(c->wrist_3d.valid, 3U) |
         bit_of(c->finger1_3d.valid, 4U) | bit_of(c->finger2_3d.valid, 5U);
    fl = bit_of(c->major_pose3d_valid, 0U) | bit_of(c->finger_pose3d_valid, 1U) |
         bit_of(c->body_frame_valid, 2U);

    line_begin(&l, "P3");
    f_u32(&l, fid);
    f_u32(&l, pm);
    f_u32(&l, fl);
    f_u32(&l, sec_to_ms(c->target_age_sec)); /* 0이면 이번 프레임에 새로 계산, 0보다 크면 HOLD 경과 시간 */
    put_point3(&l, &c->shoulder_l_3d);
    put_point3(&l, &c->shoulder_r_3d);
    put_point3(&l, &c->elbow_3d);
    put_point3(&l, &c->wrist_3d);
    put_point3(&l, &c->finger1_3d);
    put_point3(&l, &c->finger2_3d);
    line_end(&l);
}

void trace_a1(const AgentPipelineContext *ctx)
{
    uint32_t us_now;
    uint32_t t_ms;

    if (ctx == NULL) return;
    us_now = platform_trace_time_us();
    t_ms = ms_update(us_now);

    emit_a1_line(ctx, t_ms, us_now - s_mark_us);
    emit_p3_line(ctx->pose.frame_id);

    /* Agent1이 유효한 타겟을 내는지 바뀔 때만 이벤트로 남긴다. */
    if (ctx->target_ready != 0U) {
        if (s_a1_state == TRACE_STATE_BAD) emit_ev(t_ms, "A1_BACK", ctx->pose.frame_id);
        s_a1_state = TRACE_STATE_OK;
    } else {
        if (s_a1_state != TRACE_STATE_BAD) emit_ev(t_ms, "A1_LOST", ctx->pose.frame_id);
        s_a1_state = TRACE_STATE_BAD;
    }

    s_mark_us = platform_trace_time_us(); /* 다음(agent2_run) 측정은 로그 만드는 시간을 빼고 시작한다 */
}

static void emit_a2_line(const AgentPipelineContext *ctx, uint32_t t_ms, uint32_t dur,
                         const char *st, uint32_t flags, unsigned has_cmd)
{
    TraceLine l;

    line_begin(&l, "A2");
    f_u32(&l, ctx->pose.frame_id);
    f_u32(&l, t_ms);
    f_u32(&l, dur);
    f_str(&l, st);
    f_hex(&l, flags);
    if (has_cmd != 0U) {
        /* unwrap이 제자리에서 고친 뒤의 타겟 */
        f_fx(&l, ctx->target.base_deg, 1U);
        f_fx(&l, ctx->target.shoulder_deg, 1U);
        f_fx(&l, ctx->target.elbow_deg, 1U);
        f_fx(&l, ctx->target.wrist_pitch_deg, 1U);
        f_fx(&l, ctx->target.wrist_roll_deg, 1U);
        /* 이번 프레임에 매핑된 명령(거부됐으면 거부된 값) */
        f_fx(&l, ctx->a2_mapped.base_deg, 1U);
        f_fx(&l, ctx->a2_mapped.shoulder_deg, 1U);
        f_fx(&l, ctx->a2_mapped.elbow_deg, 1U);
        f_fx(&l, ctx->a2_mapped.wrist_pitch_deg, 1U);
        f_fx(&l, ctx->a2_mapped.wrist_roll_deg, 1U);
        f_fx(&l, ctx->a2_mapped.gripper_norm, 2U);
    } else {
        f_empty(&l, 11U);
    }
    line_end(&l);
}

void trace_a2(const AgentPipelineContext *ctx)
{
    uint32_t us_now;
    uint32_t t_ms;
    uint32_t flags = 0U;
    const char *st = "-";
    unsigned has_cmd = 0U;
    unsigned rejected = 0U;

    if (ctx == NULL) return;
    us_now = platform_trace_time_us();
    t_ms = ms_update(us_now);

    switch (ctx->a2_result) {
    case A2_RESULT_NEW:             st = "N"; has_cmd = 1U; break;
    case A2_RESULT_SAME:            st = "S"; has_cmd = 1U; break;
    case A2_RESULT_REJECT_SAFETY:   st = "R"; has_cmd = 1U; rejected = 1U; break;
    case A2_RESULT_REJECT_VALIDATE: st = "V"; rejected = 1U; break;
    default:                        break;
    }

    if (ctx->a2_result == A2_RESULT_REJECT_SAFETY) {
        /*
         * robot_calibration_apply()는 안전검사 flags를 버린다(NULL을 넘긴다).
         * 거부된 명령은 valid=0이라 그대로 넣으면 INVALID_COMMAND만 나오므로,
         * 복사본의 valid를 1로 바꿔 공개 함수 safety_check_apply()를 한 번 더 불러 사유를 얻는다.
         * safety_check.c는 상태 변수가 없어서 같은 입력이면 같은 결과다.
         */
        JointCommand copy = ctx->a2_mapped;
        SafetyCheckFlags f = SAFETY_CHECK_OK;

        copy.valid = 1U;
        (void)safety_check_apply(&copy, NULL, &f);
        flags = (uint32_t)f;
    }

    emit_a2_line(ctx, t_ms, us_now - s_mark_us, st, flags, has_cmd);

    /* 거부 상태가 바뀔 때만 이벤트로 남긴다. 이번 프레임에 실행이 없으면(-) 상태를 유지한다. */
    if (rejected != 0U) {
        if (s_a2_state != TRACE_STATE_BAD) emit_ev(t_ms, "A2_REJECT", ctx->pose.frame_id);
        s_a2_state = TRACE_STATE_BAD;
    } else if (has_cmd != 0U) {
        if (s_a2_state == TRACE_STATE_BAD) emit_ev(t_ms, "A2_BACK", ctx->pose.frame_id);
        s_a2_state = TRACE_STATE_OK;
    }
}

/* 램프가 목표까지 남은 각도(관절 중 가장 큰 값) */
static float ramp_remaining(const RobotMotionState *m)
{
    float worst = 0.0f;
    unsigned i;

    for (i = 0U; i < (unsigned)ROBOT_MOTION_JOINT_COUNT; ++i) {
        float d = m->target[i] - m->current[i];

        if (d < 0.0f) d = -d;
        if (d > worst) worst = d;
    }
    return worst;
}

void trace_tick(const AgentPipelineContext *ctx)
{
    TraceLine l;
    uint32_t us_now;
    uint32_t t_ms;

    if (ctx == NULL) return;
    us_now = platform_trace_time_us();
    t_ms = ms_update(us_now);

    line_begin(&l, "TK");
    f_u32(&l, ctx->ticks);
    f_u32(&l, t_ms);
    f_u32(&l, us_now - s_mark_us);
    if (ctx->output.valid != 0U) {
        f_fx(&l, ctx->output.base_deg, 1U);
        f_fx(&l, ctx->output.shoulder_deg, 1U);
        f_fx(&l, ctx->output.elbow_deg, 1U);
        f_fx(&l, ctx->output.wrist_pitch_deg, 1U);
        f_fx(&l, ctx->output.wrist_roll_deg, 1U);
        f_fx(&l, ctx->output.gripper_norm, 2U);
    } else {
        f_empty(&l, 6U);
    }
    f_u32(&l, ctx->pwm.base_pwm_us);
    f_u32(&l, ctx->pwm.shoulder_pwm_us);
    f_u32(&l, ctx->pwm.elbow_pwm_us);
    f_u32(&l, ctx->pwm.wrist_pitch_pwm_us);
    f_u32(&l, ctx->pwm.wrist_roll_pwm_us);
    f_u32(&l, ctx->pwm.gripper_pwm_us);
    if (ctx->motion.has_target != 0) {
        f_fx(&l, ramp_remaining(&ctx->motion), 1U);
    } else {
        f_empty(&l, 1U);
    }
    f_u32(&l, (ctx->servo_writes != s_prev_servo_writes) ? 1U : 0U); /* 이번 틱에 서보 쓰기가 성공했나 */
    f_u32(&l, (ctx->servo_errors != s_prev_servo_errors) ? 1U : 0U); /* 이번 틱에 서보 오류가 났나 */
    line_end(&l);

    if (ctx->servo_errors != s_prev_servo_errors) emit_ev(t_ms, "SERVO_ERR", ctx->servo_errors);
    s_prev_servo_writes = ctx->servo_writes;
    s_prev_servo_errors = ctx->servo_errors;
}

static void emit_sm(const AgentPipelineContext *ctx, const TracePlatformStats *ps, uint32_t t_ms)
{
    TraceLine l;

    line_begin(&l, "SM");
    f_u32(&l, t_ms);
    f_u32(&l, ctx->frames_in);
    f_u32(&l, ctx->targets_valid);
    f_u32(&l, ctx->commands_accepted);
    f_u32(&l, ctx->commands_rejected);
    f_u32(&l, ctx->retargets);
    f_u32(&l, ctx->ticks);
    f_u32(&l, ctx->servo_writes);
    f_u32(&l, ctx->servo_errors);
    f_u32(&l, ps->tick_overruns);
    f_u32(&l, ps->uart_crc_errors);
    f_u32(&l, ps->uart_format_errors);
    f_u32(&l, ps->uart_range_errors);
    f_u32(&l, ps->uart_overwritten);
    f_u32(&l, s_dropped);
    f_u32(&l, s_hi);
    line_end(&l);
    s_hi = s_head - s_tail; /* 다음 주기의 최대 사용량은 지금 쌓여 있는 양부터 다시 센다 */
}

/* 시간이 필요한 주기 작업: 스키마 재전송, SM(1초), 플랫폼/버퍼 이벤트 */
static void periodic(const AgentPipelineContext *ctx)
{
    TracePlatformStats ps;
    uint32_t t_ms = ms_update(platform_trace_time_us());
    uint32_t uart_errors;

    platform_trace_stats(&ps);

    if ((int32_t)(t_ms - s_next_schema_ms) >= 0) {
        emit_schema();
        s_next_schema_ms = t_ms + TRACE_SCHEMA_PERIOD_MS;
    }
    if ((int32_t)(t_ms - s_next_sm_ms) >= 0) {
        emit_sm(ctx, &ps, t_ms);
        s_next_sm_ms = t_ms + TRACE_SM_PERIOD_MS;
    }

    if (ps.tick_overruns != s_prev_overruns) {
        emit_ev(t_ms, "TICK_OVERRUN", ps.tick_overruns);
        s_prev_overruns = ps.tick_overruns;
    }
    uart_errors = ps.uart_crc_errors + ps.uart_format_errors + ps.uart_range_errors;
    if (uart_errors != s_prev_uart_errors) {
        emit_ev(t_ms, "UART_ERR", uart_errors);
        s_prev_uart_errors = uart_errors;
    }
    if (s_dropped != s_prev_dropped) {
        emit_ev(t_ms, "TRACE_DROP", s_dropped);
        s_prev_dropped = s_dropped; /* 이 이벤트가 버려져도 다시 이벤트를 만들지 않게 그 뒤의 값을 기준으로 한다 */
    }
}

void trace_init(void)
{
    TracePlatformStats ps;

    s_head = 0U;
    s_tail = 0U;
    s_dropped = 0U;
    s_hi = 0U;
    s_poll_count = 0U;
    s_a1_state = TRACE_STATE_UNKNOWN;
    s_a2_state = TRACE_STATE_UNKNOWN;
    s_prev_servo_writes = 0U;
    s_prev_servo_errors = 0U;
    s_prev_dropped = 0U;

    s_ms = 0U;
    s_us_rem = 0U;
    s_us_last = platform_trace_time_us();
    s_mark_us = s_us_last;
    s_next_sm_ms = TRACE_SM_PERIOD_MS;
    s_next_schema_ms = TRACE_SCHEMA_PERIOD_MS;

    /* 이 시점까지 쌓인 오류는 기준값으로만 삼고, 이후에 늘어난 것만 이벤트로 남긴다. */
    platform_trace_stats(&ps);
    s_prev_overruns = ps.tick_overruns;
    s_prev_uart_errors = ps.uart_crc_errors + ps.uart_format_errors + ps.uart_range_errors;

    emit_schema();
    emit_ev(0U, "BOOT", ROBOT_TRACE_UART_BAUD);
}

void trace_mark(void)
{
    s_mark_us = platform_trace_time_us();
}

void trace_poll(const AgentPipelineContext *ctx)
{
    pump();
    if ((++s_poll_count & TRACE_POLL_CHECK_MASK) != 0U) return;
    if (ctx != NULL) periodic(ctx);
}

#else /* !ROBOT_TRACE */

/* ROBOT_TRACE를 정의하지 않은 빌드: 빈 번역 단위 경고를 피하려는 자리표시자다. */
typedef int trace_disabled_placeholder;

#endif /* ROBOT_TRACE */
