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
    if (n < PM_EPS) return -1;

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
 * 양 어깨로 사람 Body Coordinate를 만든다.
 *
 * Body X : Left Shoulder -> Right Shoulder
 * Body Y : 영상의 위쪽을 X와 직교화한 방향
 * Body Z : X x Y, 카메라 forward와 같은 쪽으로 부호 통일
 */
int pm_build_body_frame(
    Point3D shoulder_l,
    Point3D shoulder_r,
    Vec3 *body_x,
    Vec3 *body_y,
    Vec3 *body_z
)
{
    Vec3 camera_up = pm_vec3(0.0f, 1.0f, 0.0f);
    Vec3 camera_forward = pm_vec3(0.0f, 0.0f, 1.0f);

    if (body_x == NULL || body_y == NULL || body_z == NULL) return -1;

    *body_x = pm_vsub(shoulder_r, shoulder_l);
    if (pm_vnormalize(body_x) != 0) return -1;

    *body_y = pm_project_perpendicular(camera_up, *body_x);
    if (pm_vnormalize(body_y) != 0) return -1;

    *body_z = pm_vcross(*body_x, *body_y);
    if (pm_vnormalize(body_z) != 0) return -1;

    if (pm_vdot(*body_z, camera_forward) < 0.0f) {
        *body_z = pm_vscale(*body_z, -1.0f);
    }

    /* 수치 오차를 줄이기 위해 Y축을 한 번 더 직교화한다. */
    *body_y = pm_vcross(*body_z, *body_x);
    if (pm_vnormalize(body_y) != 0) return -1;

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
