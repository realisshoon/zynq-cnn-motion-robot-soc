#include "pose_mapping_internal.h"

#include <math.h>
#include <stddef.h>

/* ================================================================
 * 단안 Camera 2D -> 상대 3D Pose 복원
 * ================================================================
 *
 * 절대 mm 좌표를 만드는 것이 목적이 아니다.
 * Shoulder Width = 1.0을 기준으로 상대적인 3D 자세를 복원한 뒤
 * Joint-space 각도 계산에 사용한다.
 */

/* image +Y(아래)를 Agent1 3D +Y(위)로 뒤집어 역투영한다. */
static Vec3 backproject_at_z(Point2D p, float z)
{
    float xn = (p.x - PM_CAMERA_CX) / PM_CAMERA_FX;
    float yn = -(p.y - PM_CAMERA_CY) / PM_CAMERA_FY;

    return pm_vec3(xn * z, yn * z, z);
}

/* Camera 원점에서 pixel 방향으로 향하는 단위 ray */
static Vec3 pixel_unit_ray(Point2D p)
{
    Vec3 d = pm_vec3(
        (p.x - PM_CAMERA_CX) / PM_CAMERA_FX,
        -(p.y - PM_CAMERA_CY) / PM_CAMERA_FY,
        1.0f
    );

    (void)pm_vnormalize(&d);
    return d;
}

/* 3D 좌표도 X/Y와 Z를 서로 다른 시간상수로 필터링한다. */
static Point3D ema_point3d(
    Point3D old_p,
    Point3D new_p,
    float dt_filter_sec
)
{
    Point3D out;
    float a_xy = pm_alpha_from_tau(dt_filter_sec, PM_POSITION_XY_TAU_SEC);
    float a_z  = pm_alpha_from_tau(dt_filter_sec, PM_POSITION_Z_TAU_SEC);

    out.x = old_p.x + a_xy * (new_p.x - old_p.x);
    out.y = old_p.y + a_xy * (new_p.y - old_p.y);
    out.z = old_p.z + a_z  * (new_p.z - old_p.z);
    out.valid = new_p.valid;

    return out;
}

/*
 * Camera ray와 Parent 관절 중심 Sphere의 교점을 이용해 Child 3D를 구한다.
 * 두 해가 모두 가능한 경우 이전 frame 위치와 가까운 해를 선택해
 * 단안 깊이의 +Z/-Z ambiguity가 frame마다 뒤집히는 것을 줄인다.
 */
static int reconstruct_on_ray_sphere(
    Point2D pixel,
    Point3D parent,
    float radius,
    const Point3D *prev,
    uint8_t prev_valid,
    Point3D *out
)
{
    Vec3 d;
    float b, c, disc, root;
    float t1, t2;
    Point3D p1, p2;
    int p1_ok, p2_ok;

    if (out == NULL || radius <= PM_EPS) return -1;

    d = pixel_unit_ray(pixel);

    /* |t*d - parent|^2 = radius^2 */
    b = pm_vdot(parent, d);
    c = pm_vdot(parent, parent) - radius * radius;
    disc = b * b - c;

    if (disc < 0.0f) {
        float relax = PM_RAY_DISC_RELAX_RATIO * radius * radius;

        if (disc >= -relax) {
            disc = 0.0f;
        } else {
            return -1;
        }
    }

    root = sqrtf(disc);
    t1 = b - root;
    t2 = b + root;

    p1 = pm_vscale(d, t1);
    p2 = pm_vscale(d, t2);

    p1_ok = (t1 > 0.0f &&
             p1.z >= PM_MIN_BODY_DEPTH_UNIT &&
             p1.z <= PM_MAX_BODY_DEPTH_UNIT);

    p2_ok = (t2 > 0.0f &&
             p2.z >= PM_MIN_BODY_DEPTH_UNIT &&
             p2.z <= PM_MAX_BODY_DEPTH_UNIT);

    if (!p1_ok && !p2_ok) return -1;

    if (p1_ok && !p2_ok) {
        *out = p1;
    } else if (!p1_ok && p2_ok) {
        *out = p2;
    } else if (prev_valid && prev != NULL) {
        float e1 = pm_sqrf(p1.x - prev->x)
                 + pm_sqrf(p1.y - prev->y)
                 + pm_sqrf(p1.z - prev->z);

        float e2 = pm_sqrf(p2.x - prev->x)
                 + pm_sqrf(p2.y - prev->y)
                 + pm_sqrf(p2.z - prev->z);

        *out = (e1 <= e2) ? p1 : p2;
    } else {
        /* 첫 frame은 부모 joint와 depth가 가까운 해를 사용한다. */
        *out = (fabsf(p1.z - parent.z) <= fabsf(p2.z - parent.z)) ? p1 : p2;
    }

    out->valid = 1U;
    return 0;
}

int pm_reconstruct_major_pose3d(
    PoseMappingContext *ctx,
    PoseArmSide active_arm,
    float dt_filter_sec,
    float *shoulder_span_px_out
)
{
    float du, dv;
    float shoulder_span_px;
    float normalized_shoulder_span;
    float body_z;
    float upper_len;
    float forearm_len;
    Point3D raw_shoulder_l;
    Point3D raw_shoulder_r;
    Point3D raw_elbow;
    Point3D raw_wrist;
    Point3D selected_shoulder;
    uint8_t prev_valid;

    if (ctx == NULL || shoulder_span_px_out == NULL) return -1;

    du = ctx->shoulder_r.value.x - ctx->shoulder_l.value.x;
    dv = ctx->shoulder_r.value.y - ctx->shoulder_l.value.y;
    shoulder_span_px = hypotf(du, dv);

    if (shoulder_span_px < PM_MIN_SHOULDER_WIDTH_PX) return -1;

    normalized_shoulder_span = sqrtf(
        pm_sqrf(du / PM_CAMERA_FX) +
        pm_sqrf(dv / PM_CAMERA_FY)
    );

    if (normalized_shoulder_span < PM_EPS) return -1;

    /* Shoulder Width = 1.0을 기준으로 상대 Body Depth를 추정한다. */
    body_z = PM_SHOULDER_WIDTH_UNIT / normalized_shoulder_span;

    if (body_z < PM_MIN_BODY_DEPTH_UNIT ||
        body_z > PM_MAX_BODY_DEPTH_UNIT) {
        return -1;
    }

    raw_shoulder_l = backproject_at_z(ctx->shoulder_l.value, body_z);
    raw_shoulder_r = backproject_at_z(ctx->shoulder_r.value, body_z);
    raw_shoulder_l.valid = 1U;
    raw_shoulder_r.valid = 1U;

    prev_valid = ctx->major_pose3d_valid;

    if (prev_valid) {
        ctx->shoulder_l_3d = ema_point3d(ctx->shoulder_l_3d, raw_shoulder_l, dt_filter_sec);
        ctx->shoulder_r_3d = ema_point3d(ctx->shoulder_r_3d, raw_shoulder_r, dt_filter_sec);
    } else {
        ctx->shoulder_l_3d = raw_shoulder_l;
        ctx->shoulder_r_3d = raw_shoulder_r;
    }

    selected_shoulder = (active_arm == POSE_ARM_LEFT)
        ? ctx->shoulder_l_3d
        : ctx->shoulder_r_3d;

    upper_len = PM_SHOULDER_WIDTH_UNIT * PM_UPPER_ARM_RATIO;
    forearm_len = PM_SHOULDER_WIDTH_UNIT * PM_FOREARM_RATIO;

    if (reconstruct_on_ray_sphere(
            ctx->elbow.value,
            selected_shoulder,
            upper_len,
            &ctx->elbow_3d,
            prev_valid,
            &raw_elbow) != 0) {
        return -1;
    }

    if (reconstruct_on_ray_sphere(
            ctx->wrist.value,
            raw_elbow,
            forearm_len,
            &ctx->wrist_3d,
            prev_valid,
            &raw_wrist) != 0) {
        return -1;
    }

    if (prev_valid) {
        ctx->elbow_3d = ema_point3d(ctx->elbow_3d, raw_elbow, dt_filter_sec);
        ctx->wrist_3d = ema_point3d(ctx->wrist_3d, raw_wrist, dt_filter_sec);
    } else {
        ctx->elbow_3d = raw_elbow;
        ctx->wrist_3d = raw_wrist;
    }

    ctx->major_pose3d_valid = 1U;
    *shoulder_span_px_out = shoulder_span_px;

    return 0;
}

/* Finger는 둘 다 현재 CNN frame에서 정상일 때만 새 3D를 계산한다. */
int pm_reconstruct_finger_pose3d(
    PoseMappingContext *ctx,
    float dt_filter_sec
)
{
    float finger1_len;
    float finger2_len;
    Point3D raw_finger1;
    Point3D raw_finger2;
    uint8_t prev_valid;

    if (ctx == NULL) return -1;

    finger1_len = PM_SHOULDER_WIDTH_UNIT * PM_WRIST_TO_FINGER1_RATIO;
    finger2_len = PM_SHOULDER_WIDTH_UNIT * PM_WRIST_TO_FINGER2_RATIO;
    prev_valid = ctx->finger_pose3d_valid;

    if (reconstruct_on_ray_sphere(
            ctx->finger1.value,
            ctx->wrist_3d,
            finger1_len,
            &ctx->finger1_3d,
            prev_valid,
            &raw_finger1) != 0) {
        return -1;
    }

    if (reconstruct_on_ray_sphere(
            ctx->finger2.value,
            ctx->wrist_3d,
            finger2_len,
            &ctx->finger2_3d,
            prev_valid,
            &raw_finger2) != 0) {
        return -1;
    }

    if (prev_valid) {
        ctx->finger1_3d = ema_point3d(ctx->finger1_3d, raw_finger1, dt_filter_sec);
        ctx->finger2_3d = ema_point3d(ctx->finger2_3d, raw_finger2, dt_filter_sec);
    } else {
        ctx->finger1_3d = raw_finger1;
        ctx->finger2_3d = raw_finger2;
    }

    ctx->finger_pose3d_valid = 1U;
    return 0;
}
