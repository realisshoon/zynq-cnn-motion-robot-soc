#include "forearm_mapping_internal.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

float fm_wrap180(float d)
{
    d = fmodf(d + 180.0f, 360.0f);
    if (d < 0.0f) d += 360.0f;
    return d - 180.0f;
}

int forearm_mapping_init(ForearmMappingContext *ctx)
{
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(*ctx));
    return pose_mapping_init(&ctx->pose);
}

int forearm_mapping_set_table(ForearmMappingContext *ctx, Point3D up,
                             Point3D forward, uint8_t calibrated)
{
    TableFrame table;
    if (!ctx || !ctx->pose.initialized || pm_vnormalize(&up) != 0 ||
        pm_vnormalize(&forward) != 0) return -1;
    table.z = up;
    table.x = pm_project_perpendicular(forward, up);
    if (pm_vlen(table.x) < 0.01f || pm_vnormalize(&table.x) != 0) return -1;
    table.y = pm_vcross(table.z, table.x);
    if (pm_vnormalize(&table.y) != 0) return -1;
    table.x = pm_vcross(table.y, table.z);
    table.valid = 1U;
    table.calibrated = calibrated ? 1U : 0U;
    forearm_mapping_init(ctx);
    ctx->table = table;
    return 0;
}

int fm_calculate_angles(ForearmMappingContext *ctx, float dt,
                        HumanForearmTarget *out)
{
    Vec3 f, h, r;
    float fx, fy, fz, horizontal, yaw, pitch, yr, pr;
    if (!ctx || !out || !ctx->table.valid) return -1;
    f = pm_vsub(ctx->pose.wrist_3d, ctx->pose.elbow_3d);
    if (pm_vnormalize(&f) != 0) return -1;
    fx = pm_vdot(f, ctx->table.x);
    fy = pm_vdot(f, ctx->table.y);
    fz = pm_vdot(f, ctx->table.z);
    horizontal = hypotf(fx, fy);
    ctx->yaw_singular = horizontal < (ctx->yaw_singular ? FM_YAW_LEAVE : FM_YAW_ENTER);
    pitch = atan2f(fz, horizontal) * PM_RAD_TO_DEG;
    if (!ctx->yaw_singular) {
        yaw = fm_wrap180(atan2f(fy, fx) * PM_RAD_TO_DEG);
        ctx->raw_yaw_deg = yaw;
        if (!ctx->yaw_initialized) ctx->yaw_unwrapped_deg = yaw;
        else ctx->yaw_unwrapped_deg = pm_filter_angle_continuous(
            ctx->yaw_unwrapped_deg, yaw, PM_JOINT_ANGLE_TAU_SEC,
            PM_JOINT_DEADBAND_DEG, dt, 1U);
        ctx->yaw_initialized = 1U;
    }
    /* At a pole retain both the output yaw and the last observed raw heading
     * used to build the wrist reference. With no history both start at zero,
     * but yaw_observable=0 explicitly prevents treating that as measured yaw. */
    ctx->raw_pitch_deg = pitch;
    if (!ctx->angle_valid) ctx->pitch_deg = pitch;
    else ctx->pitch_deg = pm_filter_angle_continuous(
        ctx->pitch_deg, pitch, PM_JOINT_ANGLE_TAU_SEC,
        PM_JOINT_DEADBAND_DEG, dt, 0U);
    ctx->angle_valid = 1U;

    /* Forearm-local "up": elevation tangent at fixed azimuth. Unlike
     * project(table Z, f), it remains defined at the poles using held yaw.
     * Projection removes small noise components while yaw is held. */
    yr = ctx->raw_yaw_deg * PM_DEG_TO_RAD;
    pr = pitch * PM_DEG_TO_RAD;
    h = pm_vadd(pm_vscale(ctx->table.x, cosf(yr)),
                pm_vscale(ctx->table.y, sinf(yr)));
    r = pm_vsub(pm_vscale(ctx->table.z, cosf(pr)), pm_vscale(h, sinf(pr)));
    r = pm_project_perpendicular(r, f);
    if (pm_vnormalize(&r) != 0) return -1;
    ctx->wrist_reference = r;
    out->forearm_yaw_deg = fm_wrap180(ctx->yaw_unwrapped_deg);
    out->forearm_pitch_deg = pm_clampf(ctx->pitch_deg, -90.0f, 90.0f);
    out->yaw_observable = !ctx->yaw_singular;
    out->calibrated = ctx->table.calibrated;
    return 0;
}

int fm_calculate_hand(ForearmMappingContext *ctx, float span, float age_dt,
                     float filter_dt, HumanForearmTarget *out)
{
    HumanJointTarget hand;
    /* No observed heading yet: the pole's arbitrary initial yaw cannot
     * define a measured roll zero. Caller holds/defaults hand fields. */
    if (!ctx || !out || !ctx->yaw_initialized) return -1;
    memset(&hand, 0, sizeof(hand));
    if (pm_calculate_hand_with_reference(&ctx->pose, span, age_dt, filter_dt,
                                         &ctx->wrist_reference, &hand) != 0) return -1;
    /* Legacy pitch is positive about finger1->finger2. The five-axis
     * flexion convention is positive toward H cross S, so negate it here.
     * This is a human wrist convention, never a servo mounting correction. */
    out->wrist_pitch_deg = fm_wrap180(-hand.wrist_pitch_deg);
    out->wrist_roll_deg = fm_wrap180(hand.wrist_roll_deg);
    out->gripper_norm = hand.gripper_norm;
    out->hand_fresh = 1U;
    return 0;
}

static int hold_or_invalid(ForearmMappingContext *ctx, HumanForearmTarget *out)
{
    if (ctx->last_target_valid && ctx->pose.target_age_sec <= PM_TARGET_HOLD_SEC) {
        *out = ctx->last_target;
        out->hand_fresh = 0U;
        out->yaw_observable = 0U;
        return 0;
    }
    memset(out, 0, sizeof(*out));
    ctx->last_target_valid = 0U;
    ctx->pose.hand_angle_valid = 0U;
    ctx->pose.prev_hand_normal_valid = 0U;
    ctx->pose.prev_roll_raw_valid = 0U;
    ctx->pose.finger_pose3d_valid = 0U;
    return -1;
}

int forearm_mapping_update(ForearmMappingContext *ctx, const HumanPose2D *pose,
                           PoseArmSide side, float dt, HumanForearmTarget *out)
{
    PoseMappingContext *p;
    HumanForearmTarget fresh;
    HumanPose2D finite_pose;
    float filter_dt, span = 0.0f;
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    if (!ctx || !pose || !ctx->pose.initialized || !ctx->table.valid ||
        (side != POSE_ARM_LEFT && side != POSE_ARM_RIGHT)) return -1;
    p = &ctx->pose;
    if (p->last_arm_side_valid && p->last_arm_side != side) {
        TableFrame table = ctx->table;
        forearm_mapping_init(ctx);
        ctx->table = table;
    }
    p->last_arm_side = side;
    p->last_arm_side_valid = 1U;
    if (p->last_frame_id_valid && p->last_frame_id == pose->frame_id)
        return hold_or_invalid(ctx, out); /* no re-filtering or time aging */
    p->last_frame_id = pose->frame_id;
    p->last_frame_id_valid = 1U;
    if (!isfinite(dt) || dt <= 0.0f) dt = 1.0f / PM_DEFAULT_FPS;
    filter_dt = pm_sanitize_filter_dt(dt);
    if (ctx->last_target_valid) p->target_age_sec += dt;
    finite_pose = *pose;
    {
        Point2D *points[] = {&finite_pose.shoulder_l, &finite_pose.shoulder_r,
                            &finite_pose.elbow, &finite_pose.wrist,
                            &finite_pose.finger1, &finite_pose.finger2};
        for (unsigned i = 0; i < sizeof(points)/sizeof(points[0]); ++i)
            if (!isfinite(points[i]->x) || !isfinite(points[i]->y)) points[i]->valid = 0U;
    }
    pm_update_all_landmarks(p, &finite_pose, filter_dt);
    if (!pm_major_all_fresh(p) ||
        pm_reconstruct_major_pose3d(p, side, filter_dt, &span) != 0)
        return hold_or_invalid(ctx, out);

    memset(&fresh, 0, sizeof(fresh));
    if (fm_calculate_angles(ctx, filter_dt, &fresh) != 0)
        return hold_or_invalid(ctx, out);
    if (!pm_fingers_both_fresh(p) ||
        pm_reconstruct_finger_pose3d(p, filter_dt) != 0 ||
        fm_calculate_hand(ctx, span, dt, filter_dt, &fresh) != 0) {
        if (ctx->last_target_valid) {
            fresh.wrist_pitch_deg = ctx->last_target.wrist_pitch_deg;
            fresh.wrist_roll_deg = ctx->last_target.wrist_roll_deg;
            fresh.gripper_norm = ctx->last_target.gripper_norm;
        } else {
            fresh.gripper_norm = 1.0f; /* default only; hand_fresh remains 0 */
        }
    }
    fresh.frame_id = pose->frame_id;
    fresh.valid = 1U;
    p->target_age_sec = 0.0f;
    ctx->last_target = fresh;
    ctx->last_target_valid = 1U;
    *out = fresh;
    return 1;
}
