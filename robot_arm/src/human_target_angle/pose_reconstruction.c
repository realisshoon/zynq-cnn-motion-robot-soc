#include "pose_mapping_internal.h"

#include <float.h>
#include <math.h>
#include <stddef.h>

/* ================================================================
 * 단안 Camera 2D -> 상대 3D Pose 복원
 * ================================================================
 *
 * 핵심 변경점
 * -----------
 * 예전 방식은 두 Shoulder의 Z를 같다고 가정하고 화면상의 Shoulder 폭만으로
 * body depth를 결정했다. 이 방식은 사람이 카메라를 측면으로 볼 때
 * shoulder foreshortening을 "사람이 멀어진 것"으로 오해하기 쉽다.
 *
 * 현재 방식은 Shoulder Width = 1.0이라는 3D 길이 제약은 유지하되,
 * Left/Right Shoulder가 서로 다른 Z를 가질 수 있게 한다.
 *
 * 한 frame에서 다음 4개 점은 각각 자신의 camera ray 위에 있어야 한다.
 *
 *   Shoulder L --(1.0)-- Shoulder R(active)
 *                            |
 *                       upper arm
 *                            |
 *                          Elbow
 *                            |
 *                         forearm
 *                            |
 *                          Wrist
 *
 * 단안 카메라만으로는 전체 depth가 하나로 결정되지 않으므로,
 * active shoulder depth를 1차원으로 탐색하고 다음 기준으로 가장 자연스러운
 * 3D chain을 선택한다.
 *
 *   1) Shoulder / Upper arm / Forearm link constraint를 최대한 만족
 *   2) 이전 frame 3D pose와의 연속성 유지
 *   3) 첫 frame에서는 2D shoulder 폭 기반 depth를 약한 bootstrap prior로 사용
 *
 * 따라서 측면 자세에서도 양 Shoulder의 Z 차이를 허용하면서 팔 chain을
 * 계속 복원할 수 있다.
 */

/* Cortex-A9 + 10~20 Hz에서 충분히 작은 연산량이다. */
#define PM_DEPTH_SEARCH_COARSE_STEPS        96U
#define PM_DEPTH_SEARCH_REFINE_STEPS        24U

/* exact link를 우선하고 soft fallback은 보조적으로만 선택한다. */
#define PM_RECON_RESIDUAL_WEIGHT            200.0f
#define PM_RECON_TEMPORAL_WEIGHT            1.0f
#define PM_RECON_BOOTSTRAP_DEPTH_WEIGHT     1.0f
#define PM_RECON_BOOTSTRAP_STEP_WEIGHT      0.5f
#define PM_RECON_TRACKING_DEPTH_WEIGHT      0.01f

/* Shoulder ray가 사실상 완전히 겹치는 퇴화 상태만 거른다. */
#define PM_DEGENERATE_SHOULDER_SPAN_PX      4.0f

typedef struct {
    Point3D point;
    float residual_ratio;
} RaySphereCandidate;

typedef struct {
    Point3D shoulder_l;
    Point3D shoulder_r;
    Point3D elbow;
    Point3D wrist;
    float active_shoulder_z;
    float score;
    uint8_t valid;
} MajorChainCandidate;

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

static float point_distance_sq(Point3D a, Point3D b)
{
    return pm_sqrf(a.x - b.x)
         + pm_sqrf(a.y - b.y)
         + pm_sqrf(a.z - b.z);
}

/*
 * 하나의 camera ray와 parent 중심 sphere 사이에서 가능한 점들을 만든다.
 *
 * exact intersection:
 *   최대 2개 root 반환, residual = 0
 *
 * exact intersection이 없을 때:
 *   ray 위 closest point가 link length보다 설정 비율 이내로만 벗어나면
 *   1개의 soft candidate를 허용한다.
 */
static int ray_sphere_candidates(
    Vec3 ray,
    Point3D parent,
    float radius,
    RaySphereCandidate out[2]
)
{
    float b;
    float c;
    float disc;
    int count = 0;

    if (out == NULL || radius <= PM_EPS) return 0;

    b = pm_vdot(parent, ray);
    c = pm_vdot(parent, parent) - radius * radius;
    disc = b * b - c;

    if (disc >= 0.0f) {
        float root = sqrtf(disc);
        float t[2];
        int i;

        t[0] = b - root;
        t[1] = b + root;

        for (i = 0; i < 2; ++i) {
            Point3D p;

            if (t[i] <= 0.0f) continue;

            p = pm_vscale(ray, t[i]);
            if (p.z < PM_MIN_BODY_DEPTH_UNIT ||
                p.z > PM_MAX_BODY_DEPTH_UNIT) {
                continue;
            }

            p.valid = 1U;
            out[count].point = p;
            out[count].residual_ratio = 0.0f;
            count++;
        }

        return count;
    }

    /* soft fallback */
    if (b > 0.0f) {
        Point3D closest = pm_vscale(ray, b);
        float closest_dist = pm_vlen(pm_vsub(closest, parent));
        float residual_ratio = (closest_dist - radius) / radius;

        if (residual_ratio < 0.0f) residual_ratio = 0.0f;

        if (residual_ratio <= PM_RAY_SOFT_LINK_TOLERANCE_RATIO &&
            closest.z >= PM_MIN_BODY_DEPTH_UNIT &&
            closest.z <= PM_MAX_BODY_DEPTH_UNIT) {

            closest.valid = 1U;
            out[0].point = closest;
            out[0].residual_ratio = residual_ratio;
            return 1;
        }
    }

    return 0;
}

/*
 * Finger처럼 parent가 이미 확정된 경우 사용하는 단순 선택 함수.
 * 가능한 root가 2개이면 이전 frame과 가까운 쪽을 선택한다.
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
    RaySphereCandidate candidate[2];
    Vec3 ray;
    int count;

    if (out == NULL) return -1;

    ray = pixel_unit_ray(pixel);
    count = ray_sphere_candidates(ray, parent, radius, candidate);

    if (count <= 0) return -1;

    if (count == 1) {
        *out = candidate[0].point;
        return 0;
    }

    if (prev_valid && prev != NULL) {
        float e0 = point_distance_sq(candidate[0].point, *prev);
        float e1 = point_distance_sq(candidate[1].point, *prev);
        *out = (e0 <= e1) ? candidate[0].point : candidate[1].point;
    } else {
        float dz0 = fabsf(candidate[0].point.z - parent.z);
        float dz1 = fabsf(candidate[1].point.z - parent.z);
        *out = (dz0 <= dz1) ? candidate[0].point : candidate[1].point;
    }

    return 0;
}

static float candidate_score(
    const PoseMappingContext *ctx,
    PoseArmSide active_arm,
    const MajorChainCandidate *candidate,
    float link_residual_sq,
    float nominal_body_z,
    uint8_t nominal_valid
)
{
    float score;
    float shoulder_mid_z;

    score = PM_RECON_RESIDUAL_WEIGHT * link_residual_sq;

    if (ctx->major_pose3d_valid) {
        score += PM_RECON_TEMPORAL_WEIGHT * (
            point_distance_sq(candidate->shoulder_l, ctx->shoulder_l_3d) +
            point_distance_sq(candidate->shoulder_r, ctx->shoulder_r_3d) +
            point_distance_sq(candidate->elbow,      ctx->elbow_3d) +
            point_distance_sq(candidate->wrist,      ctx->wrist_3d)
        );
    }

    if (nominal_valid) {
        float weight = ctx->major_pose3d_valid
            ? PM_RECON_TRACKING_DEPTH_WEIGHT
            : PM_RECON_BOOTSTRAP_DEPTH_WEIGHT;

        shoulder_mid_z = 0.5f *
            (candidate->shoulder_l.z + candidate->shoulder_r.z);

        score += weight * pm_sqrf(shoulder_mid_z - nominal_body_z);
    }

    /*
     * 첫 frame에는 이전 pose가 없어서 ray-sphere의 앞/뒤 root가 모두 가능하다.
     * 이때 각 link가 camera Z 방향으로 불필요하게 크게 꺾이는 해보다
     * parent와 비슷한 depth를 갖는 해를 약하게 선호한다.
     * hard constraint가 아니므로 측면 자세의 실제 shoulder Z 차이는 허용한다.
     */
    if (!ctx->major_pose3d_valid) {
        float active_shoulder_z = (active_arm == POSE_ARM_LEFT)
            ? candidate->shoulder_l.z
            : candidate->shoulder_r.z;

        score += PM_RECON_BOOTSTRAP_STEP_WEIGHT * (
            pm_sqrf(candidate->shoulder_l.z - candidate->shoulder_r.z) +
            pm_sqrf(candidate->elbow.z - active_shoulder_z) +
            pm_sqrf(candidate->wrist.z - candidate->elbow.z)
        );
    }

    return score;
}

/*
 * active shoulder의 한 depth 가설을 평가한다.
 * 각 link에서 가능한 root를 모두 조합해서 가장 좋은 chain만 남긴다.
 */
static void evaluate_active_shoulder_depth(
    const PoseMappingContext *ctx,
    PoseArmSide active_arm,
    Vec3 ray_shoulder_l,
    Vec3 ray_shoulder_r,
    Vec3 ray_elbow,
    Vec3 ray_wrist,
    float active_shoulder_z,
    float nominal_body_z,
    uint8_t nominal_valid,
    MajorChainCandidate *best
)
{
    Vec3 active_ray;
    Vec3 other_shoulder_ray;
    Point3D active_shoulder;
    RaySphereCandidate other_shoulder_candidate[2];
    RaySphereCandidate elbow_candidate[2];
    int other_count;
    int elbow_count;
    int i;
    int j;

    float upper_len = PM_SHOULDER_WIDTH_UNIT * PM_UPPER_ARM_RATIO;
    float forearm_len = PM_SHOULDER_WIDTH_UNIT * PM_FOREARM_RATIO;

    if (best == NULL) return;

    active_ray = (active_arm == POSE_ARM_LEFT)
        ? ray_shoulder_l
        : ray_shoulder_r;

    other_shoulder_ray = (active_arm == POSE_ARM_LEFT)
        ? ray_shoulder_r
        : ray_shoulder_l;

    if (active_ray.z <= PM_EPS) return;

    active_shoulder = pm_vscale(
        active_ray,
        active_shoulder_z / active_ray.z
    );
    active_shoulder.valid = 1U;

    other_count = ray_sphere_candidates(
        other_shoulder_ray,
        active_shoulder,
        PM_SHOULDER_WIDTH_UNIT,
        other_shoulder_candidate
    );

    if (other_count <= 0) return;

    elbow_count = ray_sphere_candidates(
        ray_elbow,
        active_shoulder,
        upper_len,
        elbow_candidate
    );

    if (elbow_count <= 0) return;

    for (i = 0; i < other_count; ++i) {
        for (j = 0; j < elbow_count; ++j) {
            RaySphereCandidate wrist_candidate[2];
            int wrist_count;
            int k;

            wrist_count = ray_sphere_candidates(
                ray_wrist,
                elbow_candidate[j].point,
                forearm_len,
                wrist_candidate
            );

            for (k = 0; k < wrist_count; ++k) {
                MajorChainCandidate current;
                float residual_sq;
                float score;

                if (active_arm == POSE_ARM_LEFT) {
                    current.shoulder_l = active_shoulder;
                    current.shoulder_r = other_shoulder_candidate[i].point;
                } else {
                    current.shoulder_l = other_shoulder_candidate[i].point;
                    current.shoulder_r = active_shoulder;
                }

                current.elbow = elbow_candidate[j].point;
                current.wrist = wrist_candidate[k].point;
                current.active_shoulder_z = active_shoulder_z;
                current.valid = 1U;

                residual_sq =
                    pm_sqrf(other_shoulder_candidate[i].residual_ratio) +
                    pm_sqrf(elbow_candidate[j].residual_ratio) +
                    pm_sqrf(wrist_candidate[k].residual_ratio);

                score = candidate_score(
                    ctx,
                    active_arm,
                    &current,
                    residual_sq,
                    nominal_body_z,
                    nominal_valid
                );

                current.score = score;

                if (!best->valid || score < best->score) {
                    *best = current;
                }
            }
        }
    }
}

/*
 * active shoulder depth를 coarse -> refine 2단계로 찾는다.
 * 전 구간을 탐색하므로 사람이 몸을 측면으로 돌려 nominal shoulder depth가
 * 틀어져도 fixed equal-Z 가정에 묶이지 않는다.
 */
static int search_major_chain(
    const PoseMappingContext *ctx,
    PoseArmSide active_arm,
    Vec3 ray_shoulder_l,
    Vec3 ray_shoulder_r,
    Vec3 ray_elbow,
    Vec3 ray_wrist,
    float nominal_body_z,
    uint8_t nominal_valid,
    MajorChainCandidate *best
)
{
    float z_min = PM_MIN_BODY_DEPTH_UNIT;
    float z_max = PM_MAX_BODY_DEPTH_UNIT;
    float coarse_step;
    unsigned i;

    if (best == NULL) return -1;

    best->valid = 0U;
    best->score = FLT_MAX;

    coarse_step = (z_max - z_min) /
        (float)(PM_DEPTH_SEARCH_COARSE_STEPS - 1U);

    for (i = 0U; i < PM_DEPTH_SEARCH_COARSE_STEPS; ++i) {
        float z = z_min + coarse_step * (float)i;

        evaluate_active_shoulder_depth(
            ctx,
            active_arm,
            ray_shoulder_l,
            ray_shoulder_r,
            ray_elbow,
            ray_wrist,
            z,
            nominal_body_z,
            nominal_valid,
            best
        );
    }

    if (!best->valid) return -1;

    /* coarse best 주변만 더 촘촘하게 재탐색 */
    {
        float refine_min = best->active_shoulder_z - coarse_step;
        float refine_max = best->active_shoulder_z + coarse_step;
        float refine_step;

        if (refine_min < z_min) refine_min = z_min;
        if (refine_max > z_max) refine_max = z_max;

        refine_step = (refine_max - refine_min) /
            (float)(PM_DEPTH_SEARCH_REFINE_STEPS - 1U);

        for (i = 0U; i < PM_DEPTH_SEARCH_REFINE_STEPS; ++i) {
            float z = refine_min + refine_step * (float)i;

            evaluate_active_shoulder_depth(
                ctx,
                active_arm,
                ray_shoulder_l,
                ray_shoulder_r,
                ray_elbow,
                ray_wrist,
                z,
                nominal_body_z,
                nominal_valid,
                best
            );
        }
    }

    return best->valid ? 0 : -1;
}

int pm_reconstruct_major_pose3d(
    PoseMappingContext *ctx,
    PoseArmSide active_arm,
    float dt_filter_sec,
    float *shoulder_span_px_out
)
{
    float du;
    float dv;
    float shoulder_span_px;
    float normalized_shoulder_span;
    float nominal_body_z = 0.0f;
    uint8_t nominal_valid = 0U;

    Vec3 ray_shoulder_l;
    Vec3 ray_shoulder_r;
    Vec3 ray_elbow;
    Vec3 ray_wrist;
    MajorChainCandidate best;
    uint8_t prev_valid;

    if (ctx == NULL || shoulder_span_px_out == NULL) return -1;

    du = ctx->shoulder_r.value.x - ctx->shoulder_l.value.x;
    dv = ctx->shoulder_r.value.y - ctx->shoulder_l.value.y;
    shoulder_span_px = hypotf(du, dv);

    /* 양 Shoulder ray가 거의 완전히 같은 경우 body axis 자체가 불안정하다. */
    if (shoulder_span_px < PM_DEGENERATE_SHOULDER_SPAN_PX) return -1;

    normalized_shoulder_span = sqrtf(
        pm_sqrf(du / PM_CAMERA_FX) +
        pm_sqrf(dv / PM_CAMERA_FY)
    );

    /*
     * 2D shoulder 폭 기반 depth는 이제 정답이 아니라 bootstrap/약한 prior다.
     * 측면에서는 foreshortening 때문에 값이 커질 수 있으므로 hard constraint로
     * 사용하지 않는다.
     */
    if (normalized_shoulder_span > PM_EPS) {
        nominal_body_z = PM_SHOULDER_WIDTH_UNIT / normalized_shoulder_span;
        nominal_body_z = pm_clampf(
            nominal_body_z,
            PM_MIN_BODY_DEPTH_UNIT,
            PM_MAX_BODY_DEPTH_UNIT
        );
        nominal_valid = 1U;
    }

    ray_shoulder_l = pixel_unit_ray(ctx->shoulder_l.value);
    ray_shoulder_r = pixel_unit_ray(ctx->shoulder_r.value);
    ray_elbow      = pixel_unit_ray(ctx->elbow.value);
    ray_wrist      = pixel_unit_ray(ctx->wrist.value);

    if (search_major_chain(
            ctx,
            active_arm,
            ray_shoulder_l,
            ray_shoulder_r,
            ray_elbow,
            ray_wrist,
            nominal_body_z,
            nominal_valid,
            &best) != 0) {
        return -1;
    }

    prev_valid = ctx->major_pose3d_valid;

    /*
     * Shoulder -> Elbow -> Wrist chain이 완성된 뒤 한 번에 commit한다.
     * 중간 실패 시 이전 정상 3D pose를 그대로 유지한다.
     */
    if (prev_valid) {
        ctx->shoulder_l_3d = ema_point3d(
            ctx->shoulder_l_3d, best.shoulder_l, dt_filter_sec);
        ctx->shoulder_r_3d = ema_point3d(
            ctx->shoulder_r_3d, best.shoulder_r, dt_filter_sec);
        ctx->elbow_3d = ema_point3d(
            ctx->elbow_3d, best.elbow, dt_filter_sec);
        ctx->wrist_3d = ema_point3d(
            ctx->wrist_3d, best.wrist, dt_filter_sec);
    } else {
        ctx->shoulder_l_3d = best.shoulder_l;
        ctx->shoulder_r_3d = best.shoulder_r;
        ctx->elbow_3d = best.elbow;
        ctx->wrist_3d = best.wrist;
    }

    ctx->major_pose3d_valid = 1U;

    /*
     * gripper 2D 정규화가 측면 shoulder foreshortening 때문에 과도하게 커지는
     * 것을 막기 위해 최소 reference 폭만 보장한다.
     */
    *shoulder_span_px_out = (shoulder_span_px < PM_MIN_SHOULDER_WIDTH_PX)
        ? PM_MIN_SHOULDER_WIDTH_PX
        : shoulder_span_px;

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
        ctx->finger1_3d = ema_point3d(
            ctx->finger1_3d, raw_finger1, dt_filter_sec);
        ctx->finger2_3d = ema_point3d(
            ctx->finger2_3d, raw_finger2, dt_filter_sec);
    } else {
        ctx->finger1_3d = raw_finger1;
        ctx->finger2_3d = raw_finger2;
    }

    ctx->finger_pose3d_valid = 1U;
    return 0;
}
