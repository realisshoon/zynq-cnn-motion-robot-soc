#include "pose_mapping_internal.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

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

/* Robot 관절은 180도까지만 돌아간다: elbow가 shoulder보다 카메라에서
 * 더 먼(뒤) 해는 로봇이 절대 재현할 수 없으므로 강하게 배제한다.
 * wrist가 elbow보다 뒤인 해는 실제로 손목을 굽히면 나타날 수 있으므로
 * (elbow/wrist roll이 0도나 180도 근처일 때) 약한 선호로만 사용해
 * 실제 그런 자세일 때는 residual/temporal 증거로 뒤집힐 수 있게 둔다. */
#define PM_RECON_SHOULDER_ELBOW_ORDER_WEIGHT 200.0f
#define PM_RECON_ELBOW_WRIST_ORDER_WEIGHT    2.0f

/* Shoulder ray가 사실상 완전히 겹치는 퇴화 상태만 거른다. */
#define PM_DEGENERATE_SHOULDER_SPAN_PX      4.0f

/* These are dimensionless heuristic scores, not learned depth measurements.
 * PM_FINGER_BRANCH_MIN_LEAD no longer gates the first pick: as soon as the
 * window (POSE_FINGER_BRANCH_WINDOW frames) is full, the best-scoring
 * branch is reported, even if the lead over the runner-up is tiny. A larger
 * lead is still required to abandon an already selected branch, so
 * established tracking does not flip-flop on frame-to-frame score noise.
 * best_cost/second_cost are already POSE_FINGER_BRANCH_WINDOW-frame rolling
 * averages, so most single-frame noise is already filtered out before this
 * margin is even checked — 0.050 on top of that was observed to never be
 * crossed for an entire clip (a runner-up branch that stayed consistently,
 * increasingly better by up to 0.046 was still never switched to), freezing
 * the reported finger position for the rest of the video. */
#define PM_FINGER_BRANCH_MIN_LEAD           0.020f
#define PM_FINGER_BRANCH_SWITCH_LEAD        0.010f

/* The first-ever pick needs the full POSE_FINGER_BRANCH_WINDOW (60 frames)
 * of evidence, since there is no prior branch to fall back on. Once a
 * branch has been selected at least once, re-evaluating (to hold, switch,
 * or recover from a brief dropout) only needs a much shorter trailing
 * window — requiring 60 fresh frames after every reset made even a short
 * plane-quality dropout freeze the reported position for seconds. */
#define PM_FINGER_BRANCH_TRACKING_WINDOW    6U

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

    /*
     * Motor 180도 한계상 도달 불가능한 순서(elbow가 shoulder보다 뒤)는
     * 매 frame 강하게 배제하고, wrist가 elbow보다 뒤인 순서는 약하게만
     * 배제한다(실제로 손목을 굽힌 경우와 구분이 안 되므로 hard clamp가
     * 아니라 다른 증거로 뒤집을 수 있는 soft prior로 둔다).
     *
     * "뒤"는 카메라 depth가 아니라 사람 본인의 앞/뒤축(Body Z)으로 판단해야
     * 한다 — 카메라 앞에서 몸을 옆으로 틀면 Camera Z와 Body Z가 갈라지기
     * 때문이다. Body frame은 이 major chain 복원 *다음*에 계산되므로
     * (forearm_mapping.c) 여기서는 직전 frame의 안정화된 Body Z만 쓸 수
     * 있다 — 프레임 간 Body Z 변화는 느리므로 근사로 충분하다. 아직 Body
     * frame이 없으면(초기 몇 frame) camera Z로 fallback한다.
     */
    {
        Vec3 shoulder_mid = pm_vscale(
            pm_vadd(candidate->shoulder_l, candidate->shoulder_r), 0.5f);
        float elbow_behind_shoulder;
        float wrist_behind_elbow;

        if (ctx->body_frame_valid) {
            Vec3 elbow_rel = pm_vsub(candidate->elbow, shoulder_mid);
            Vec3 wrist_rel = pm_vsub(candidate->wrist, candidate->elbow);
            float elbow_front_z = pm_vdot(elbow_rel, ctx->body_z_axis);
            float wrist_front_z = pm_vdot(wrist_rel, ctx->body_z_axis);

            /* +Body Z = 사람 정면. 정면 성분이 음수면 그만큼 "뒤"다. */
            elbow_behind_shoulder = fmaxf(0.0f, -elbow_front_z);
            wrist_behind_elbow = fmaxf(0.0f, -wrist_front_z);
        } else {
            elbow_behind_shoulder =
                fmaxf(0.0f, candidate->elbow.z - shoulder_mid.z);
            wrist_behind_elbow =
                fmaxf(0.0f, candidate->wrist.z - candidate->elbow.z);
        }

        score += PM_RECON_SHOULDER_ELBOW_ORDER_WEIGHT *
            pm_sqrf(elbow_behind_shoulder);
        score += PM_RECON_ELBOW_WRIST_ORDER_WEIGHT *
            pm_sqrf(wrist_behind_elbow);
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
    Point3D predicted_finger1, predicted_finger2;
    uint8_t prev_valid;

    if (ctx == NULL) return -1;

    finger1_len = PM_SHOULDER_WIDTH_UNIT * PM_WRIST_TO_FINGER1_RATIO;
    finger2_len = PM_SHOULDER_WIDTH_UNIT * PM_WRIST_TO_FINGER2_RATIO;
    prev_valid = ctx->finger_pose3d_valid;
    predicted_finger1 = pm_vadd(ctx->wrist_3d,
        pm_vsub(ctx->finger1_3d, ctx->finger_parent_wrist));
    predicted_finger2 = pm_vadd(ctx->wrist_3d,
        pm_vsub(ctx->finger2_3d, ctx->finger_parent_wrist));

    if (reconstruct_on_ray_sphere(ctx->finger1.value, ctx->wrist_3d,
            finger1_len, &predicted_finger1, prev_valid, &raw_finger1) != 0 ||
        reconstruct_on_ray_sphere(ctx->finger2.value, ctx->wrist_3d,
            finger2_len, &predicted_finger2, prev_valid, &raw_finger2) != 0)
        return -1;

    if (prev_valid) {
        ctx->finger1_3d = ema_point3d(
            predicted_finger1, raw_finger1, dt_filter_sec);
        ctx->finger2_3d = ema_point3d(
            predicted_finger2, raw_finger2, dt_filter_sec);
    } else {
        ctx->finger1_3d = raw_finger1;
        ctx->finger2_3d = raw_finger2;
    }

    ctx->finger_pose3d_valid = 1U;
    ctx->finger_parent_wrist = ctx->wrist_3d;
    return 0;
}

void pm_reset_finger_branch_tracker(PoseMappingContext *ctx)
{
    if (ctx == NULL) return;
    memset(ctx->finger_branch, 0, sizeof(ctx->finger_branch));
    ctx->finger_branch_ring = 0U;
    ctx->finger_branch_selected = 0U;
    ctx->finger_branch_selected_valid = 0U;
    ctx->finger_pose3d_valid = 0U;
}

/* Score one complete hand hypothesis in the wrist's moving local origin.
 * Translation of the whole arm therefore does not look like finger motion.
 * Forearm alignment is deliberately weak: a real wrist may bend backwards. */
static float finger_branch_frame_cost(
    const PoseMappingContext *ctx,
    const PoseFingerBranchState *track,
    Point3D finger1,
    Point3D finger2,
    Vec3 forearm_n,
    float finger_length,
    Point3D *mid_relative_out,
    Point3D *span_out
)
{
    Vec3 mid_relative = pm_vsub(
        pm_vscale(pm_vadd(finger1, finger2), 0.5f), ctx->wrist_3d);
    Vec3 span = pm_vsub(finger2, finger1);
    Vec3 hand_n = mid_relative;
    Vec3 lateral = pm_project_perpendicular(span, forearm_n);
    float span_length = pm_vlen(span);
    float cost;

    if (pm_vnormalize(&hand_n) != 0 || span_length <= PM_EPS ||
        pm_vlen(lateral) / span_length < PM_MIN_HAND_PLANE_QUALITY)
        return FLT_MAX;

    /* A gentle anatomical prior, never a hard straight-hand constraint. */
    cost = 0.05f * (1.0f - pm_vdot(forearm_n, hand_n));

    /* A mixed near/far pair often puts thumb and index implausibly far apart.
     * This remains soft because grip aperture and human proportions vary. */
    if (span_length > 1.25f * finger_length)
        cost += 0.06f * pm_sqrf(span_length / finger_length - 1.25f);

    if (track->prev_valid) {
        float inv_length_sq = 1.0f / pm_sqrf(finger_length);
        cost += 0.50f * point_distance_sq(mid_relative,
                                           track->prev_mid_relative) * inv_length_sq;
        cost += 0.15f * point_distance_sq(span,
                                           track->prev_span) * inv_length_sq;
    }

    *mid_relative_out = mid_relative;
    *span_out = span;
    return cost;
}

/* Operational forearm path only. Keep the legacy single-frame reconstruction
 * above unchanged for callers that explicitly use the older mapping API. */
int pm_reconstruct_finger_pose3d_tracked(
    PoseMappingContext *ctx,
    float dt_filter_sec
)
{
    RaySphereCandidate finger1_candidate[2], finger2_candidate[2];
    Point3D current_finger1[4], current_finger2[4];
    Vec3 forearm_n;
    float finger1_length, finger2_length, mean_length;
    float best_cost = FLT_MAX, second_cost = FLT_MAX;
    int finger1_count, finger2_count, best = -1;
    unsigned i;

    if (ctx == NULL) return -1;
    finger1_length = PM_SHOULDER_WIDTH_UNIT * PM_WRIST_TO_FINGER1_RATIO;
    finger2_length = PM_SHOULDER_WIDTH_UNIT * PM_WRIST_TO_FINGER2_RATIO;
    mean_length = 0.5f * (finger1_length + finger2_length);
    if (mean_length <= PM_EPS) return -1;

    forearm_n = pm_vsub(ctx->wrist_3d, ctx->elbow_3d);
    if (pm_vnormalize(&forearm_n) != 0) {
        pm_reset_finger_branch_tracker(ctx);
        return -1;
    }

    finger1_count = ray_sphere_candidates(
        pixel_unit_ray(ctx->finger1.value), ctx->wrist_3d,
        finger1_length, finger1_candidate);
    finger2_count = ray_sphere_candidates(
        pixel_unit_ray(ctx->finger2.value), ctx->wrist_3d,
        finger2_length, finger2_candidate);
    if (finger1_count <= 0 || finger2_count <= 0) {
        pm_reset_finger_branch_tracker(ctx);
        return -1;
    }

    for (i = 0U; i < 4U; ++i) {
        PoseFingerBranchState *track = &ctx->finger_branch[i];
        unsigned index1 = i & 1U;
        unsigned index2 = (i >> 1U) & 1U;
        Point3D mid_relative, span;
        float cost;

        if (index1 >= (unsigned)finger1_count ||
            index2 >= (unsigned)finger2_count) {
            track->consecutive_frames = 0U;
            track->prev_valid = 0U;
            continue;
        }

        current_finger1[i] = finger1_candidate[index1].point;
        current_finger2[i] = finger2_candidate[index2].point;
        cost = finger_branch_frame_cost(
            ctx, track, current_finger1[i], current_finger2[i],
            forearm_n, mean_length, &mid_relative, &span);
        if (cost == FLT_MAX) {
            track->consecutive_frames = 0U;
            track->prev_valid = 0U;
            continue;
        }

        track->frame_cost[ctx->finger_branch_ring] = cost;
        track->prev_mid_relative = mid_relative;
        track->prev_span = span;
        track->prev_valid = 1U;
        if (track->consecutive_frames < POSE_FINGER_BRANCH_WINDOW)
            track->consecutive_frames++;

        {
            unsigned required = ctx->finger_branch_selected_valid
                ? PM_FINGER_BRANCH_TRACKING_WINDOW
                : POSE_FINGER_BRANCH_WINDOW;

            if (track->consecutive_frames >= required) {
                float average = 0.0f;
                unsigned j;
                for (j = 0U; j < required; ++j) {
                    unsigned idx = (ctx->finger_branch_ring +
                        POSE_FINGER_BRANCH_WINDOW - j) % POSE_FINGER_BRANCH_WINDOW;
                    average += track->frame_cost[idx];
                }
                average /= (float)required;
                if (average < best_cost) {
                    second_cost = best_cost;
                    best_cost = average;
                    best = (int)i;
                } else if (average < second_cost) {
                    second_cost = average;
                }
            }
        }
    }
    ctx->finger_branch_ring = (uint8_t)(
        (ctx->finger_branch_ring + 1U) % POSE_FINGER_BRANCH_WINDOW);

    /* No candidate has filled the window yet: retain the caller's previous
     * pitch/roll (gripper stays independently observable from the 2D
     * fingertip separation). Once a candidate has filled the window, report
     * it outright, unless it would mean abandoning an already selected
     * branch without a clear lead. */
    if (best < 0 || (ctx->finger_branch_selected_valid &&
                      (unsigned)best != ctx->finger_branch_selected &&
                      second_cost - best_cost < PM_FINGER_BRANCH_SWITCH_LEAD))
        return -1;

    if (ctx->finger_pose3d_valid && ctx->finger_branch_selected_valid &&
        (unsigned)best == ctx->finger_branch_selected) {
        Point3D predicted_finger1 = pm_vadd(ctx->wrist_3d,
            pm_vsub(ctx->finger1_3d, ctx->finger_parent_wrist));
        Point3D predicted_finger2 = pm_vadd(ctx->wrist_3d,
            pm_vsub(ctx->finger2_3d, ctx->finger_parent_wrist));
        ctx->finger1_3d = ema_point3d(predicted_finger1,
                                       current_finger1[best], dt_filter_sec);
        ctx->finger2_3d = ema_point3d(predicted_finger2,
                                       current_finger2[best], dt_filter_sec);
    } else {
        ctx->finger1_3d = current_finger1[best];
        ctx->finger2_3d = current_finger2[best];
    }
    ctx->finger_parent_wrist = ctx->wrist_3d;
    ctx->finger_pose3d_valid = 1U;
    ctx->finger_branch_selected = (uint8_t)best;
    ctx->finger_branch_selected_valid = 1U;
    return 0;
}
