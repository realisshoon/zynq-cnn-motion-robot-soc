

#include "app/uart/telemetry.h"


/* [TRACE] 빌드 로그에 남는 표시: trace를 켜면 UART 속도가 바뀐다(trace.h의 ROBOT_CONSOLE_BAUD와 같은 값). */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "app/robot/vision/human_target/forearm_target.h"
#include "app/robot/control/forearm_safety_check.h"

/* ---- 설정값 ---- */
#define TELEMETRY_RING_SIZE         8192U       /* 2의 거듭제곱이어야 한다(인덱스를 마스크로 자른다) */
#define TELEMETRY_RING_MASK         (TELEMETRY_RING_SIZE - 1U)
#define TELEMETRY_LINE_MAX          320U        /* 한 줄의 최대 길이(줄 끝 "\r\n" 포함). 넘으면 그 줄을 버린다 */
#define TELEMETRY_SM_PERIOD_MS      1000U
#define TELEMETRY_SCHEMA_PERIOD_MS  10000U
#define TELEMETRY_POLL_CHECK_MASK   0xFFU       /* poll 256회에 한 번만 시간을 읽어 주기 작업을 확인한다 */
#define TELEMETRY_FX_LIMIT          2000000.0f  /* 이 크기를 넘는 실수는 inf로 찍는다 */

/* 에지 검출용 상태 */
enum { TELEMETRY_STATE_UNKNOWN = 0, TELEMETRY_STATE_OK = 1, TELEMETRY_STATE_BAD = 2 };

/* 컬럼 정의. 아래 레코드 작성 코드와 항상 같이 고친다(테스트가 컬럼 수를 비교한다). */
static const char *const k_schema[] = {
    "#A1,fid,t_ms,dur_us,dt_ms,pv,vm,rc,ov,er,ep,wp,wr,grip",
    "#P3,fid,pm,fl,age_ms,slx,sly,slz,srx,sry,srz,ex,ey,ez,wx,wy,wz,f1x,f1y,f1z,f2x,f2y,f2z",
    "#A2,fid,t_ms,dur_us,st,fg,uer,uep,uwp,uwr,cer,cep,cwp,cwr,cg",
    "#TK,tick,t_ms,dur_us,er,ep,wp,wr,g,per,pep,pwp,pwr,pg,rem,w,err",
    "#SM,t_ms,fr,tv,acc,rej,rt,tk,sw,se,ovr,crc,fmt,rng,ow,drop,hi",
    "#EV,t_ms,code,arg"
    ,"#CN,fid,t_ms,seq,irq,elapsed_us,flags,overwritten"
    ,"#CAM,fid,t_ms,state,pan_us,tilt_us,pan_target_us,tilt_target_us"
    ,"#IN,fid,t_ms,slx,sly,slv,srx,sry,srv,ex,ey,ev,wx,wy,wv,f1x,f1y,f1v,f2x,f2y,f2v"
    ,"#CS,t_ms,irq,ok,error,timeout,last_us,max_us,overwritten"
    ,"#CE,fid,t_ms,error"
    ,"#RAW,t_ms,sid,seq,fid,slx,sly,sls,slv,srx,sry,srs,srv,ex,ey,es,ev,wx,wy,ws,wv,redx,redy,redv,greenx,greeny,greenv"
    ,"#PAIR,t_ms,rsid,pair,lsid,lseq,lfid,rseq,rfid,sync,gap_us,control,async,es,ws,red_s,green_s"
    ,"#PIX,rsid,pair,side,sid,seq,fid,slx,sly,slv,srx,sry,srv,ex,ey,ev,wx,wy,wv,redx,redy,redv,greenx,greeny,greenv"
    ,"#PG,rsid,pair,accepted,reason"
    ,"#RQ,rsid,pair,state,candidates,filter_epoch,l_age_us,r_age_us,gap_us"
    ,"#GR,rsid,lsid,lseq,rseq,lfid,rfid,t_ms,source,fresh,desired,command,latched,close_n,open_n,result"
    ,"#GH,rsid,lsid,lseq,rseq,t_ms,span_px,pending,cancel_n,candidate_age_ms"
};
#define TELEMETRY_SCHEMA_COUNT ((unsigned)(sizeof(k_schema) / sizeof(k_schema[0])))

/* ---- 상태 ---- */
static uint8_t s_ring[TELEMETRY_RING_SIZE];
static uint32_t s_head;          /* 다음에 쓸 위치(누적 바이트) */
static uint32_t s_tail;          /* 다음에 보낼 위치(누적 바이트) */
static uint32_t s_dropped;       /* 통째로 버린 줄 수 */
static uint32_t s_hi;            /* 최근 SM 주기 동안 링버퍼 사용량의 최대(바이트) */
static uint32_t s_poll_count;
static int s_output_enabled;
static int s_robot_output_enabled;

static uint32_t s_us_last;       /* 마지막으로 읽은 마이크로초 */
static uint32_t s_us_rem;        /* 밀리초로 못 채운 마이크로초 */
static uint32_t s_ms;            /* telemetry_init() 이후 밀리초. 마이크로초가 한 바퀴 돌아도 이어진다 */
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
static uint32_t s_cnn_irq;
static uint32_t s_cnn_ok;
static uint32_t s_cnn_error;
static uint32_t s_cnn_timeout;
static uint32_t s_cnn_last_us;
static uint32_t s_cnn_max_us;
static uint32_t s_cnn_overwritten;

/* ---- 줄 만들기 ---- */
typedef struct {
    char buf[TELEMETRY_LINE_MAX];
    uint32_t len;
    uint8_t over;                /* 줄이 너무 길어 잘렸으면 1. 그 줄은 버린다 */
    uint8_t robot;               /* CNN/카메라 출력과 독립적으로 끌 수 있다 */
} TelemetryLine;

static void put_ch(TelemetryLine *l, char c)
{
    /* 줄 끝 "\r\n" 2바이트를 남겨 둔다. */
    if (l->len + 2U < TELEMETRY_LINE_MAX) {
        l->buf[l->len++] = c;
    } else {
        l->over = 1U;
    }
}

static void put_str(TelemetryLine *l, const char *s)
{
    while (*s != '\0') put_ch(l, *s++);
}

static void put_u32(TelemetryLine *l, uint32_t v)
{
    char t[10];
    unsigned n = 0U;

    do {
        t[n++] = (char)('0' + (v % 10U));
        v /= 10U;
    } while (v != 0U);
    while (n != 0U) put_ch(l, t[--n]);
}

static void put_i32(TelemetryLine *l, int32_t v)
{
    if (v < 0) {
        put_ch(l, '-');
        put_u32(l, (uint32_t)(-(v + 1)) + 1U); /* INT32_MIN도 안전하다 */
    } else {
        put_u32(l, (uint32_t)v);
    }
}

static void put_hex(TelemetryLine *l, uint32_t v)
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
static void put_fx(TelemetryLine *l, float v, unsigned dec)
{
    uint32_t a;
    unsigned neg = 0U;
    float y;

    if (v != v) {
        put_str(l, "nan");
        return;
    }
    if (v > TELEMETRY_FX_LIMIT) {
        put_str(l, "inf");
        return;
    }
    if (v < -TELEMETRY_FX_LIMIT) {
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
static void f_u32(TelemetryLine *l, uint32_t v)            { put_ch(l, ','); put_u32(l, v); }
static void f_i32(TelemetryLine *l, int32_t v)             { put_ch(l, ','); put_i32(l, v); }
static void f_hex(TelemetryLine *l, uint32_t v)            { put_ch(l, ','); put_hex(l, v); }
static void f_str(TelemetryLine *l, const char *s)         { put_ch(l, ','); put_str(l, s); }
static void f_fx(TelemetryLine *l, float v, unsigned dec)  { put_ch(l, ','); put_fx(l, v, dec); }
static void f_empty(TelemetryLine *l, unsigned n)          { while (n-- != 0U) put_ch(l, ','); }

static int tag_is(const char *name, const char *tag)
{
    size_t len = strlen(tag);
    return strncmp(name, tag, len) == 0 &&
           (name[len] == '\0' || name[len] == ',');
}

static void line_begin(TelemetryLine *l, const char *tag)
{
    const char *name = tag[0] == '#' ? tag + 1 : tag;
    l->len = 0U;
    l->over = 0U;
    l->robot = (uint8_t)(tag_is(name, "IN") || tag_is(name, "A1") ||
                         tag_is(name, "P3") || tag_is(name, "A2") ||
                         tag_is(name, "TK") || tag_is(name, "SM") ||
                         tag_is(name, "EV"));
    put_str(l, tag);
}

/* ---- 링버퍼 ---- */

/* 줄 하나를 통째로 넣는다. 자리가 모자라면 넣지 않고 drop을 센다(줄이 반쪽만 들어가는 일은 없다). */
static void ring_put(const char *data, uint32_t len)
{
    uint32_t used = s_head - s_tail;
    uint32_t i;

    if (len > TELEMETRY_RING_SIZE - used) {
        s_dropped++;
        return;
    }
    for (i = 0U; i < len; ++i) {
        s_ring[(s_head + i) & TELEMETRY_RING_MASK] = (uint8_t)data[i];
    }
    s_head += len;
    used += len;
    if (used > s_hi) s_hi = used;
}

static void line_end(TelemetryLine *l)
{
    if (!s_output_enabled || (l->robot && !s_robot_output_enabled)) return;
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
    while (s_head != s_tail) {
        uint32_t used = s_head - s_tail;
        uint32_t idx = s_tail & TELEMETRY_RING_MASK;
        uint32_t chunk = TELEMETRY_RING_SIZE - idx;
        uint32_t sent;
        if (chunk > used) chunk = used;
        sent = platform_telemetry_tx(&s_ring[idx], chunk);
        s_tail += sent;
        if (sent != chunk) break;
    }
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
    TelemetryLine l;
    unsigned i;

    for (i = 0U; i < TELEMETRY_SCHEMA_COUNT; ++i) {
        line_begin(&l, k_schema[i]);
        line_end(&l);
    }
}

static void emit_ev(uint32_t t_ms, const char *code, uint32_t arg)
{
    TelemetryLine l;

    line_begin(&l, "EV");
    f_u32(&l, t_ms);
    f_str(&l, code);
    f_u32(&l, arg);
    line_end(&l);
}

/* ---- 레코드 ---- */

static void emit_a1_line(const RobotPipeline *ctx, uint32_t t_ms, uint32_t dur)
{
    TelemetryLine l;
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
    f_i32(&l, ctx->pose_result);
    f_u32(&l, ctx->target_ready);
    if (ctx->target_ready != 0U) {
        f_fx(&l, ctx->target.elbow_roll_deg, 1U);
        f_fx(&l, ctx->target.elbow_pitch_deg, 1U);
        f_fx(&l, ctx->target.wrist_pitch_deg, 1U);
        f_fx(&l, ctx->target.wrist_roll_deg, 1U);
        f_fx(&l, ctx->target.gripper_norm, 2U);
    } else {
        f_empty(&l, 5U);
    }
    line_end(&l);
}

static void put_point3(TelemetryLine *l, const Point3D *p)
{
    f_fx(l, p->x, 3U);
    f_fx(l, p->y, 3U);
    f_fx(l, p->z, 3U);
}

/* Agent1 내부의 Point3D 6점. forearm_target_debug_context()가 돌려주는
 * const 포인터(ForearmMappingContext)로만 읽는다. 점 자체는 그 안의
 * PoseMappingContext(.pose)가 legacy와 같은 이름으로 들고 있다. */
static void emit_p3_line(uint32_t fid)
{
    const ForearmMappingContext *fc = forearm_target_debug_context();
    const PoseMappingContext *c;
    TelemetryLine l;
    uint32_t pm;
    uint32_t fl;

    if (fc == NULL) return;
    c = &fc->pose;

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

void telemetry_pose_target(const RobotPipeline *ctx)
{
    uint32_t us_now;
    uint32_t t_ms;

    if (ctx == NULL) return;
    us_now = platform_telemetry_time_us();
    t_ms = ms_update(us_now);

    emit_a1_line(ctx, t_ms, us_now - s_mark_us);
    emit_p3_line(ctx->pose.frame_id);

    /* Agent1이 유효한 타겟을 내는지 바뀔 때만 이벤트로 남긴다. */
    if (ctx->target_ready != 0U) {
        if (s_a1_state == TELEMETRY_STATE_BAD) emit_ev(t_ms, "A1_BACK", ctx->pose.frame_id);
        s_a1_state = TELEMETRY_STATE_OK;
    } else {
        if (s_a1_state != TELEMETRY_STATE_BAD) emit_ev(t_ms, "A1_LOST", ctx->pose.frame_id);
        s_a1_state = TELEMETRY_STATE_BAD;
    }

    s_mark_us = platform_telemetry_time_us(); /* 다음(robot_motion_set_target) 측정은 로그 만드는 시간을 빼고 시작한다 */
}

static void emit_a2_line(const RobotPipeline *ctx, uint32_t t_ms, uint32_t dur,
                         const char *st, uint32_t flags, unsigned has_cmd)
{
    TelemetryLine l;

    line_begin(&l, "A2");
    f_u32(&l, ctx->pose.frame_id);
    f_u32(&l, t_ms);
    f_u32(&l, dur);
    f_str(&l, st);
    f_hex(&l, flags);
    if (has_cmd != 0U) {
        /* A2가 대표각으로 정규화한 뒤의 타겟. u* 열 이름은 로그 호환을 위해 유지한다. */
        f_fx(&l, ctx->target.elbow_roll_deg, 1U);
        f_fx(&l, ctx->target.elbow_pitch_deg, 1U);
        f_fx(&l, ctx->target.wrist_pitch_deg, 1U);
        f_fx(&l, ctx->target.wrist_roll_deg, 1U);
        /* 이번 프레임에 매핑된 명령(거부됐으면 거부된 값) */
        f_fx(&l, ctx->mapped_target.elbow_roll_deg, 1U);
        f_fx(&l, ctx->mapped_target.elbow_pitch_deg, 1U);
        f_fx(&l, ctx->mapped_target.wrist_pitch_deg, 1U);
        f_fx(&l, ctx->mapped_target.wrist_roll_deg, 1U);
        f_fx(&l, ctx->mapped_target.gripper_norm, 2U);
    } else {
        f_empty(&l, 9U);
    }
    line_end(&l);
}

void telemetry_robot_target(const RobotPipeline *ctx)
{
    uint32_t us_now;
    uint32_t t_ms;
    uint32_t flags = 0U;
    const char *st = "-";
    unsigned has_cmd = 0U;
    unsigned rejected = 0U;

    if (ctx == NULL) return;
    us_now = platform_telemetry_time_us();
    t_ms = ms_update(us_now);

    switch (ctx->target_result) {
    case ROBOT_TARGET_NEW:             st = "N"; has_cmd = 1U; break;
    case ROBOT_TARGET_SAME:            st = "S"; has_cmd = 1U; break;
    case ROBOT_TARGET_REJECT_SAFETY:   st = "R"; has_cmd = 1U; rejected = 1U; break;
    case ROBOT_TARGET_REJECT_VALIDATE: st = "V"; rejected = 1U; break;
    default:                        break;
    }

    if (ctx->target_result == ROBOT_TARGET_REJECT_SAFETY) {
        /*
         * forearm_calibration_apply()는 안전검사 flags를 버린다(NULL을 넘긴다).
         * 거부된 명령은 valid=0이라 그대로 넣으면 INVALID_COMMAND만 나오므로,
         * 복사본의 valid를 1로 바꿔 공개 함수 forearm_safety_check_apply()를
         * 한 번 더 불러 사유를 얻는다. forearm_safety_check.c는 상태 변수가
         * 없어서 같은 입력이면 같은 결과다.
         */
        ForearmJointCommand copy = ctx->mapped_target;
        ForearmSafetyCheckFlags f = FOREARM_SAFETY_CHECK_OK;

        copy.valid = 1U;
        (void)forearm_safety_check_apply(&copy, &f);
        flags = (uint32_t)f;
    }

    emit_a2_line(ctx, t_ms, us_now - s_mark_us, st, flags, has_cmd);

    /* 거부 상태가 바뀔 때만 이벤트로 남긴다. 이번 프레임에 실행이 없으면(-) 상태를 유지한다. */
    if (rejected != 0U) {
        if (s_a2_state != TELEMETRY_STATE_BAD) emit_ev(t_ms, "A2_REJECT", ctx->pose.frame_id);
        s_a2_state = TELEMETRY_STATE_BAD;
    } else if (has_cmd != 0U) {
        if (s_a2_state == TELEMETRY_STATE_BAD) emit_ev(t_ms, "A2_BACK", ctx->pose.frame_id);
        s_a2_state = TELEMETRY_STATE_OK;
    }
}

/* 램프가 목표까지 남은 각도(관절 중 가장 큰 값) */
static float ramp_remaining(const ForearmMotionState *m)
{
    float worst = 0.0f;
    unsigned i;

    for (i = 0U; i < (unsigned)FOREARM_MOTION_JOINT_COUNT; ++i) {
        float d = (float)(m->axes[i].target - m->axes[i].q);

        if (d < 0.0f) d = -d;
        if (d > worst) worst = d;
    }
    return worst;
}

void telemetry_gripper(const RobotPipeline *ctx, const StereoDepthResult *depth, int result)
{
    TelemetryLine line;
    if (ctx == NULL || depth == NULL) return;
    line_begin(&line, "GR");
    line.robot = 1U;
    f_u32(&line, depth->right_session_id);
    f_u32(&line, depth->left_session_id);
    f_u32(&line, depth->left_sequence);
    f_u32(&line, depth->right_sequence);
    f_u32(&line, depth->left_frame_id);
    f_u32(&line, depth->right_frame_id);
    f_u32(&line, ms_update(platform_telemetry_time_us()));
    f_u32(&line, depth->image_pose.gripper_2d.source);
    f_u32(&line, ctx->gripper_fresh);
    if (ctx->gripper_fresh) f_fx(&line, ctx->gripper_desired, 3U);
    else f_empty(&line, 1U);
    f_fx(&line, ctx->command_valid ? ctx->command.gripper_norm : ctx->output.gripper_norm, 3U);
    f_u32(&line, ctx->gripper_latch.latched);
    f_u32(&line, ctx->gripper_latch.close_count);
    f_u32(&line, ctx->gripper_latch.open_count);
    f_i32(&line, result);
    line_end(&line);
    line_begin(&line, "GH");
    line.robot = 1U;
    f_u32(&line, depth->right_session_id);
    f_u32(&line, depth->left_session_id);
    f_u32(&line, depth->left_sequence);
    f_u32(&line, depth->right_sequence);
    f_u32(&line, ms_update(platform_telemetry_time_us()));
    if (ctx->gripper_fresh) f_fx(&line, ctx->gripper_distance_px, 3U);
    else f_empty(&line, 1U);
    f_u32(&line, ctx->gripper_latch.candidate_active);
    f_u32(&line, ctx->gripper_latch.cancel_count);
    f_u32(&line, ctx->gripper_latch.candidate_active ?
        (platform_telemetry_time_us() - ctx->gripper_latch.candidate_started_us) / 1000U : 0U);
    line_end(&line);
    line_begin(&line, "GM");
    line.robot = 1U;
    f_u32(&line, depth->right_session_id);
    f_u32(&line, depth->left_session_id);
    f_u32(&line, ms_update(platform_telemetry_time_us()));
    f_u32(&line, ctx->gripper_motion_hold.armed);
    f_u32(&line, ctx->gripper_motion_hold.locked);
    f_u32(&line, ctx->gripper_motion_hold.moving);
    if (ctx->gripper_motion_hold.speed_valid && ctx->gripper_motion_hold.samples &&
        platform_telemetry_time_us() - ctx->gripper_motion_hold.times[ctx->gripper_motion_hold.samples - 1U] <= 100000U)
        f_fx(&line, ctx->gripper_motion_hold.speed, 2U);
    else f_empty(&line, 1U);
    f_u32(&line, ctx->gripper_motion_hold.open_count);
    f_u32(&line, ctx->gripper_motion_hold.manual_open);
    line_end(&line);
    line_begin(&line, "GO");
    line.robot = 1U;
    f_u32(&line, depth->right_session_id);
    f_u32(&line, depth->left_session_id);
    f_u32(&line, ms_update(platform_telemetry_time_us()));
    f_u32(&line, ctx->gripper_motion_hold.strong_open.pending);
    f_u32(&line, ctx->gripper_motion_hold.strong_open.count);
    f_u32(&line, ctx->gripper_motion_hold.strong_open.pending ?
        (platform_telemetry_time_us() - ctx->gripper_motion_hold.strong_open.started_us) / 1000U : 0U);
    line_end(&line);
}

void telemetry_tick(const RobotPipeline *ctx)
{
    const ForearmJointCommand *command;
    TelemetryLine l;
    uint32_t us_now;
    uint32_t t_ms;

    if (ctx == NULL) return;
    command = servo_output_is_current_tick(ctx) ? &ctx->servo_command : NULL;
    us_now = platform_telemetry_time_us();
    t_ms = ms_update(us_now);

    line_begin(&l, "TK");
    f_u32(&l, ctx->ticks);
    f_u32(&l, t_ms);
    f_u32(&l, us_now - s_mark_us);
    if (command != NULL && command->valid != 0U) {
        f_fx(&l, command->elbow_roll_deg, 1U);
        f_fx(&l, command->elbow_pitch_deg, 1U);
        f_fx(&l, command->wrist_pitch_deg, 1U);
        f_fx(&l, command->wrist_roll_deg, 1U);
        f_fx(&l, command->gripper_norm, 2U);
    } else {
        f_empty(&l, 5U);
    }
    f_u32(&l, ctx->pwm.elbow_roll_pwm_us);
    f_u32(&l, ctx->pwm.elbow_pitch_pwm_us);
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

void telemetry_cnn_frame(uint32_t frame_id, uint32_t result_seq,
                     uint32_t irq_count, uint32_t elapsed_us,
                     uint32_t joint_flags, uint32_t overwritten)
{
    TelemetryLine l;
    uint32_t t_ms = ms_update(platform_telemetry_time_us());
    ++s_cnn_ok;
    s_cnn_irq = irq_count;
    s_cnn_last_us = elapsed_us;
    if (elapsed_us > s_cnn_max_us) s_cnn_max_us = elapsed_us;
    s_cnn_overwritten = overwritten;
    line_begin(&l, "CN");
    f_u32(&l, frame_id);
    f_u32(&l, t_ms);
    f_u32(&l, result_seq);
    f_u32(&l, irq_count);
    f_u32(&l, elapsed_us);
    f_hex(&l, joint_flags);
    f_u32(&l, overwritten);
    line_end(&l);
}

void telemetry_cnn_error(uint32_t frame_id, int error, uint32_t irq_count)
{
    TelemetryLine l;
    uint32_t t_ms = ms_update(platform_telemetry_time_us());
    ++s_cnn_error;
    s_cnn_irq = irq_count;
    if (error == -14) ++s_cnn_timeout; /* CNN_ERR_TIMEOUT */
    line_begin(&l, "CE");
    f_u32(&l, frame_id);
    f_u32(&l, t_ms);
    f_i32(&l, error);
    line_end(&l);
}

void telemetry_camera(uint32_t frame_id, uint32_t state,
                  uint32_t pan_us, uint32_t tilt_us,
                  uint32_t pan_target_us, uint32_t tilt_target_us)
{
    TelemetryLine l;
    line_begin(&l, "CAM");
    f_u32(&l, frame_id);
    f_u32(&l, ms_update(platform_telemetry_time_us()));
    f_u32(&l, state);
    f_u32(&l, pan_us);
    f_u32(&l, tilt_us);
    f_u32(&l, pan_target_us);
    f_u32(&l, tilt_target_us);
    line_end(&l);
}

void telemetry_input(const HumanPose2D *pose)
{
    const Point2D *points[6];
    TelemetryLine l;
    unsigned i;
    if (pose == NULL) return;
    points[0] = &pose->shoulder_l;
    points[1] = &pose->shoulder_r;
    points[2] = &pose->elbow;
    points[3] = &pose->wrist;
    points[4] = &pose->finger1;
    points[5] = &pose->finger2;
    line_begin(&l, "IN");
    f_u32(&l, pose->frame_id);
    f_u32(&l, ms_update(platform_telemetry_time_us()));
    for (i = 0U; i < 6U; ++i) {
        f_fx(&l, points[i]->x, 1U);
        f_fx(&l, points[i]->y, 1U);
        f_u32(&l, points[i]->valid);
    }
    line_end(&l);
}

void telemetry_cnn_coordinates(const StereoCoordinateFrame *frame)
{
    static const unsigned joints[] = {5U, 6U, 8U, 10U};
    static const unsigned markers[] = {0U, 2U};
    TelemetryLine line;
    unsigned index;
    if (frame == NULL) return;
    line_begin(&line, "RAW");
    f_u32(&line, ms_update(platform_telemetry_time_us()));
    f_u32(&line, frame->session_id);
    f_u32(&line, frame->sequence);
    f_u32(&line, frame->frame_id);
    for (index = 0U; index < sizeof(joints) / sizeof(joints[0]); ++index) {
        const StereoCoordinateJoint *joint = &frame->joint[joints[index]];
        f_u32(&line, joint->x);
        f_u32(&line, joint->y);
        f_i32(&line, joint->score);
        f_u32(&line, joint->valid);
    }
    for (index = 0U; index < sizeof(markers) / sizeof(markers[0]); ++index) {
        uint32_t marker = frame->markers[markers[index]];
        f_u32(&line, marker & 0x7FFU);
        f_u32(&line, (marker >> 11) & 0x3FFU);
        f_u32(&line, marker >> 31);
    }
    line_end(&line);
}

static void trace_pair_pixels(uint32_t pair_id, const StereoDepthResult *depth,
                              unsigned side, const Point2D *points)
{
    static const unsigned selected[] = {5U, 6U, 8U, 10U, 17U, 19U};
    TelemetryLine line;
    unsigned index;
    line_begin(&line, "PIX");
    f_u32(&line, depth->right_session_id);
    f_u32(&line, pair_id);
    f_str(&line, side ? "R" : "L");
    f_u32(&line, side ? depth->right_session_id : depth->left_session_id);
    f_u32(&line, side ? depth->right_sequence : depth->left_sequence);
    f_u32(&line, side ? depth->right_frame_id : depth->left_frame_id);
    for (index = 0U; index < sizeof(selected) / sizeof(selected[0]); ++index) {
        const Point2D *point = &points[selected[index]];
        f_fx(&line, point->x, 1U);
        f_fx(&line, point->y, 1U);
        f_u32(&line, point->valid);
    }
    line_end(&line);
}

void telemetry_stereo_pair(uint32_t pair_id, const StereoDepthResult *depth,
                       const Point2D *left_points, const Point2D *right_points)
{
    static const unsigned selected[] = {8U, 10U, 17U, 19U};
    TelemetryLine line;
    unsigned index;
    uint64_t gap;
    if (depth == NULL || left_points == NULL || right_points == NULL) return;
    gap = depth->left_received_us > depth->right_received_us
        ? depth->left_received_us - depth->right_received_us
        : depth->right_received_us - depth->left_received_us;
    line_begin(&line, "PAIR");
    f_u32(&line, ms_update(platform_telemetry_time_us()));
    f_u32(&line, depth->right_session_id);
    f_u32(&line, pair_id);
    f_u32(&line, depth->left_session_id);
    f_u32(&line, depth->left_sequence);
    f_u32(&line, depth->left_frame_id);
    f_u32(&line, depth->right_sequence);
    f_u32(&line, depth->right_frame_id);
    f_u32(&line, depth->time_verified);
    f_u32(&line, gap > UINT32_MAX ? UINT32_MAX : (uint32_t)gap);
    f_i32(&line, depth->control_status);
    f_i32(&line, depth->async_status);
    for (index = 0U; index < sizeof(selected) / sizeof(selected[0]); ++index)
        f_i32(&line, depth->point_status[selected[index]]);
    line_end(&line);
    trace_pair_pixels(pair_id, depth, 0U, left_points);
    trace_pair_pixels(pair_id, depth, 1U, right_points);
    line_begin(&line, "MS");
    f_u32(&line, depth->right_session_id);
    f_u32(&line, pair_id);
    f_u32(&line, depth->arm_stationary);
    f_u32(&line, depth->arm_motion_count);
    f_fx(&line, depth->arm_motion_rms_px, 3U);
    f_fx(&line, depth->arm_motion_drift_px, 3U);
    f_fx(&line, depth->arm_motion_anchor_px, 3U);
    f_u32(&line, depth->arm_motion_trend);
    line_end(&line);
}

void telemetry_stereo_admission(uint32_t pair_id, const StereoDepthResult *depth,
                            int accepted, const char *reason)
{
    TelemetryLine line;
    if (depth == NULL || reason == NULL) return;
    line_begin(&line, "PG");
    f_u32(&line, depth->right_session_id);
    f_u32(&line, pair_id);
    f_u32(&line, accepted ? 1U : 0U);
    f_str(&line, reason);
    line_end(&line);
}

static uint32_t receipt_age(uint64_t now_us, uint64_t received_us)
{
    uint64_t age;
    if (received_us > now_us) return UINT32_MAX;
    age = now_us - received_us;
    return age > UINT32_MAX ? UINT32_MAX : (uint32_t)age;
}

void telemetry_stereo_tracking(uint32_t pair_id, const StereoDepthResult *depth,
                           const char *state, unsigned candidates, uint32_t epoch,
                           uint64_t now_us)
{
    TelemetryLine line;
    uint64_t gap;
    if (depth == NULL || state == NULL) return;
    gap = depth->left_received_us > depth->right_received_us
        ? depth->left_received_us - depth->right_received_us
        : depth->right_received_us - depth->left_received_us;
    line_begin(&line, "RQ");
    f_u32(&line, depth->right_session_id);
    f_u32(&line, pair_id);
    f_str(&line, state);
    f_u32(&line, candidates);
    f_u32(&line, epoch);
    f_u32(&line, receipt_age(now_us, depth->left_received_us));
    f_u32(&line, receipt_age(now_us, depth->right_received_us));
    f_u32(&line, gap > UINT32_MAX ? UINT32_MAX : (uint32_t)gap);
    line_end(&line);
}

static void emit_cs(uint32_t t_ms)
{
    TelemetryLine l;
    line_begin(&l, "CS");
    f_u32(&l, t_ms);
    f_u32(&l, s_cnn_irq);
    f_u32(&l, s_cnn_ok);
    f_u32(&l, s_cnn_error);
    f_u32(&l, s_cnn_timeout);
    f_u32(&l, s_cnn_last_us);
    f_u32(&l, s_cnn_max_us);
    f_u32(&l, s_cnn_overwritten);
    line_end(&l);
}

static void emit_sm(const RobotPipeline *ctx, const TelemetryPlatformStats *ps, uint32_t t_ms)
{
    TelemetryLine l;

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
static void periodic(const RobotPipeline *ctx)
{
    TelemetryPlatformStats ps;
    uint32_t t_ms = ms_update(platform_telemetry_time_us());
    uint32_t uart_errors;

    platform_telemetry_stats(&ps);

    if ((int32_t)(t_ms - s_next_schema_ms) >= 0) {
        emit_schema();
        s_next_schema_ms = t_ms + TELEMETRY_SCHEMA_PERIOD_MS;
    }
    if ((int32_t)(t_ms - s_next_sm_ms) >= 0) {
        emit_sm(ctx, &ps, t_ms);
        emit_cs(t_ms);
        s_next_sm_ms = t_ms + TELEMETRY_SM_PERIOD_MS;
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

void telemetry_init(void)
{
    TelemetryPlatformStats ps;

    s_head = 0U;
    s_tail = 0U;
    s_dropped = 0U;
    s_hi = 0U;
    s_poll_count = 0U;
    s_output_enabled = 1;
    s_robot_output_enabled = 1;
    s_a1_state = TELEMETRY_STATE_UNKNOWN;
    s_a2_state = TELEMETRY_STATE_UNKNOWN;
    s_prev_servo_writes = 0U;
    s_prev_servo_errors = 0U;
    s_prev_dropped = 0U;
    s_cnn_irq = 0U;
    s_cnn_ok = 0U;
    s_cnn_error = 0U;
    s_cnn_timeout = 0U;
    s_cnn_last_us = 0U;
    s_cnn_max_us = 0U;
    s_cnn_overwritten = 0U;

    s_ms = 0U;
    s_us_rem = 0U;
    s_us_last = platform_telemetry_time_us();
    s_mark_us = s_us_last;
    s_next_sm_ms = TELEMETRY_SM_PERIOD_MS;
    s_next_schema_ms = TELEMETRY_SCHEMA_PERIOD_MS;

    /* 이 시점까지 쌓인 오류는 기준값으로만 삼고, 이후에 늘어난 것만 이벤트로 남긴다. */
    platform_telemetry_stats(&ps);
    s_prev_overruns = ps.tick_overruns;
    s_prev_uart_errors = ps.uart_crc_errors + ps.uart_format_errors + ps.uart_range_errors;

    emit_schema();
    emit_ev(0U, "BOOT", ROBOT_CONSOLE_BAUD);
}

void telemetry_set_output_enabled(int enabled)
{
    TelemetryPlatformStats ps;
    uint32_t t_ms;

    if (!enabled) {
        s_output_enabled = 0;
        s_tail = s_head;
        s_hi = 0U;
        return;
    }
    if (s_output_enabled) return;
    s_output_enabled = 1;
    t_ms = ms_update(platform_telemetry_time_us());
    platform_telemetry_stats(&ps);
    s_prev_overruns = ps.tick_overruns;
    s_prev_uart_errors = ps.uart_crc_errors + ps.uart_format_errors +
                         ps.uart_range_errors;
    s_prev_dropped = s_dropped;
    s_next_sm_ms = t_ms + TELEMETRY_SM_PERIOD_MS;
    s_next_schema_ms = t_ms + TELEMETRY_SCHEMA_PERIOD_MS;
    emit_schema();
    emit_ev(t_ms, "TRACE_ON", 0U);
}

void telemetry_set_robot_output_enabled(int enabled)
{
    int next = enabled ? 1 : 0;
    if (next == s_robot_output_enabled) return;
    s_robot_output_enabled = next;
    if (next) emit_schema();
}

int telemetry_robot_output_enabled(void)
{
    return s_robot_output_enabled;
}

void telemetry_mark(void)
{
    s_mark_us = platform_telemetry_time_us();
}

void telemetry_poll(const RobotPipeline *ctx)
{
    if (!s_output_enabled) return;
    pump();
    if ((++s_poll_count & TELEMETRY_POLL_CHECK_MASK) != 0U) return;
    if (ctx != NULL) periodic(ctx);
}
