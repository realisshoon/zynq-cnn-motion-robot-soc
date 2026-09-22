#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../../src/human_target_angle/pose_mapping_internal.h"

static void near(float actual, float expected, float tolerance)
{
    assert(isfinite(actual));
    assert(fabsf(actual - expected) <= tolerance);
}

static void vector_near(Vec3 a, Vec3 b)
{
    near(a.x, b.x, 0.0001f);
    near(a.y, b.y, 0.0001f);
    near(a.z, b.z, 0.0001f);
}

static void check_frame(Vec3 x, Vec3 y, Vec3 z)
{
    near(pm_vlen(x), 1.0f, 0.0001f);
    near(pm_vlen(y), 1.0f, 0.0001f);
    near(pm_vlen(z), 1.0f, 0.0001f);
    near(pm_vdot(x, y), 0.0f, 0.0001f);
    near(pm_vdot(y, z), 0.0f, 0.0001f);
    near(pm_vdot(z, x), 0.0f, 0.0001f);
    vector_near(pm_vcross(x, y), z);
    assert(y.y > 0.0f);
    /* Independent component formula for projected camera up. */
    {
        float n = sqrtf(1.0f - x.y * x.y);
        vector_near(y, pm_vec3(-x.y * x.x / n, n, -x.y * x.z / n));
    }
}

static void first_log_frame(void)
{
    PoseMappingContext ctx;
    HumanJointTarget out = {0};
    Vec3 x, y, z, old_y, old_z, u;
    pose_mapping_init(&ctx);
    ctx.shoulder_l_3d = pm_vec3(0.298f, 0.750f, 7.253f);
    ctx.shoulder_r_3d = pm_vec3(-0.585f, 0.750f, 6.783f);
    ctx.elbow_3d = pm_vec3(-0.754f, 0.015f, 6.766f);
    u = pm_vsub(ctx.elbow_3d, ctx.shoulder_r_3d);
    ctx.wrist_3d = pm_vadd(ctx.elbow_3d, u); /* synthetic straight forearm */
    assert(pm_build_body_frame(ctx.shoulder_l_3d, ctx.shoulder_r_3d, &x, &y, &z) == 0);
    check_frame(x, y, z);
    vector_near(x, pm_vec3(-0.882740f, 0.0f, -0.469862f));
    vector_near(y, pm_vec3(0.0f, 1.0f, 0.0f));
    vector_near(z, pm_vec3(0.469862f, 0.0f, -0.882740f));
    /* Reproduce the old camera-forward forcing separately. */
    old_z = pm_vscale(z, -1.0f);
    old_y = pm_vcross(old_z, x);
    vector_near(old_y, pm_vec3(0.0f, -1.0f, 0.0f));
    pm_vnormalize(&u);
    near(atan2f(pm_vdot(u, old_y), hypotf(pm_vdot(u, x), pm_vdot(u, old_z)))
         * PM_RAD_TO_DEG, 76.9878f, 0.01f);
    assert(pm_calculate_major_angles(&ctx, POSE_ARM_RIGHT, 0.05f, &out) == 0);
    near(out.shoulder_deg, -76.9878f, 0.01f);
    near(out.base_deg, 112.2812f, 0.01f);
    near(out.elbow_deg, 180.0f, 0.05f);
    printf("P3 regression: shoulder +76.9878 -> %.4f; base %.4f\n",
           out.shoulder_deg, out.base_deg);
}

/* Analytic world geometry, independent of the implementation's body axes. */
static void set_pose(PoseMappingContext *ctx, float yaw, float base, float elevation,
                     float roll, float pitch)
{
    float c = cosf(yaw * PM_DEG_TO_RAD), s = sinf(yaw * PM_DEG_TO_RAD);
    float b = base * PM_DEG_TO_RAD, e = elevation * PM_DEG_TO_RAD;
    float r = roll * PM_DEG_TO_RAD, p = pitch * PM_DEG_TO_RAD;
    Vec3 x = pm_vec3(-c, 0.0f, s);
    Vec3 y = pm_vec3(0.0f, 1.0f, 0.0f);
    Vec3 z = pm_vec3(-s, 0.0f, -c);
    Vec3 upper = pm_vadd(pm_vscale(x, sinf(b) * cosf(e)),
                        pm_vadd(pm_vscale(y, sinf(e)), pm_vscale(z, cosf(b) * cosf(e))));
    Vec3 span = pm_vadd(pm_vscale(x, cosf(r)), pm_vscale(y, sinf(r)));
    Vec3 forward = pm_vadd(pm_vscale(z, cosf(p)), pm_vscale(y, -sinf(p)));
    Vec3 center;
    ctx->shoulder_l_3d = pm_vscale(x, -0.5f);
    ctx->shoulder_r_3d = pm_vscale(x, 0.5f);
    ctx->shoulder_l.value.x = 640.0f + 100.0f * c;
    ctx->shoulder_r.value.x = 640.0f - 100.0f * c;
    ctx->elbow_3d = pm_vadd(ctx->shoulder_r_3d, upper);
    ctx->wrist_3d = pm_vadd(ctx->elbow_3d, z);
    center = pm_vadd(ctx->wrist_3d, forward);
    ctx->finger1_3d = pm_vsub(center, pm_vscale(span, 0.2f));
    ctx->finger2_3d = pm_vadd(center, pm_vscale(span, 0.2f));
    ctx->finger1.value.x = 0.0f;
    ctx->finger2.value.x = 20.0f;
}

static void simple_angles_and_views(void)
{
    const float elevations[] = {-90.0f, -30.0f, 0.0f, 30.0f, 90.0f};
    const float rolls[] = {-30.0f, 0.0f, 30.0f};
    PoseMappingContext ctx;
    HumanJointTarget out = {0};
    Vec3 x, y, z;
    unsigned i;
    for (i = 0; i < sizeof(elevations) / sizeof(elevations[0]); ++i) {
        pose_mapping_init(&ctx);
        set_pose(&ctx, 0.0f, 0.0f, elevations[i], 0.0f, 0.0f);
        assert(pm_calculate_major_angles(&ctx, POSE_ARM_RIGHT, 0.05f, &out) == 0);
        near(out.shoulder_deg, elevations[i], 0.001f);
        if (elevations[i] == 0.0f) near(out.base_deg, 0.0f, 0.001f);
    }
    for (i = 0; i < 3; ++i) {
        pose_mapping_init(&ctx);
        set_pose(&ctx, 90.0f * i, 90.0f, 0.0f, rolls[i], 0.0f);
        assert(pm_calculate_major_angles(&ctx, POSE_ARM_RIGHT, 0.05f, &out) == 0);
        near(out.base_deg, 90.0f, 0.001f);
        assert(pm_get_stable_body_frame(&ctx, &x, &y, &z) == 0);
        check_frame(x, y, z);
        near(x.x, i == 0 ? -1.0f : (i == 1 ? 0.0f : 1.0f), 0.0001f);
        near(z.z, i == 0 ? -1.0f : (i == 1 ? 0.0f : 1.0f), 0.0001f);
        assert(pm_calculate_hand_angles_and_gripper(&ctx, 100.0f, 0.05f, 0.05f, &out) == 0);
        near(out.wrist_roll_deg, rolls[i], 0.001f);
        near(out.wrist_pitch_deg, 0.0f, 0.001f);
    }
    for (i = 0; i < 3; ++i) {
        pose_mapping_init(&ctx);
        set_pose(&ctx, 0.0f, 0.0f, 0.0f, 0.0f, rolls[i]);
        assert(pm_calculate_major_angles(&ctx, POSE_ARM_RIGHT, 0.05f, &out) == 0);
        assert(pm_calculate_hand_angles_and_gripper(&ctx, 100.0f, 0.05f, 0.05f, &out) == 0);
        near(out.wrist_pitch_deg, rolls[i], 0.001f);
    }
    /* Tilted shoulders: Y is projected up, not necessarily camera (0,1,0). */
    assert(pm_build_body_frame(pm_vec3(0,0,0), pm_vec3(-1,0.5f,0.3f), &x,&y,&z) == 0);
    check_frame(x,y,z);
    pose_mapping_init(&ctx);
    set_pose(&ctx,0,0,0,0,0);
    ctx.wrist_3d = ctx.shoulder_r_3d; /* fully folded inner angle */
    assert(pm_calculate_major_angles(&ctx,POSE_ARM_RIGHT,0.05f,&out) == 0);
    near(out.elbow_deg,0.0f,0.001f);
}

static void continuity(void)
{
    PoseMappingContext ctx;
    HumanJointTarget out = {0}, prev = {0};
    Vec3 prev_x = {0}, prev_y = {0}, prev_z = {0};
    int i;
    pose_mapping_init(&ctx);
    for (i = 0; i <= 180; ++i) {
        set_pose(&ctx, (float)i, 30.0f, 20.0f, 20.0f, 0.0f);
        assert(pm_calculate_major_angles(&ctx, POSE_ARM_RIGHT, 0.05f, &out) == 0);
        assert(pm_calculate_hand_angles_and_gripper(&ctx, 100.0f, 0.05f, 0.05f, &out) == 0);
        check_frame(ctx.body_x_axis, ctx.body_y_axis, ctx.body_z_axis);
        if (i > 0) {
            assert(pm_vdot(prev_x, ctx.body_x_axis) > 0.99f);
            assert(pm_vdot(prev_y, ctx.body_y_axis) > 0.99f);
            assert(pm_vdot(prev_z, ctx.body_z_axis) > 0.99f);
            assert(fabsf(pm_wrap180(out.base_deg - prev.base_deg)) < 3.0f);
            assert(fabsf(out.shoulder_deg - prev.shoulder_deg) < 0.01f);
            assert(fabsf(pm_wrap180(out.wrist_roll_deg - prev.wrist_roll_deg)) < 3.0f);
        }
        prev = out;
        prev_x = ctx.body_x_axis; prev_y = ctx.body_y_axis; prev_z = ctx.body_z_axis;
    }
    assert(ctx.shoulder_l.value.x < ctx.shoulder_r.value.x);
    assert(ctx.body_x_axis.x > 0.99f);
    /* Both directions across +/-180: public values wrap; physical deltas do not. */
    for (int direction = -1; direction <= 1; direction += 2) {
        pose_mapping_init(&ctx);
        for (i = 0; i <= 40; ++i) {
            float angle = direction * (170.0f + (float)i);
            set_pose(&ctx, 0.0f, angle, 20.0f, angle, 0.0f);
            assert(pm_calculate_major_angles(&ctx, POSE_ARM_RIGHT, 0.05f, &out) == 0);
            assert(pm_calculate_hand_angles_and_gripper(&ctx, 100.0f, 0.05f, 0.05f, &out) == 0);
            if (i > 0) {
                float db = direction * pm_wrap180(out.base_deg - prev.base_deg);
                float dr = direction * pm_wrap180(out.wrist_roll_deg - prev.wrist_roll_deg);
                assert(db >= 0.0f && db < 1.1f);
                assert(dr >= 0.0f && dr < 1.1f);
            }
            assert(fabsf(out.base_deg) <= 180.0f && fabsf(out.wrist_roll_deg) <= 180.0f);
            prev = out;
        }
        assert(direction * out.base_deg < -140.0f);
        assert(direction * out.wrist_roll_deg < -140.0f);
    }
}

static void degenerate_frames(void)
{
    PoseMappingContext ctx;
    Vec3 x, y, z, keep_x, keep_y, keep_z;
    pose_mapping_init(&ctx);
    ctx.shoulder_l_3d = pm_vec3(0,0,0);
    ctx.shoulder_r_3d = pm_vec3(0,1,0);
    assert(pm_update_stable_body_frame(&ctx, 0.05f) == -1);
    assert(!ctx.body_frame_valid);
    set_pose(&ctx, 0,0,0,0,0);
    assert(pm_update_stable_body_frame(&ctx, 0.05f) == 0);
    keep_x = ctx.body_x_axis; keep_y = ctx.body_y_axis; keep_z = ctx.body_z_axis;
    for (int i = -1; i <= 1; ++i) {
        ctx.shoulder_l_3d = pm_vec3(0,0,0);
        ctx.shoulder_r_3d = pm_vec3(i * 0.001f,1,0);
        assert(pm_build_body_frame(ctx.shoulder_l_3d,ctx.shoulder_r_3d,&x,&y,&z) == -1);
        assert(pm_update_stable_body_frame(&ctx,0.05f) == -1);
        vector_near(ctx.body_x_axis,keep_x);
        vector_near(ctx.body_y_axis,keep_y);
        vector_near(ctx.body_z_axis,keep_z);
    }
    assert(pm_build_body_frame(pm_vec3(0,0,0),pm_vec3(0,0,0),&x,&y,&z) == -1);
    assert(pm_build_body_frame(pm_vec3(0,0,0),pm_vec3(NAN,1,0),&x,&y,&z) == -1);
    /* Raw axes can both be valid while their filtered blend hits the pole. */
    {
        float a = pm_alpha_from_tau(0.05f, PM_BODY_FRAME_TAU_SEC);
        float rx = 0.02f * (1.0f - a) / a;
        ctx.body_x_axis = pm_vec3(-0.02f, sqrtf(1.0f-0.02f*0.02f), 0);
        assert(pm_build_body_frame(pm_vec3(0,0,0),ctx.body_x_axis,
                                  &ctx.body_x_axis,&ctx.body_y_axis,&ctx.body_z_axis) == 0);
        keep_x = ctx.body_x_axis; keep_y = ctx.body_y_axis; keep_z = ctx.body_z_axis;
        ctx.shoulder_l_3d = pm_vec3(0,0,0);
        ctx.shoulder_r_3d = pm_vec3(rx,sqrtf(1.0f-rx*rx),0);
        assert(pm_build_body_frame(ctx.shoulder_l_3d,ctx.shoulder_r_3d,&x,&y,&z) == 0);
        assert(pm_update_stable_body_frame(&ctx,0.05f) == -1);
        vector_near(ctx.body_x_axis,keep_x);
        vector_near(ctx.body_y_axis,keep_y);
        vector_near(ctx.body_z_axis,keep_z);
    }
    pose_mapping_reset(&ctx);
    set_pose(&ctx,0,0,0,0,0);
    assert(pm_update_stable_body_frame(&ctx,0.05f) == 0);
    check_frame(ctx.body_x_axis,ctx.body_y_axis,ctx.body_z_axis);
}

int main(void)
{
    first_log_frame();
    simple_angles_and_views();
    continuity();
    degenerate_frames();
    puts("test_body_frame: PASS (signs, views, tilt, continuity, wrap, degeneracy)");
    return 0;
}
