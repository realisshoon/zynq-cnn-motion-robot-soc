#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../../src/human_target_angle/forearm_mapping_internal.h"
#include "human_target_angle/agent1_forearm_stage.h"
#include "uart_pose/uart_pose_protocol.h"

static void near(float actual, float expected, float tolerance)
{
    if (fabsf(actual - expected) > tolerance)
        fprintf(stderr, "actual=%f expected=%f tolerance=%f\n", actual, expected, tolerance);
    assert(isfinite(actual) && fabsf(actual - expected) <= tolerance);
}
static void init(ForearmMappingContext *c)
{
    assert(forearm_mapping_init(c) == 0);
    c->pose.shoulder_l_3d=pm_vec3(-0.5f,0,0);
    c->pose.shoulder_r_3d=pm_vec3(0.5f,0,0);
    assert(pm_update_stable_body_frame(&c->pose,0.05f)==0);
}
static HumanForearmTarget direction(ForearmMappingContext *c, Vec3 f)
{
    HumanForearmTarget t = {0};
    c->pose.elbow_3d = pm_vec3(3,4,5); /* translation must not affect angles */
    c->pose.wrist_3d = pm_vadd(c->pose.elbow_3d, f);
    assert(fm_calculate_angles(c, 0.05f, &t) == 0);
    return t;
}
static Vec3 direction_at(float yaw, float pitch)
{
    float y = yaw*PM_DEG_TO_RAD, p = pitch*PM_DEG_TO_RAD;
    return pm_vec3(sinf(y)*cosf(p), sinf(p), cosf(y)*cosf(p));
}
static void test_geometry(void)
{
    ForearmMappingContext c;
    HumanForearmTarget t;
    const float cases[][5] = {
        {0,0,1,0,0}, {1,0,0,90,0}, {-1,0,0,-90,0}, {0,0,-1,-180,0},
        {0,1,1,0,45}, {0,-1,1,0,-45}, {0,1,0,0,90}, {0,-1,0,0,-90}
    };
    for (unsigned i=0; i<sizeof(cases)/sizeof(cases[0]); i++) {
        init(&c);
        t = direction(&c, pm_vec3(cases[i][0],cases[i][1],cases[i][2]));
        near(t.elbow_roll_deg,cases[i][3],0.001f);
        near(t.elbow_pitch_deg,cases[i][4],0.001f);
        assert(t.elbow_roll_observable == (i<6));
    }
    init(&c);
    near(pm_vdot(c.pose.body_x_axis,c.pose.body_y_axis),0,1e-6f);
    near(pm_vdot(pm_vcross(c.pose.body_x_axis,c.pose.body_y_axis),c.pose.body_z_axis),1,1e-6f);
    c.pose.body_frame_valid=0;
    assert(fm_calculate_angles(&c,0.05f,&t)==-1);
    c.pose.body_frame_valid=1;
    c.pose.wrist_3d=c.pose.elbow_3d;
    assert(fm_calculate_angles(&c,0.05f,&t)==-1);
    c.pose.wrist_3d.x=NAN;
    assert(fm_calculate_angles(&c,0.05f,&t)==-1);
}
static void test_temporal_geometry(void)
{
    ForearmMappingContext c;
    HumanForearmTarget t;
    init(&c);
    t=direction(&c,direction_at(179,0));
    float old=t.elbow_roll_deg;
    for (int i=0;i<40;i++) {
        t=direction(&c,direction_at(-179,0));
        assert(fabsf(fm_wrap180(t.elbow_roll_deg-old))<2.1f);
        old=t.elbow_roll_deg;
    }
    near(fm_wrap180(t.elbow_roll_deg+179),0,PM_JOINT_DEADBAND_DEG+0.01f);
    for (int sign=-1;sign<=1;sign+=2) {
        init(&c);
        t=direction(&c,direction_at(35,0));
        for (int i=0;i<60;i++) {
            t=direction(&c,pm_vec3((i%2 ? -1 : 1)*0.001f,(float)sign,0.001f));
            near(t.elbow_roll_deg,35,0.001f);
            assert(!t.elbow_roll_observable);
            near(pm_vlen(c.wrist_reference),1,1e-5f);
            if(i>0) assert(pm_vdot(c.wrist_reference, pm_vec3(-sign*sinf(35*PM_DEG_TO_RAD),
                                                          0,-sign*cosf(35*PM_DEG_TO_RAD)))>0.99f);
        }
        near(t.elbow_pitch_deg,90*sign,0.5f);
        t=direction(&c,pm_vec3(0.03f,1,0));
        assert(!t.elbow_roll_observable); /* hysteresis */
        t=direction(&c,pm_vec3(0.05f,1,0));
        assert(t.elbow_roll_observable);
    }
    init(&c);
    direction(&c,direction_at(0,0));
    t=direction(&c,direction_at(60,40));
    assert(t.elbow_roll_deg>0 && t.elbow_roll_deg<60);
    assert(t.elbow_pitch_deg>0 && t.elbow_pitch_deg<40);
    /* Same camera-space F changes its body-local azimuth when shoulders turn. */
    c.pose.shoulder_l_3d=pm_vec3(0,0,0.5f);
    c.pose.shoulder_r_3d=pm_vec3(0,0,-0.5f);
    c.pose.body_frame_valid=0; c.angle_valid=0; c.elbow_roll_initialized=0;
    assert(pm_update_stable_body_frame(&c.pose,0.05f)==0);
    t=direction(&c,pm_vec3(0,0,1));
    near(t.elbow_roll_deg,-90,0.001f);
}

static Vec3 rotate(Vec3 v, Vec3 axis, float degrees)
{
    float a=degrees*PM_DEG_TO_RAD;
    assert(pm_vnormalize(&axis)==0);
    return pm_vadd(pm_vadd(pm_vscale(v,cosf(a)),pm_vscale(pm_vcross(axis,v),sinf(a))),
                   pm_vscale(axis,pm_vdot(axis,v)*(1-cosf(a))));
}

static void test_body_rotation(void)
{
    ForearmMappingContext a,b;
    HumanForearmTarget ta,tb;
    const Vec3 axes[]={{1,0,0,1},{0,1,0,1},{0,0,1,1},{0.3f,0.7f,0.2f,1}};
    for(unsigned k=0;k<sizeof(axes)/sizeof(axes[0]);k++) {
        init(&a); init(&b);
        b.pose.body_x_axis=rotate(a.pose.body_x_axis,axes[k],63);
        b.pose.body_y_axis=rotate(a.pose.body_y_axis,axes[k],63);
        b.pose.body_z_axis=rotate(a.pose.body_z_axis,axes[k],63);
        for(int i=0;i<30;i++) {
            /* Avoid testing exactly on the existing 0.5-degree deadband:
             * rotation rounding can put equivalent floats on opposite sides. */
            Vec3 f=direction_at(165+i,20+i*0.7f);
            ta=direction(&a,f);
            b.pose.elbow_3d=rotate(a.pose.elbow_3d,axes[k],63);
            b.pose.wrist_3d=rotate(a.pose.wrist_3d,axes[k],63);
            assert(fm_calculate_angles(&b,0.05f,&tb)==0);
            near(fm_wrap180(tb.elbow_roll_deg-ta.elbow_roll_deg),0,0.003f);
            near(tb.elbow_pitch_deg,ta.elbow_pitch_deg,0.003f);
            near(pm_vdot(b.wrist_reference,rotate(a.wrist_reference,axes[k],63)),1,1e-5f);
        }
    }
    /* Builder is equivariant for rotations preserving camera up (+Y). */
    init(&a); ta=direction(&a,direction_at(30,20));
    for(int deg=-180;deg<=180;deg+=30) {
        init(&b);
        b.pose.shoulder_l_3d=rotate(a.pose.shoulder_l_3d,pm_vec3(0,1,0),(float)deg);
        b.pose.shoulder_r_3d=rotate(a.pose.shoulder_r_3d,pm_vec3(0,1,0),(float)deg);
        b.pose.body_frame_valid=0;
        assert(pm_update_stable_body_frame(&b.pose,0.05f)==0);
        tb=direction(&b,rotate(direction_at(30,20),pm_vec3(0,1,0),(float)deg));
        near(tb.elbow_roll_deg,ta.elbow_roll_deg,0.003f);
        near(tb.elbow_pitch_deg,ta.elbow_pitch_deg,0.003f);
    }
    /* Limitation: pitching the torso about its shoulder line is unobservable
     * from the two shoulders. Camera up remains fixed; no false invariance claim. */
    init(&a); init(&b);
    ta=direction(&a,pm_vec3(0,0,1));
    tb=direction(&b,rotate(pm_vec3(0,0,1),pm_vec3(1,0,0),30));
    near(ta.elbow_pitch_deg,0,0.001f);
    near(tb.elbow_pitch_deg,-30,0.001f);

    /* Original P3 Y-sign fixture; synthetic forearm parallel to the upper arm. */
    forearm_mapping_init(&a);
    a.pose.shoulder_l_3d=pm_vec3(0.298f,0.750f,7.253f);
    a.pose.shoulder_r_3d=pm_vec3(-0.585f,0.750f,6.783f);
    assert(pm_update_stable_body_frame(&a.pose,0.05f)==0);
    ta=direction(&a,pm_vec3(-0.169f,-0.735f,-0.017f));
    near(ta.elbow_pitch_deg,-76.9878f,0.02f);
    assert(a.pose.body_y_axis.y>0);
}
static HumanForearmTarget hand(ForearmMappingContext *c, float yaw, float elevation,
                               float flexion, float roll, float finger_pixels)
{
    Vec3 f=direction_at(yaw,elevation), n, s, h, center;
    HumanForearmTarget t=direction(c,f);
    float r=roll*PM_DEG_TO_RAD, p=flexion*PM_DEG_TO_RAD;
    n=pm_vadd(pm_vscale(c->wrist_reference,cosf(r)),
              pm_vscale(pm_vcross(f,c->wrist_reference),sinf(r)));
    s=pm_vcross(n,f);
    h=pm_vadd(pm_vscale(f,cosf(p)),pm_vscale(n,sinf(p)));
    center=pm_vadd(c->pose.wrist_3d,h);
    c->pose.finger1_3d=pm_vadd(center,pm_vscale(s,0.1f));
    c->pose.finger2_3d=pm_vsub(center,pm_vscale(s,0.1f));
    c->pose.finger1.value.x=finger_pixels; c->pose.finger1.value.y=0;
    c->pose.finger2.value.x=0; c->pose.finger2.value.y=0;
    c->pose.wrist.value.x=0; c->pose.wrist.value.y=12;
    c->pose.wrist.value.valid=1;
    c->pose.finger1.value.valid=1;
    c->pose.finger2.value.valid=1;
    assert(fm_calculate_hand(c,100,0.05f,0.05f,&t)==0);
    return t;
}
static void test_wrist(void)
{
    ForearmMappingContext c;
    HumanForearmTarget t;
    /* Pure parent translation must not introduce relative finger-depth lag. */
    init(&c);
    c.pose.wrist_3d=pm_vec3(0,0,5);
    c.pose.finger_parent_wrist=c.pose.wrist_3d;
    /* Offset direction (0.3,0.15,0.1) is arbitrary; magnitude must match
     * each finger's configured wrist-relative length so the ray-sphere
     * solve actually admits these points. */
    float dir_len = sqrtf(0.3f*0.3f + 0.15f*0.15f + 0.1f*0.1f);
    float finger1_len = PM_SHOULDER_WIDTH_UNIT * PM_WRIST_TO_FINGER1_RATIO;
    float finger2_len = PM_SHOULDER_WIDTH_UNIT * PM_WRIST_TO_FINGER2_RATIO;
    float s1 = finger1_len / dir_len, s2 = finger2_len / dir_len;
    c.pose.finger1_3d=pm_vec3(0.3f*s1,0.15f*s1,5.0f+0.1f*s1);
    c.pose.finger2_3d=pm_vec3(0.3f*s2,-0.15f*s2,5.0f+0.1f*s2);
    c.pose.finger_pose3d_valid=1;
    c.pose.wrist_3d.z=6;
    {
        float f1z=6.0f+0.1f*s1, f2z=6.0f+0.1f*s2;
        c.pose.finger1.value.x=PM_CAMERA_CX+PM_CAMERA_FX*0.3f*s1/f1z;
        c.pose.finger1.value.y=PM_CAMERA_CY-PM_CAMERA_FY*0.15f*s1/f1z;
        c.pose.finger2.value.x=PM_CAMERA_CX+PM_CAMERA_FX*0.3f*s2/f2z;
        c.pose.finger2.value.y=PM_CAMERA_CY+PM_CAMERA_FY*0.15f*s2/f2z;
    }
    assert(pm_reconstruct_finger_pose3d(&c.pose,0.05f)==0);
    near(c.pose.finger1_3d.z-c.pose.wrist_3d.z,0.1f*s1,0.001f);
    near(c.pose.finger2_3d.z-c.pose.wrist_3d.z,0.1f*s2,0.001f);
    /* Roll must remain defined through 90-degree flexion, and must not
     * acquire a 180-degree offset when the palm normal changes hemisphere. */
    const float cases[][2]={{0,0},{30,0},{-30,0},{0,45},{0,-45},{25,40},{0,179},{0,-179},
                            {89,40},{90,40},{91,40},{120,40},{-90,-45}};
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        init(&c); t=hand(&c,25,35,cases[i][0],cases[i][1],12);
        near(t.wrist_pitch_deg,cases[i][0],0.003f);
        near(fm_wrap180(t.wrist_roll_deg-cases[i][1]-180.0f),0,0.003f);
        assert(t.gripper_norm==1 && t.hand_fresh);
    }
    init(&c);
    t=hand(&c,25,35,0,0,12);
    {
        Vec3 f=direction_at(25,35);
        Vec3 center=pm_vadd(c.pose.wrist_3d,c.wrist_reference);
        c.pose.finger1_3d=pm_vsub(center,pm_vscale(f,0.1f));
        c.pose.finger2_3d=pm_vadd(center,pm_vscale(f,0.1f));
        /* Lateral axis parallel to forearm: HOLD rather than fabricated axis. */
        assert(fm_calculate_hand(&c,100,0.05f,0.05f,&t)==-1);
    }
    init(&c); t=hand(&c,0,0,0,179,12);
    for(int i=0;i<80;i++) {
        float old=t.wrist_roll_deg;
        t=hand(&c,0,0,0,-179,9);
        assert(fabsf(fm_wrap180(t.wrist_roll_deg-old))<3);
        assert(t.gripper_norm>=0.0f && t.gripper_norm<=1.0f);
    }
    near(fm_wrap180(t.wrist_roll_deg-1),0,PM_ROLL_DEADBAND_DEG+0.01f);
    /* First pole has no roll reference measurement; no hand target yet. */
    init(&c); t=direction(&c,pm_vec3(0,1,0));
    assert(fm_calculate_hand(&c,100,0.05f,0.05f,&t)==-1);
    /* A pole with heading history has a well-defined retained reference. */
    hand(&c,0,0,0,0,12); t=hand(&c,0,90,0,0,12);
    near(fm_wrap180(t.wrist_roll_deg+180),0,0.003f);
    assert(!t.elbow_roll_observable);
    /* Nearly vertical directions retain a continuous Body/forearm reference. */
    init(&c); hand(&c,0,85,0,0,12);
    Vec3 prev=c.wrist_reference;
    for(int i=0;i<30;i++) {
        t=hand(&c,0,89.9f,0,0,12);
        assert(pm_vdot(prev,c.wrist_reference)>0.99f);
        assert(fabsf(fm_wrap180(t.wrist_roll_deg+180))<1);
        prev=c.wrist_reference;
    }
    /* Degenerate plane must fail, leaving caller to hold hand fields. */
    c.pose.finger2_3d=c.pose.finger1_3d;
    assert(fm_calculate_hand(&c,100,0.05f,0.05f,&t)==-1);
    init(&c); hand(&c,0,0,0,25,12);
    assert(pose_mapping_start_roll_zero_calibration(&c.pose)==0);
    for(int i=0;i<80;i++) t=hand(&c,0,0,0,25,12);
    assert(c.pose.roll_zero_calibrated);
    near(t.wrist_roll_deg,0,0.6f);
}

static void test_wrist_body_reference(void)
{
    ForearmMappingContext a,b;
    HumanForearmTarget ta,tb;
    HumanJointTarget legacy;
    PoseMappingContext legacy_ctx;
    Vec3 axis=pm_vec3(0.2f,0.8f,0.4f);
    init(&a); init(&b);
    ta=hand(&a,25,35,20,-35,12);
    b.pose.body_x_axis=rotate(a.pose.body_x_axis,axis,71);
    b.pose.body_y_axis=rotate(a.pose.body_y_axis,axis,71);
    b.pose.body_z_axis=rotate(a.pose.body_z_axis,axis,71);
    b.pose.elbow_3d=rotate(a.pose.elbow_3d,axis,71);
    b.pose.wrist_3d=rotate(a.pose.wrist_3d,axis,71);
    b.pose.finger1_3d=rotate(a.pose.finger1_3d,axis,71);
    b.pose.finger2_3d=rotate(a.pose.finger2_3d,axis,71);
    b.pose.wrist.value=a.pose.wrist.value;
    b.pose.finger1.value=a.pose.finger1.value;
    b.pose.finger2.value=a.pose.finger2.value;
    assert(fm_calculate_angles(&b,0.05f,&tb)==0);
    assert(fm_calculate_hand(&b,100,0.05f,0.05f,&tb)==0);
    near(tb.wrist_pitch_deg,ta.wrist_pitch_deg,0.003f);
    near(tb.wrist_roll_deg,ta.wrist_roll_deg,0.003f);

    /* Away from the vertical reference singularity, legacy and forearm
     * calculations use the same Body-Y reference and thumb-to-index axis. */
    legacy_ctx=a.pose;
    legacy_ctx.hand_angle_valid=0;
    legacy_ctx.prev_hand_normal_valid=0;
    legacy_ctx.prev_roll_raw_valid=0;
    assert(pm_calculate_hand_angles_and_gripper(&legacy_ctx,100,0.05f,0.05f,&legacy)==0);
    near(ta.wrist_pitch_deg,legacy.wrist_pitch_deg,0.003f);
    near(ta.wrist_roll_deg,legacy.wrist_roll_deg,0.003f);
    /* A sudden plane-normal sign ambiguity is continuous for a flat hand. */
    init(&a); ta=hand(&a,0,0,0,30,12);
    Vec3 tmp=a.pose.finger1_3d;
    a.pose.finger1_3d=a.pose.finger2_3d;
    a.pose.finger2_3d=tmp;
    assert(fm_calculate_hand(&a,100,0.05f,0.05f,&tb)==0);
    near(tb.wrist_roll_deg,ta.wrist_roll_deg,0.003f);
}
static Point2D p2(float x,float y)
{
    Point2D p={PM_CAMERA_CX+2*(x-319.5f),PM_CAMERA_CY+2*(y-239.5f),1};
    return p;
}
static HumanPose2D sample(void)
{
    HumanPose2D p={0};
    p.shoulder_l=p2(250,190); p.shoulder_r=p2(390,190);
    p.elbow=p2(445,225); p.wrist=p2(500,255);
    p.finger1=p2(528,238); p.finger2=p2(535,278);
    p.valid=1; p.frame_id=123;
    return p;
}
static void test_gripper_without_wrist_geometry(void)
{
    ForearmMappingContext c;
    HumanForearmTarget t;
    HumanPose2D p = sample();

    /* Coincident thumb/index tips are a valid closed gripper observation,
     * while their 3D span cannot define a wrist orientation. */
    p.finger2 = p.finger1;
    assert(forearm_mapping_init(&c) == 0);
    assert(forearm_mapping_update(&c, &p, POSE_ARM_RIGHT, 0.05f, &t) == -1);
    assert(!t.valid && !t.hand_fresh);
    near(t.gripper_norm, 0.0f, 0.0f);
    assert(c.pose.gripper_initialized && c.pose.gripper_state == 0U);
}
static void set_gripper_ratio(PoseMappingContext *p, float ratio,
                              float hand_length, float origin_x)
{
    float spread = 4.0f * hand_length * ratio / (4.0f - ratio * ratio);
    p->wrist.value = (Point2D){origin_x, 200.0f, 1U};
    p->finger1.value = (Point2D){origin_x, 200.0f + hand_length, 1U};
    p->finger2.value = (Point2D){origin_x + spread,
                                 200.0f + hand_length, 1U};
}
static void test_gripper_normalized(void)
{
    PoseMappingContext p, scaled;
    float value, held;

    assert(pose_mapping_init(&p) == 0);
    set_gripper_ratio(&p, 0.0f, 40.0f, 300.0f);
    assert(pm_update_gripper_from_2d(&p, 100.0f, &value) == 0);
    near(value, 0.0f, 1e-5f);
    assert(p.gripper_state == 0U && !p.gripper_last_hold);

    set_gripper_ratio(&p, 0.30f, 40.0f, 300.0f);
    for (int i = 0; i < 15; ++i)
        assert(pm_update_gripper_from_2d(&p, 100.0f, &value) == 0);
    near(value, 0.4f, 0.002f);
    assert(p.gripper_state == 0U); /* mid-band retains CLOSE */
    near(p.gripper_open_ratio, 0.30f, 1e-4f);
    for (int i = 0; i < 8; ++i) {
        set_gripper_ratio(&p, i & 1 ? 0.29f : 0.31f, 40.0f, 300.0f);
        assert(pm_update_gripper_from_2d(&p, 100.0f, &value) == 0);
        assert(p.gripper_state == 0U);
        assert(value > 0.0f && value < 1.0f);
    }
    set_gripper_ratio(&p, 0.30f, 40.0f, 300.0f);
    for (int i = 0; i < 10; ++i)
        assert(pm_update_gripper_from_2d(&p, 100.0f, &value) == 0);
    near(value, 0.4f, 0.005f);

    assert(pose_mapping_init(&scaled) == 0);
    set_gripper_ratio(&scaled, 0.30f, 80.0f, 600.0f);
    assert(pm_update_gripper_from_2d(&scaled, 200.0f, &held) == 0);
    near(held, value, 0.005f); /* translating/scaling the hand changes no ratio */

    set_gripper_ratio(&p, 1.0f, 40.0f, 300.0f);
    assert(pm_update_gripper_from_2d(&p, 100.0f, &held) == 0);
    near(held, value, 0.005f); /* one-frame spike removed by median */
    set_gripper_ratio(&p, 0.30f, 40.0f, 300.0f);
    assert(pm_update_gripper_from_2d(&p, 100.0f, &held) == 0);
    near(held, value, 0.005f);
    assert(p.gripper_state == 0U);

    set_gripper_ratio(&p, 0.41f, 40.0f, 300.0f);
    for (int i = 0; i < 8; ++i)
        assert(pm_update_gripper_from_2d(&p, 100.0f, &value) == 0);
    assert(p.gripper_state == 1U);
    set_gripper_ratio(&p, 0.30f, 40.0f, 300.0f);
    for (int i = 0; i < 8; ++i)
        assert(pm_update_gripper_from_2d(&p, 100.0f, &value) == 0);
    assert(p.gripper_state == 1U); /* mid-band retains OPEN */
    set_gripper_ratio(&p, 0.19f, 40.0f, 300.0f);
    for (int i = 0; i < 8; ++i)
        assert(pm_update_gripper_from_2d(&p, 100.0f, &value) == 0);
    assert(p.gripper_state == 0U);

    assert(pose_mapping_init(&scaled) == 0);
    set_gripper_ratio(&scaled, 0.70f, 40.0f, 300.0f);
    assert(pm_update_gripper_from_2d(&scaled, 100.0f, &held) == 0);
    near(held, 1.0f, 1e-5f);
    assert(scaled.gripper_state == 1U);

    held = value;
    p.finger1.value = p.wrist.value;
    p.finger2.value = p.wrist.value;
    assert(pm_update_gripper_from_2d(&p, 100.0f, &value) == -1);
    near(value, held, 0.0f);
    assert(p.gripper_last_hold &&
           p.gripper_hold_reason == PM_GRIPPER_HOLD_HAND_SCALE);
    p.finger1.value.valid = 0U;
    assert(pm_update_gripper_from_2d(&p, 100.0f, &value) == -1);
    near(value, held, 0.0f);
    assert(p.gripper_hold_reason == PM_GRIPPER_HOLD_MISSING);
    p.finger1.value.valid = 1U;
    p.finger2.value.valid = 0U;
    assert(pm_update_gripper_from_2d(&p, 100.0f, &value) == -1);
    near(value, held, 0.0f);
    p.finger2.value.valid = 1U;
    p.wrist.value.valid = 0U;
    assert(pm_update_gripper_from_2d(&p, 100.0f, &value) == -1);
    near(value, held, 0.0f);
    p.wrist.value.valid = 1U;
    p.finger1.value.x = NAN;
    assert(pm_update_gripper_from_2d(&p, 100.0f, &value) == -1);
    near(value, held, 0.0f);
    assert(p.gripper_hold_reason == PM_GRIPPER_HOLD_NONFINITE);
    p.finger1.value.x = INFINITY;
    assert(pm_update_gripper_from_2d(&p, 100.0f, &value) == -1);
    near(value, held, 0.0f);

    /* Sustained real motion reaches the opposite endpoint in three fresh
     * observations; the first one alone cannot trigger a full command. */
    assert(pose_mapping_init(&scaled) == 0);
    set_gripper_ratio(&scaled, 0.0f, 40.0f, 300.0f);
    for (int i = 0; i < 2; ++i)
        assert(pm_update_gripper_from_2d(&scaled, 100.0f, &value) == 0);
    set_gripper_ratio(&scaled, 0.70f, 40.0f, 300.0f);
    assert(pm_update_gripper_from_2d(&scaled, 100.0f, &value) == 0);
    near(value, 0.0f, 1e-5f);
    assert(pm_update_gripper_from_2d(&scaled, 100.0f, &value) == 0);
    assert(value > 0.0f && value < 1.0f);
    assert(pm_update_gripper_from_2d(&scaled, 100.0f, &value) == 0);
    near(value, 1.0f, 1e-5f);
    assert(scaled.gripper_state == 1U);

    /* Forearm's early 2D update and later hand calculation may request the
     * same frame; the ratio history must advance only once. */
    scaled.last_frame_id_valid = 1U;
    scaled.last_frame_id = 100U;
    set_gripper_ratio(&scaled, 0.30f, 40.0f, 300.0f);
    assert(pm_update_gripper_from_2d(&scaled, 100.0f, &value) == 0);
    held = value;
    set_gripper_ratio(&scaled, 0.70f, 40.0f, 300.0f);
    assert(pm_update_gripper_from_2d(&scaled, 100.0f, &value) == 0);
    near(value, held, 0.0f);
}
static void test_finger_branch_window(void)
{
    PoseMappingContext p;
    const float depth = 4.65f;

    assert(pose_mapping_init(&p) == 0);
    p.wrist_3d = pm_vec3(0.0f, 0.0f, 5.0f);
    p.elbow_3d = pm_vec3(0.0f, 0.0f, 5.65f);
    p.finger1.value.x = PM_CAMERA_CX - PM_CAMERA_FX * 0.07f / depth;
    p.finger1.value.y = PM_CAMERA_CY;
    p.finger2.value.x = PM_CAMERA_CX + PM_CAMERA_FX * 0.07f / depth;
    p.finger2.value.y = PM_CAMERA_CY;

    for (unsigned i = 1U; i < POSE_FINGER_BRANCH_WINDOW; ++i) {
        p.finger_branch_elapsed_frames = (uint8_t)i;
        assert(pm_reconstruct_finger_pose3d_tracked(&p, 0.05f) == -1);
        assert(!p.finger_pose3d_valid && !p.finger_branch_selected_valid);
    }
    p.finger_branch_elapsed_frames = POSE_FINGER_BRANCH_WINDOW;
    assert(pm_reconstruct_finger_pose3d_tracked(&p, 0.05f) == 0);
    assert(p.finger_branch_selected == 0U); /* nearer, forearm-aligned pair */
    assert(p.finger1_3d.z < p.wrist_3d.z);
    assert(p.finger2_3d.z < p.wrist_3d.z);

    pm_reset_finger_branch_tracker(&p);
    assert(!p.finger_pose3d_valid && !p.finger_branch_selected_valid);
    /* ever_selected survives the reset: this is not a first-ever pick
     * anymore, so re-acquisition only needs the short tracking window
     * (6 frames), not the full 60-frame window again. Requiring 60 fresh
     * frames after every reset made even a brief dropout or a single missed
     * detection freeze the reported position for seconds. */
    assert(p.finger_branch_ever_selected);
    p.elbow_3d = pm_vec3(-0.65f, 0.0f, 5.0f);
    p.finger1.value.x = PM_CAMERA_CX;
    p.finger1.value.y = PM_CAMERA_CY - PM_CAMERA_FY * 0.07f / depth;
    p.finger2.value.x = PM_CAMERA_CX;
    p.finger2.value.y = PM_CAMERA_CY + PM_CAMERA_FY * 0.07f / depth;
    for (unsigned i = 1U; i < 6U; ++i)
        assert(pm_reconstruct_finger_pose3d_tracked(&p, 0.05f) == -1);
    assert(pm_reconstruct_finger_pose3d_tracked(&p, 0.05f) == 0);
    assert(p.finger_pose3d_valid);
    /* The tie-break is stable: re-running does not flip the pick. */
    assert(pm_reconstruct_finger_pose3d_tracked(&p, 0.05f) == 0);
    assert(p.finger_branch_selected == 0U);

    /* The 60-frame clock survives missing fingers and a reset. After it
     * expires, the first usable observation can bootstrap the branch. */
    assert(pose_mapping_init(&p) == 0);
    p.wrist_3d = pm_vec3(0.0f, 0.0f, 5.0f);
    p.elbow_3d = pm_vec3(0.0f, 0.0f, 5.65f);
    p.finger1.value.x = PM_CAMERA_CX - PM_CAMERA_FX * 0.07f / depth;
    p.finger1.value.y = PM_CAMERA_CY;
    p.finger2.value.x = PM_CAMERA_CX + PM_CAMERA_FX * 0.07f / depth;
    p.finger2.value.y = PM_CAMERA_CY;
    p.finger_branch_elapsed_frames = POSE_FINGER_BRANCH_WINDOW;
    pm_reset_finger_branch_tracker(&p);
    assert(p.finger_branch_elapsed_frames == POSE_FINGER_BRANCH_WINDOW);
    assert(!p.finger_branch_ever_selected);
    assert(pm_reconstruct_finger_pose3d_tracked(&p, 0.05f) == 0);
    assert(p.finger_branch_selected_valid && p.finger_branch_ever_selected);
}
static void test_pipeline(void)
{
    ForearmMappingContext c, old;
    HumanPose2D p=sample();
    HumanForearmTarget t, prev;
    forearm_mapping_init(&c);
    assert(forearm_mapping_update(&c,&p,POSE_ARM_RIGHT,0.05f,&t)==-1);
    assert(c.pose.body_frame_valid);
    assert(!t.valid && !t.hand_fresh);
    /* Seed an already-approved wrist target to exercise the downstream HOLD
     * path independently of the branch decision tested above. */
    memset(&c.last_target,0,sizeof(c.last_target));
    c.last_target.valid=1;
    c.last_target.frame_id=p.frame_id;
    c.last_target.wrist_pitch_deg=15.0f;
    c.last_target.wrist_roll_deg=-20.0f;
    c.last_target_valid=1;
    old=c; prev=c.last_target;
    assert(forearm_mapping_update(&c,&p,POSE_ARM_RIGHT,100,&t)==0);
    assert(memcmp(&c,&old,sizeof(c))==0);
    near(t.elbow_roll_deg,prev.elbow_roll_deg,0);
    p.frame_id++; p.finger1.valid=0;
    assert(forearm_mapping_update(&c,&p,POSE_ARM_RIGHT,0.05f,&t)==1);
    assert(!t.hand_fresh && t.valid && t.frame_id==124);
    near(t.wrist_roll_deg,prev.wrist_roll_deg,0);
    near(t.gripper_norm,prev.gripper_norm,0);
    assert(c.pose.gripper_last_hold &&
           c.pose.gripper_hold_reason == PM_GRIPPER_HOLD_MISSING);
    for(int i=0;i<12;i++) {
        p.frame_id++;
        assert(forearm_mapping_update(&c,&p,POSE_ARM_RIGHT,0.05f,&t)==1);
        assert(t.valid && !t.hand_fresh);
        near(t.wrist_roll_deg,prev.wrist_roll_deg,0);
    }
    uint32_t last_fresh=p.frame_id;
    p.frame_id++; p.elbow.valid=0;
    assert(forearm_mapping_update(&c,&p,POSE_ARM_RIGHT,0.05f,&t)==0);
    assert(t.frame_id==last_fresh && t.valid && !t.elbow_roll_observable);
    for(int i=0;i<10;i++) {p.frame_id++; forearm_mapping_update(&c,&p,POSE_ARM_RIGHT,0.05f,&t);}
    assert(!t.valid);
    p=sample(); p.frame_id=200;
    assert(forearm_mapping_update(&c,&p,POSE_ARM_RIGHT,NAN,&t)==-1);
    p.frame_id++; p.valid=0;
    assert(forearm_mapping_update(&c,&p,POSE_ARM_RIGHT,1,&t)==-1);
    p=sample(); p.frame_id=300;
    assert(forearm_mapping_update(&c,&p,POSE_ARM_RIGHT,0.05f,&t)==-1);
    /* Same-id side change must reset history, never reuse the other arm. */
    forearm_mapping_update(&c,&p,POSE_ARM_LEFT,0.05f,&t);
    assert(c.pose.last_arm_side==POSE_ARM_LEFT);
    assert(agent1_forearm_stage_init()==0);
    assert(agent1_forearm_stage_run(&p,POSE_ARM_RIGHT,0.05f)==-1);
    assert(!agent1_forearm_stage_output()->valid);
    p.frame_id++; p.wrist.x=NAN;
    assert(forearm_mapping_update(&c,&p,POSE_ARM_LEFT,1,&t)==-1);
    p=sample(); p.frame_id=400;
    assert(forearm_mapping_update(&c,&p,POSE_ARM_RIGHT,0.05f,&t)==-1);
}

static void test_initial_branch_scores(void)
{
    PoseMappingContext p;
    assert(pose_mapping_init(&p) == 0);
    p.wrist_3d = pm_vec3(0.0f, 0.0f, 5.0f);
    p.finger_branch_elapsed_frames = POSE_FINGER_BRANCH_WINDOW;
    assert(pm_select_initial_finger_branch(&p) == -1);
    p.finger_branch_initial_count[0] = 2U;
    p.finger_branch_initial_score_sum[0] = 0.4f;
    p.finger_branch_initial_count[1] = 1U;
    p.finger_branch_initial_score_sum[1] = 0.1f;
    p.finger_branch_initial_finger1_relative[1] = pm_vec3(0.1f, 0.0f, 0.3f);
    p.finger_branch_initial_finger2_relative[1] = pm_vec3(-0.1f, 0.0f, 0.3f);
    pm_reset_finger_branch_tracker(&p);
    assert(pm_select_initial_finger_branch(&p) == 0);
    assert(p.finger_branch_selected == 1U && p.finger_branch_ever_selected);
    near(p.finger1_3d.z, 5.3f, 1e-6f);
}

static void test_major_only_after_initial_window(void)
{
    ForearmMappingContext c;
    HumanPose2D p = sample();
    HumanForearmTarget t;
    assert(forearm_mapping_init(&c) == 0);
    p.finger1.valid = 0U;
    p.finger2.valid = 0U;
    for (unsigned i = 1U; i < POSE_FINGER_BRANCH_WINDOW; ++i) {
        p.frame_id = i;
        assert(forearm_mapping_update(&c, &p, POSE_ARM_RIGHT, 0.05f, &t) == -1);
        assert(!t.valid);
    }
    p.frame_id = POSE_FINGER_BRANCH_WINDOW;
    assert(forearm_mapping_update(&c, &p, POSE_ARM_RIGHT, 0.05f, &t) == 1);
    assert(t.valid && !t.wrist_valid && !t.hand_fresh);
    assert(isfinite(t.elbow_roll_deg) && isfinite(t.elbow_pitch_deg));
    p.frame_id++;
    p.wrist.x += 10.0f;
    assert(forearm_mapping_update(&c, &p, POSE_ARM_RIGHT, 0.05f, &t) == 1);
    assert(t.valid && !t.wrist_valid && !t.hand_fresh);
}
static void test_uart(const char *path)
{
    FILE *f=fopen(path,"rb");
    PoseUartParser parser;
    HumanPose2D p;
    ForearmMappingContext c;
    HumanForearmTarget t;
    int byte, count=0, accepted=0;
    assert(f);
    pose_uart_parser_init(&parser); forearm_mapping_init(&c);
    while((byte=fgetc(f))!=EOF) {
        int r=pose_uart_parser_push(&parser,(uint8_t)byte,&p);
        assert(r>=0);
        if(r==1) {
            int rc=forearm_mapping_update(&c,&p,POSE_ARM_RIGHT,0.05f,&t);
            assert(rc==1 || rc==-1);
            assert(t.frame_id==p.frame_id);
            if (rc==1) {
                assert(t.valid && isfinite(t.elbow_pitch_deg));
                accepted++;
            } else assert(!t.valid);
            count++;
        }
    }
    fclose(f); assert(count==522 && accepted>0);
    printf("UART forearm replay: %d frames, %d valid PASS\n",count,accepted);
}
static void test_finger_relative_2d_filter(void)
{
    ForearmMappingContext c;
    HumanPose2D p = sample();
    float initial_offset, initial_ray, previous_ray, previous_landmark;

    init(&c);
    pm_update_all_landmarks(&c.pose, &p, 0.05f);
    initial_offset = c.pose.finger1_relative_2d.ray_input.x -
                     c.pose.wrist.value.x;
    initial_ray = c.pose.finger1_relative_2d.ray_input.x;

    /* A common hand translation must preserve the finger's wrist-relative
     * position, while a finger-only jump should be more damped than the
     * ordinary landmark used by the gripper. */
    p.wrist.x += 20.0f;
    p.finger1.x += 20.0f;
    p.finger2.x += 20.0f;
    pm_update_all_landmarks(&c.pose, &p, 0.05f);
    near(c.pose.finger1_relative_2d.ray_input.x - c.pose.wrist.value.x,
         initial_offset, 0.001f);
    assert(c.pose.finger1_relative_2d.ray_input.x > initial_ray);

    previous_ray = c.pose.finger1_relative_2d.ray_input.x;
    previous_landmark = c.pose.finger1.value.x;
    p.finger1.x += 12.0f;
    pm_update_all_landmarks(&c.pose, &p, 0.05f);
    assert(c.pose.finger1_relative_2d.ray_input.x > previous_ray);
    assert(c.pose.finger1_relative_2d.ray_input.x - previous_ray <
           c.pose.finger1.value.x - previous_landmark);
}

int main(int argc,char **argv)
{
    test_geometry(); test_temporal_geometry(); test_body_rotation(); test_wrist();
    test_wrist_body_reference(); test_gripper_without_wrist_geometry();
    test_gripper_normalized();
    test_finger_branch_window(); test_finger_relative_2d_filter();
    test_pipeline(); test_initial_branch_scores();
    test_major_only_after_initial_window();
    if(argc>1) test_uart(argv[1]);
    puts("Forearm geometry / wrist / temporal / pipeline: PASS");
    return 0;
}
