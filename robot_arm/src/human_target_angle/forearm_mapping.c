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

static int fm_calculate_angles_in_frame(ForearmMappingContext *ctx, float dt,
                                         Vec3 body_x, Vec3 body_y, Vec3 body_z,
                                         HumanForearmTarget *out)
{
    Vec3 f, h, r;
    float fx, fy, fz, horizontal, yaw, pitch, yr, pr;
    if (!ctx || !out) return -1;
    f = pm_vsub(ctx->pose.wrist_3d, ctx->pose.elbow_3d);
    if (pm_vnormalize(&f) != 0) return -1;
    fx = pm_vdot(f, body_x);
    fy = pm_vdot(f, body_y);
    fz = pm_vdot(f, body_z);
    horizontal = hypotf(fx, fz);
    ctx->elbow_roll_singular = horizontal < (ctx->elbow_roll_singular ? FM_AZIMUTH_LEAVE : FM_AZIMUTH_ENTER);
    pitch = atan2f(fy, horizontal) * PM_RAD_TO_DEG;
    if (!ctx->elbow_roll_singular) {
        /* Same zero/sign as legacy base, now applied to the FOREARM. */
        yaw = fm_wrap180(atan2f(fx, fz) * PM_RAD_TO_DEG);
        ctx->raw_elbow_roll_deg = yaw;
        if (!ctx->elbow_roll_initialized) ctx->elbow_roll_unwrapped_deg = yaw;
        else ctx->elbow_roll_unwrapped_deg = pm_filter_angle_continuous(
            ctx->elbow_roll_unwrapped_deg, yaw, PM_JOINT_ANGLE_TAU_SEC,
            PM_JOINT_DEADBAND_DEG, dt, 1U);
        ctx->elbow_roll_initialized = 1U;
    }
    /* At a pole retain both the output yaw and the last observed raw heading
     * used to build the wrist reference. With no history both start at zero,
     * but elbow_roll_observable=0 marks that value as unobserved. */
    ctx->raw_elbow_pitch_deg = pitch;
    if (!ctx->angle_valid) ctx->elbow_pitch_deg = pitch;
    else ctx->elbow_pitch_deg = pm_filter_angle_continuous(
        ctx->elbow_pitch_deg, pitch, PM_JOINT_ANGLE_TAU_SEC,
        PM_JOINT_DEADBAND_DEG, dt, 0U);
    ctx->angle_valid = 1U;

    /* Forearm-local "up": elevation tangent at fixed azimuth. Unlike
     * project(Body Y, f), it remains defined at the poles using held azimuth.
     * Projection removes small noise components while yaw is held. */
    yr = ctx->raw_elbow_roll_deg * PM_DEG_TO_RAD;
    pr = pitch * PM_DEG_TO_RAD;
    h = pm_vadd(pm_vscale(body_x, sinf(yr)), pm_vscale(body_z, cosf(yr)));
    r = pm_vsub(pm_vscale(body_y, cosf(pr)), pm_vscale(h, sinf(pr)));
    r = pm_project_perpendicular(r, f);
    if (pm_vnormalize(&r) != 0) return -1;
    ctx->wrist_reference = r;
    out->elbow_roll_deg = fm_wrap180(ctx->elbow_roll_unwrapped_deg);
    out->elbow_pitch_deg = pm_clampf(ctx->elbow_pitch_deg, -90.0f, 90.0f);
    out->elbow_roll_observable = !ctx->elbow_roll_singular;
    return 0;
}

int fm_calculate_angles(ForearmMappingContext *ctx, float dt,
                        HumanForearmTarget *out)
{
    Vec3 body_x, body_y, body_z;
    if (!ctx || !out || pm_get_stable_body_frame(&ctx->pose,
                                                &body_x, &body_y, &body_z) != 0) return -1;
    return fm_calculate_angles_in_frame(ctx, dt, body_x, body_y, body_z, out);
}

static int fm_calculate_hand_impl(ForearmMappingContext *ctx, float span,
                                  float age_dt, float filter_dt,
                                  uint8_t hand_fresh, HumanForearmTarget *out)
{
    HumanJointTarget hand;
    /* No observed heading yet: the pole's arbitrary initial yaw cannot
     * define a measured roll zero. Caller holds/defaults hand fields. */
    if (!ctx || !out || !ctx->elbow_roll_initialized) return -1;
    memset(&hand, 0, sizeof(hand));
    if (pm_calculate_hand_with_reference_ex(&ctx->pose, span, age_dt, filter_dt,
                                            &ctx->wrist_reference,
                                            hand_fresh, &hand) != 0) return -1;
    /* HUMAN wrist flexion is positive about the projected thumb-to-index
     * (Finger1->Finger2) axis. */
    out->wrist_pitch_deg = fm_wrap180(hand.wrist_pitch_deg);
    out->wrist_roll_deg = fm_wrap180(hand.wrist_roll_deg);
    out->gripper_norm = hand.gripper_norm;
    out->hand_fresh = hand_fresh;
    out->wrist_valid = 1U;
    return 0;
}

int fm_calculate_hand(ForearmMappingContext *ctx, float span, float age_dt,
                     float filter_dt, HumanForearmTarget *out)
{
    return fm_calculate_hand_impl(ctx, span, age_dt, filter_dt, 1U, out);
}

static int hold_or_invalid(ForearmMappingContext *ctx, HumanForearmTarget *out)
{
    if (ctx->last_target_valid && ctx->pose.target_age_sec <= PM_TARGET_HOLD_SEC) {
        *out = ctx->last_target;
        out->hand_fresh = 0U;
        out->elbow_roll_observable = 0U;
        return 0;
    }
    memset(out, 0, sizeof(*out));
    ctx->last_target_valid = 0U;
    ctx->pose.hand_angle_valid = 0U;
    ctx->pose.prev_hand_normal_valid = 0U;
    ctx->pose.prev_roll_raw_valid = 0U;
    ctx->pose.prev_pitch_raw_valid = 0U;
    pm_reset_finger_branch_tracker(&ctx->pose);
    return -1;
}

int forearm_mapping_update(ForearmMappingContext *ctx, const HumanPose2D *pose,
                           PoseArmSide side, float dt, HumanForearmTarget *out)
{
    PoseMappingContext *p;
    HumanForearmTarget fresh;
    HumanPose2D finite_pose;
    float filter_dt, span = 0.0f;
    int finger_status = -1;
    uint8_t gripper_fresh = 0U, hand_updated = 0U;
    uint8_t selected_before, hand_observed;
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    if (!ctx || !pose || !ctx->pose.initialized ||
        (side != POSE_ARM_LEFT && side != POSE_ARM_RIGHT)) return -1;
    if (ctx->stereo_input_active) forearm_mapping_init(ctx);
    p = &ctx->pose;
    if (p->last_arm_side_valid && p->last_arm_side != side) {
        forearm_mapping_init(ctx);
    }
    p->last_arm_side = side;
    p->last_arm_side_valid = 1U;
    if (p->last_frame_id_valid && p->last_frame_id == pose->frame_id)
        return hold_or_invalid(ctx, out); /* no re-filtering or time aging */
    p->last_frame_id = pose->frame_id;
    p->last_frame_id_valid = 1U;
    if (p->finger_branch_elapsed_frames < POSE_FINGER_BRANCH_WINDOW)
        p->finger_branch_elapsed_frames++;
    p->gripper_last_hold = 1U;
    p->gripper_hold_reason = PM_GRIPPER_HOLD_MISSING;
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
    if (pm_update_stable_body_frame(p, filter_dt) != 0 ||
        fm_calculate_angles(ctx, filter_dt, &fresh) != 0)
        return hold_or_invalid(ctx, out);
    if (pm_fingers_both_fresh(p) &&
        pm_update_gripper_from_2d(p, span, &fresh.gripper_norm) == 0)
        gripper_fresh = 1U;
    if (pm_fingers_both_fresh(p)) {
        p->finger_gap_frames = 0U;
    } else {
        if (p->finger_gap_frames < 0xFFFFU) p->finger_gap_frames++;
        /* A brief CNN dropout should not throw away branch evidence that
         * was still accumulating (or already selected) toward this same
         * hand pose. Only a sustained loss is treated as the hand possibly
         * having moved to a different, unrelated configuration. */
        if (p->finger_gap_frames > PM_FINGER_DROPOUT_TOLERANCE_FRAMES)
            pm_reset_finger_branch_tracker(p);
    }
    selected_before = p->finger_branch_selected_valid;
    hand_observed = pm_fingers_both_fresh(p);
    if (hand_observed)
        finger_status = pm_reconstruct_finger_pose3d_tracked(p, filter_dt);
    if (finger_status != 0 && !p->finger_branch_selected_valid &&
        p->finger_branch_elapsed_frames >= POSE_FINGER_BRANCH_WINDOW)
        finger_status = pm_select_initial_finger_branch(p);
    /* The initial estimate is based on the full 60-frame score history,
     * possibly with no finger detection in the current frame. */
    if (!selected_before && p->finger_branch_selected_valid)
        hand_observed = 0U;
    if (finger_status == 0 &&
        fm_calculate_hand_impl(ctx, span, dt, filter_dt,
                               hand_observed, &fresh) == 0)
        hand_updated = 1U;
    if (!hand_updated) {
        if (ctx->last_target_valid) {
            fresh.wrist_pitch_deg = ctx->last_target.wrist_pitch_deg;
            fresh.wrist_roll_deg = ctx->last_target.wrist_roll_deg;
            fresh.wrist_valid = ctx->last_target.wrist_valid;
        }
        if (!gripper_fresh) fresh.gripper_norm = ctx->last_target_valid
            ? ctx->last_target.gripper_norm : 1.0f;
    }
    fresh.frame_id = pose->frame_id;
    /* At 60 frames, major angles may start even without a hand solution.
     * Agent2 holds wrist/gripper at the current robot command until then. */
    if (!ctx->last_target_valid && !hand_updated &&
        p->finger_branch_elapsed_frames < POSE_FINGER_BRANCH_WINDOW) {
        *out = fresh;
        return -1;
    }
    fresh.valid = 1U;
    p->target_age_sec = 0.0f;
    ctx->last_target = fresh;
    ctx->last_target_valid = 1U;
    *out = fresh;
    return 1;
}

static int measured_point_valid(Point3D point)
{
    return point.valid && isfinite(point.x) && isfinite(point.y) &&
           isfinite(point.z) && point.z > 0.0f;
}

int forearm_mapping_update_stereo(ForearmMappingContext *ctx, const HumanPose2D *image_pose,
                                  const HumanPose3D *measured_pose, PoseArmSide side,
                                  float dt, HumanForearmTarget *out)
{
    PoseMappingContext *pose;
    HumanForearmTarget fresh;
    HumanPose2D finite_pose;
    float filter_dt, span;
    uint8_t hand_updated = 0U, gripper_fresh = 0U;
    if (out == NULL) return -1;
    memset(out, 0, sizeof(*out));
    if (ctx == NULL || image_pose == NULL || measured_pose == NULL || !ctx->pose.initialized ||
        (side != POSE_ARM_LEFT && side != POSE_ARM_RIGHT)) return -1;
    if (!ctx->stereo_input_active ||
        (ctx->pose.last_arm_side_valid && ctx->pose.last_arm_side != side)) {
        forearm_mapping_init(ctx);
        ctx->stereo_input_active = 1U;
    }
    pose = &ctx->pose;
    if (pose->last_frame_id_valid && pose->last_frame_id == image_pose->frame_id)
        return hold_or_invalid(ctx, out);
    pose->last_frame_id = image_pose->frame_id;
    pose->last_frame_id_valid = 1U;
    pose->last_arm_side = side;
    pose->last_arm_side_valid = 1U;
    if (!isfinite(dt) || dt <= 0.0f) dt = 1.0f / PM_DEFAULT_FPS;
    filter_dt = pm_sanitize_filter_dt(dt);
    if (ctx->last_target_valid) pose->target_age_sec += dt;
    if (!image_pose->valid || !measured_pose->valid ||
        measured_pose->frame_id != image_pose->frame_id ||
        !measured_point_valid(measured_pose->elbow) || !measured_point_valid(measured_pose->wrist))
        return hold_or_invalid(ctx, out);
    finite_pose = *image_pose;
    {
        Point2D *points[] = {&finite_pose.shoulder_l, &finite_pose.shoulder_r, &finite_pose.elbow,
                            &finite_pose.wrist, &finite_pose.finger1, &finite_pose.finger2};
        unsigned index;
        for (index = 0; index < sizeof(points) / sizeof(points[0]); ++index)
            if (!isfinite(points[index]->x) || !isfinite(points[index]->y)) points[index]->valid = 0;
    }
    pm_update_all_landmarks(pose, &finite_pose, filter_dt);
    if (!pose->elbow.fresh || !pose->wrist.fresh) return hold_or_invalid(ctx, out);
    pose->shoulder_l_3d = measured_point_valid(measured_pose->shoulder_l)
        ? measured_pose->shoulder_l : (Point3D){0};
    pose->shoulder_r_3d = measured_point_valid(measured_pose->shoulder_r)
        ? measured_pose->shoulder_r : (Point3D){0};
    pose->elbow_3d = measured_pose->elbow;
    pose->wrist_3d = measured_pose->wrist;
    pose->major_pose3d_valid = 1U;
    pose->finger_pose3d_valid = 0U;
    span = fmaxf(pm_distance_2d(pose->elbow.value, pose->wrist.value),
                  PM_MIN_SHOULDER_WIDTH_PX);
    memset(&fresh, 0, sizeof(fresh));
    if (fm_calculate_angles_in_frame(ctx, filter_dt, pm_vec3(-1.0f, 0.0f, 0.0f),
                                     pm_vec3(0.0f, 1.0f, 0.0f), pm_vec3(0.0f, 0.0f, -1.0f),
                                     &fresh) != 0) return hold_or_invalid(ctx, out);
    if (pm_fingers_both_fresh(pose) &&
        measured_point_valid(measured_pose->finger1) && measured_point_valid(measured_pose->finger2)) {
        pose->finger1_3d = measured_pose->finger1;
        pose->finger2_3d = measured_pose->finger2;
        pose->finger_parent_wrist = pose->wrist_3d;
        pose->finger_pose3d_valid = 1U;
        if (pm_update_gripper_from_2d(pose, span, &fresh.gripper_norm) == 0) gripper_fresh = 1U;
        if (fm_calculate_hand_impl(ctx, span, dt, filter_dt, 1U, &fresh) == 0) hand_updated = 1U;
    }
    if (!hand_updated) {
        if (ctx->last_target_valid) {
            fresh.wrist_pitch_deg = ctx->last_target.wrist_pitch_deg;
            fresh.wrist_roll_deg = ctx->last_target.wrist_roll_deg;
            fresh.wrist_valid = ctx->last_target.wrist_valid;
        }
        if (!gripper_fresh) fresh.gripper_norm = ctx->last_target_valid
            ? ctx->last_target.gripper_norm : 1.0f;
    }
    fresh.frame_id = image_pose->frame_id;
    fresh.valid = 1U;
    pose->target_age_sec = 0.0f;
    ctx->last_target = fresh;
    ctx->last_target_valid = 1U;
    *out = fresh;
    return 1;
}
