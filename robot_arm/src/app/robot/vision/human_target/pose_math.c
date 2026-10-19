#include "pose_mapping_internal.h"

#include <math.h>
#include <stddef.h>

/* ================================================================
 * 공통 수학 / Vector / Angle Helper
 * ================================================================
 *
 * Agent1의 여러 파일에서 공통으로 사용하는 수학 함수를 모았다.
 * 알고리즘을 바꿀 때 pose_mapping.c를 길게 만들지 않기 위한 파일이다.
 */

float pm_clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

float pm_sqrf(float v)
{
    return v * v;
}

/*
 * Time-based EMA 계수.
 *
 * alpha = 1 - exp(-dt / tau)
 *
 * CNN이 10~20 Hz 사이에서 변해도 실제 시간 기준 반응속도를
 * 비슷하게 유지하기 위해 고정 alpha 대신 사용한다.
 */
float pm_alpha_from_tau(float dt_sec, float tau_sec)
{
    if (tau_sec <= PM_EPS) return 1.0f;
    if (dt_sec <= 0.0f) return 0.0f;

    return 1.0f - expf(-dt_sec / tau_sec);
}

/* EMA 계산에 사용할 dt의 비정상 범위를 잘라낸다. */
float pm_sanitize_filter_dt(float dt_sec)
{
    if (dt_sec <= 0.0f) {
        dt_sec = 1.0f / PM_DEFAULT_FPS;
    }

    return pm_clampf(
        dt_sec,
        PM_FILTER_DT_MIN_SEC,
        PM_FILTER_DT_MAX_SEC
    );
}

Vec3 pm_vec3(float x, float y, float z)
{
    Vec3 v;
    v.x = x;
    v.y = y;
    v.z = z;
    v.valid = 1U;
    return v;
}

Vec3 pm_vadd(Vec3 a, Vec3 b)
{
    return pm_vec3(a.x + b.x, a.y + b.y, a.z + b.z);
}

Vec3 pm_vsub(Vec3 a, Vec3 b)
{
    return pm_vec3(a.x - b.x, a.y - b.y, a.z - b.z);
}

Vec3 pm_vscale(Vec3 a, float s)
{
    return pm_vec3(a.x * s, a.y * s, a.z * s);
}

float pm_vdot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 pm_vcross(Vec3 a, Vec3 b)
{
    return pm_vec3(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    );
}

float pm_vlen(Vec3 a)
{
    return sqrtf(pm_vdot(a, a));
}

int pm_vnormalize(Vec3 *v)
{
    float n;

    if (v == NULL) return -1;

    n = pm_vlen(*v);
    if (!isfinite(n) || n < PM_EPS) return -1;

    v->x /= n;
    v->y /= n;
    v->z /= n;

    return 0;
}

/* Vector에서 axis 방향 성분을 제거한다. */
Vec3 pm_project_perpendicular(Vec3 v, Vec3 axis)
{
    return pm_vsub(v, pm_vscale(axis, pm_vdot(v, axis)));
}

float pm_wrap180(float deg)
{
    while (deg > 180.0f) deg -= 360.0f;
    while (deg < -180.0f) deg += 360.0f;
    return deg;
}

/* +179 -> -179 경계에서 358도 튄 것으로 보이지 않게 처리한다. */
float pm_unwrap_near(float deg, float reference)
{
    while ((deg - reference) > 180.0f) deg -= 360.0f;
    while ((deg - reference) < -180.0f) deg += 360.0f;
    return deg;
}

float pm_distance_2d(Point2D a, Point2D b)
{
    return hypotf(b.x - a.x, b.y - a.y);
}

/*
 * 카메라 roll(이미지 평면 내 회전, +Z=카메라 시선축 기준)만큼 "위" 벡터를
 * 보정한다. 카메라를 위/아래로 향하는 각도(pitch)는 이미지 up 벡터 자체를
 * 안 바꾸므로 여기서 다루지 않는다 — roll만 X-Y 평면 안에서 up을 틀어지게
 * 만든다. roll=0이면 기존과 동일한 (0,1,0)을 그대로 돌려준다.
 */
Vec3 pm_camera_up_for_roll(float roll_deg)
{
    float rad = -roll_deg * PM_DEG_TO_RAD;
    float c = cosf(rad), s = sinf(rad);
    Vec3 up = {-s, c, 0.0f, 1U};
    return up;
}

/*
 * pm_camera_up_for_roll()의 역함수 방향 관계: 카메라가 시계방향으로 roll_deg만큼
 * 돌아가면 관측되는 모든 것(어깨선 포함)의 이미지평면 각도가 -roll_deg만큼
 * 회전한다. 즉 어깨가 실제로 수평(world-level)이고 카메라를 정면으로 바라보고
 * 있다고 가정하면, 관측된 raw BodyX의 이미지평면 각도로부터 카메라 roll을
 * 역산할 수 있다.
 *
 * 기준 방향은 +X가 아니라 -X다: 사람이 카메라를 정면으로 보면 해부학적
 * 오른쪽 어깨(shoulder_r)가 화면 왼쪽(카메라 -X)에 나타난다(거울 대칭.
 * docs/coordinate_system.md의 실측 예제도 SR.x < SL.x). roll=0, 정면
 * 기준일 때 body_x ~= (-1,0,0)이어야 하므로 atan2(y,-x)를 쓴다.
 *
 * 사람이 몸을 돌리면(yaw) X0 부호 자체가 달라질 수 있어 이 역산이 깨진다 —
 * 호출부(pm_update_stable_body_frame)가 body_x.x가 충분히 음수(정면에 가까움)일
 * 때만 이 값을 쓰도록 게이팅한다. 또한 사람이 실제로 어깨를 기울인 경우와도
 * 구분하지 못하므로 이 값 자체는 근사치다 — 느린 저역통과로만 사용한다
 * (PM_CAMERA_ROLL_ADAPT_*).
 */
float pm_camera_roll_estimate_from_x(Vec3 body_x)
{
    return atan2f(body_x.y, -body_x.x) * PM_RAD_TO_DEG;
}

/*
 * X is anatomical left -> right; Y is projected camera up; Z = X x Y.
 * Forcing Z toward camera +Z would reverse Y for front-facing people.
 * Near vertical X, projected up is ill-conditioned. Report failure so the
 * public API uses its bounded target HOLD/invalid policy, not an arbitrary Y.
 * 0.01 = sin(angle to camera up), approximately a 0.57 degree exclusion cone.
 * At the pole, up semantics and continuous Y cannot both be guaranteed.
 */
static int complete_body_frame(Vec3 x, float roll_deg, Vec3 *y, Vec3 *z)
{
    const Vec3 up = pm_camera_up_for_roll(roll_deg);
    *y = pm_project_perpendicular(up, x);
    if (pm_vlen(*y) < PM_BODY_UP_MIN_PROJECTION || pm_vnormalize(y) != 0) {
        return -1;
    }
    *z = pm_vcross(x, *y);
    if (pm_vnormalize(z) != 0) return -1;
    *y = pm_vcross(*z, x);
    return pm_vnormalize(y);
}

/*
 * Stateless construction — always uses the fixed PM_CAMERA_ROLL_DEG config
 * value, never the per-frame adaptive estimate (no history to adapt from
 * here). Used by tests and one-shot reconstruction; the live pipeline goes
 * through pm_update_stable_body_frame() instead.
 */
int pm_build_body_frame(
    Point3D shoulder_l,
    Point3D shoulder_r,
    Vec3 *body_x,
    Vec3 *body_y,
    Vec3 *body_z
)
{
    if (body_x == NULL || body_y == NULL || body_z == NULL) return -1;

    *body_x = pm_vsub(shoulder_r, shoulder_l);
    if (pm_vnormalize(body_x) != 0) return -1;

    return complete_body_frame(*body_x, PM_CAMERA_ROLL_DEG, body_y, body_z);
}

/*
 * 현재 3D Shoulder로 만든 Body frame을 시간축으로 안정화한다.
 *
 * 중요한 점:
 * - 측면에서는 두 Shoulder가 영상에서 거의 겹쳐 body axis가 민감해질 수 있다.
 * - 한 frame의 이상한 Shoulder 때문에 Base/Roll 기준축이 크게 튀는 것을 줄인다.
 * - 관측 신뢰도는 alpha로 반영한다. 정의 불가능한 축은 상위 HOLD로 보낸다.
 */
int pm_update_stable_body_frame(PoseMappingContext *ctx, float dt_filter_sec)
{
    Vec3 raw_x, raw_y, raw_z;
    Vec3 filtered_x;
    Vec3 new_y, new_z;
    float shoulder_span_px;
    float reliability = 1.0f;
    float alpha;
    float axis_dot;
    float axis_jump_deg;
    float active_roll_deg;
    float measured_roll_deg;
    uint8_t roll_observation_ok;

    if (ctx == NULL) return -1;

    raw_x = pm_vsub(ctx->shoulder_r_3d, ctx->shoulder_l_3d);
    if (pm_vnormalize(&raw_x) != 0) return -1;

    shoulder_span_px = pm_distance_2d(
        ctx->shoulder_l.value,
        ctx->shoulder_r.value
    );

    /*
     * 이번 프레임 축은 항상 "이전까지 검증된" roll 추정치로 만든다. 새 관측치는
     * 이번 프레임이 끝까지 성공했을 때만(맨 아래) 반영한다 — 실패/저신뢰 프레임이
     * 추정치를 오염시키지 않도록 하기 위함.
     */
    active_roll_deg = ctx->camera_roll_estimate_valid
        ? ctx->camera_roll_estimate_deg : PM_CAMERA_ROLL_DEG;
#if !PM_CAMERA_ROLL_ADAPT_ENABLE
    active_roll_deg = PM_CAMERA_ROLL_DEG;
#endif
    roll_observation_ok = (shoulder_span_px >= PM_BODY_FRAME_LOW_CONF_SPAN_PX) &&
        (raw_x.x < -PM_CAMERA_ROLL_ADAPT_MIN_FRONTAL);
    measured_roll_deg = pm_camera_roll_estimate_from_x(raw_x);

    if (complete_body_frame(raw_x, active_roll_deg, &raw_y, &raw_z) != 0) {
        return -1;
    }

    if (!ctx->body_frame_valid) {
        ctx->body_x_axis = raw_x;
        ctx->body_y_axis = raw_y;
        ctx->body_z_axis = raw_z;
        ctx->body_frame_valid = 1U;
        ctx->camera_roll_estimate_deg = active_roll_deg;
        ctx->camera_roll_estimate_valid = 1U;
        return 0;
    }

    /*
     * Side-view처럼 Shoulder가 가까워질수록 body direction 관측 신뢰도가 낮다.
     * 그래도 0으로 만들지 않고 천천히 따라가게 한다.
     */
    if (shoulder_span_px < PM_BODY_FRAME_LOW_CONF_SPAN_PX) {
        reliability *= PM_BODY_FRAME_LOW_CONF_SCALE;
    }

    /*
     * 한 CNN frame 사이에 body X축이 크게 바뀌면 landmark 흔들림일 가능성이 높다.
     * 역시 reject 대신 반영량만 낮춘다.
     */
    axis_dot = pm_clampf(
        pm_vdot(raw_x, ctx->body_x_axis),
        -1.0f,
        1.0f
    );
    axis_jump_deg = acosf(axis_dot) * PM_RAD_TO_DEG;

    if (axis_jump_deg > PM_BODY_FRAME_LARGE_JUMP_DEG) {
        reliability *= PM_BODY_FRAME_LARGE_JUMP_SCALE;
    }

    alpha = pm_alpha_from_tau(dt_filter_sec, PM_BODY_FRAME_TAU_SEC);
    alpha *= reliability;
    alpha = pm_clampf(alpha, 0.0f, 1.0f);

    filtered_x = pm_vadd(
        pm_vscale(ctx->body_x_axis, 1.0f - alpha),
        pm_vscale(raw_x, alpha)
    );

    if (pm_vnormalize(&filtered_x) != 0) {
        /* Keep the last frame, but do not report a fresh target. */
        return -1;
    }

    /* filtered X축을 기준으로 Y/Z를 다시 직교화한다. */
    if (complete_body_frame(filtered_x, active_roll_deg, &new_y, &new_z) != 0) {
        return -1;
    }

    ctx->body_x_axis = filtered_x;
    ctx->body_y_axis = new_y;
    ctx->body_z_axis = new_z;
    ctx->body_frame_valid = 1U;

    /*
     * 이번 프레임이 끝까지 성공했고 어깨가 충분히 넓게(고신뢰) 보였을 때만
     * roll 추정치를 갱신한다. PM_CAMERA_ROLL_ADAPT_TAU_SEC로 느리게 따라가서
     * 사람의 빠른 동작과는 섞이지 않게 한다.
     */
    if (roll_observation_ok) {
        float roll_alpha = pm_alpha_from_tau(dt_filter_sec, PM_CAMERA_ROLL_ADAPT_TAU_SEC);
        ctx->camera_roll_estimate_deg = pm_clampf(
            active_roll_deg + roll_alpha * pm_wrap180(measured_roll_deg - active_roll_deg),
            -PM_CAMERA_ROLL_ADAPT_MAX_DEG,
            PM_CAMERA_ROLL_ADAPT_MAX_DEG
        );
    }

    return 0;
}

int pm_get_stable_body_frame(
    const PoseMappingContext *ctx,
    Vec3 *body_x,
    Vec3 *body_y,
    Vec3 *body_z
)
{
    if (ctx == NULL || body_x == NULL || body_y == NULL || body_z == NULL) {
        return -1;
    }

    if (!ctx->body_frame_valid) return -1;

    *body_x = ctx->body_x_axis;
    *body_y = ctx->body_y_axis;
    *body_z = ctx->body_z_axis;
    return 0;
}

/*
 * 사람 관절 추정값용 연속 각도 필터.
 * 1) 필요 시 ±180도 경계 unwrap
 * 2) 작은 추정 노이즈 deadband
 * 3) time-based EMA
 *
 * 여기서는 로봇의 최대 각속도를 제한하지 않는다.
 * Human angle -> Robot angle 변환 후의 실제 동작 속도 제한은 Agent2가 담당한다.
 */
float pm_filter_angle_continuous(
    float prev,
    float current,
    float tau_sec,
    float deadband_deg,
    float dt_filter_sec,
    uint8_t use_unwrap
)
{
    float delta;
    float alpha;

    if (use_unwrap) {
        current = pm_unwrap_near(current, prev);
    }

    delta = current - prev;
    if (fabsf(delta) < deadband_deg) {
        current = prev;
    }

    alpha = pm_alpha_from_tau(dt_filter_sec, tau_sec);
    return prev + alpha * (current - prev);
}
