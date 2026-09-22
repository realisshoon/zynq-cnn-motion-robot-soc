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
    assert(forearm_mapping_set_table(c, pm_vec3(0,0,1), pm_vec3(1,0,0), 1) == 0);
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
    return pm_vec3(cosf(y)*cosf(p), sinf(y)*cosf(p), sinf(p));
}
static void test_geometry(void)
{
    ForearmMappingContext c, before;
    HumanForearmTarget t;
    const float cases[][5] = {
        {1,0,0,0,0}, {0,1,0,90,0}, {0,-1,0,-90,0}, {-1,0,0,-180,0},
        {1,0,1,0,45}, {1,0,-1,0,-45}, {0,0,1,0,90}, {0,0,-1,0,-90}
    };
    for (unsigned i=0; i<sizeof(cases)/sizeof(cases[0]); i++) {
        init(&c);
        t = direction(&c, pm_vec3(cases[i][0],cases[i][1],cases[i][2]));
        near(t.forearm_yaw_deg,cases[i][3],0.001f);
        near(t.forearm_pitch_deg,cases[i][4],0.001f);
        assert(t.yaw_observable == (i<6));
    }
    init(&c);
    before = c;
    assert(forearm_mapping_set_table(&c, pm_vec3(0,0,1), pm_vec3(0,0,1), 1) == -1);
    assert(memcmp(&before,&c,sizeof(c)) == 0);
    assert(forearm_mapping_set_table(&c, pm_vec3(NAN,0,1), pm_vec3(1,0,0),1) == -1);
    assert(forearm_mapping_set_table(&c, pm_vec3(0,0,0), pm_vec3(1,0,0),1) == -1);
    /* Rotated, scaled, non-orthogonal supplied directions are orthonormalized. */
    assert(forearm_mapping_set_table(&c, pm_vec3(0,2,0), pm_vec3(0,1,3),0) == 0);
    near(pm_vdot(c.table.x,c.table.z),0,1e-6f);
    near(pm_vdot(pm_vcross(c.table.x,c.table.y),c.table.z),1,1e-6f);
    t=direction(&c,c.table.y);
    near(t.forearm_yaw_deg,90,0.001f);
    assert(!t.calibrated);
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
    float old=t.forearm_yaw_deg;
    for (int i=0;i<40;i++) {
        t=direction(&c,direction_at(-179,0));
        assert(fabsf(fm_wrap180(t.forearm_yaw_deg-old))<2.1f);
        old=t.forearm_yaw_deg;
    }
    near(fm_wrap180(t.forearm_yaw_deg+179),0,PM_JOINT_DEADBAND_DEG+0.01f);
    for (int sign=-1;sign<=1;sign+=2) {
        init(&c);
        t=direction(&c,direction_at(35,0));
        for (int i=0;i<60;i++) {
            t=direction(&c,pm_vec3((i%2 ? -1 : 1)*0.001f,0.001f,(float)sign));
            near(t.forearm_yaw_deg,35,0.001f);
            assert(!t.yaw_observable);
            near(pm_vlen(c.wrist_reference),1,1e-5f);
            if(i>0) assert(pm_vdot(c.wrist_reference, pm_vec3(-sign*cosf(35*PM_DEG_TO_RAD),
                                                         -sign*sinf(35*PM_DEG_TO_RAD),0))>0.99f);
        }
        near(t.forearm_pitch_deg,90*sign,0.5f);
        t=direction(&c,pm_vec3(0.03f,0,1));
        assert(!t.yaw_observable); /* hysteresis */
        t=direction(&c,pm_vec3(0.05f,0,1));
        assert(t.yaw_observable);
    }
    init(&c);
    direction(&c,direction_at(0,0));
    t=direction(&c,direction_at(60,40));
    assert(t.forearm_yaw_deg>0 && t.forearm_yaw_deg<60);
    assert(t.forearm_pitch_deg>0 && t.forearm_pitch_deg<40);
    /* Shoulders/body axis are not the direct angle reference. */
    c.pose.shoulder_l_3d=pm_vec3(100,-100,50);
    c.pose.shoulder_r_3d=pm_vec3(-100,100,-50);
    c.angle_valid=0; c.yaw_initialized=0;
    t=direction(&c,direction_at(60,40));
    near(t.forearm_yaw_deg,60,0.001f);
    near(t.forearm_pitch_deg,40,0.001f);
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
    c->pose.finger1_3d=pm_vsub(center,pm_vscale(s,0.1f));
    c->pose.finger2_3d=pm_vadd(center,pm_vscale(s,0.1f));
    c->pose.finger1.value.x=0; c->pose.finger1.value.y=0;
    c->pose.finger2.value.x=finger_pixels; c->pose.finger2.value.y=0;
    assert(fm_calculate_hand(c,100,0.05f,0.05f,&t)==0);
    return t;
}
static void test_wrist(void)
{
    ForearmMappingContext c;
    HumanForearmTarget t;
    const float cases[][2]={{0,0},{30,0},{-30,0},{0,45},{0,-45},{25,40},{0,179},{0,-179}};
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        init(&c); t=hand(&c,25,35,cases[i][0],cases[i][1],12);
        near(t.wrist_pitch_deg,cases[i][0],0.003f);
        near(t.wrist_roll_deg,cases[i][1],0.003f);
        assert(t.gripper_norm==1 && t.hand_fresh);
    }
    init(&c); t=hand(&c,0,0,0,179,12);
    for(int i=0;i<80;i++) {
        float old=t.wrist_roll_deg;
        t=hand(&c,0,0,0,-179,9);
        assert(fabsf(fm_wrap180(t.wrist_roll_deg-old))<3);
        assert(t.gripper_norm==1); /* hysteresis gap */
    }
    near(fm_wrap180(t.wrist_roll_deg+179),0,PM_ROLL_DEADBAND_DEG+0.01f);
    t=hand(&c,0,0,0,-179,7); assert(t.gripper_norm==0);
    t=hand(&c,0,0,0,-179,9); assert(t.gripper_norm==0);
    t=hand(&c,0,0,0,-179,12); assert(t.gripper_norm==1);
    /* First pole has no roll reference measurement; no hand target yet. */
    init(&c); t=direction(&c,pm_vec3(0,0,1));
    assert(fm_calculate_hand(&c,100,0.05f,0.05f,&t)==-1);
    /* A pole with heading history has a well-defined retained reference. */
    hand(&c,0,0,0,0,12); t=hand(&c,0,90,0,0,12);
    near(t.wrist_roll_deg,0,0.003f); assert(!t.yaw_observable);
    /* Nearly vertical noisy directions do not switch table fallback axes. */
    init(&c); hand(&c,0,85,0,0,12);
    Vec3 prev=c.wrist_reference;
    for(int i=0;i<30;i++) {
        t=hand(&c,0,89.9f,0,0,12);
        assert(pm_vdot(prev,c.wrist_reference)>0.99f);
        assert(fabsf(t.wrist_roll_deg)<1);
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
static void test_pipeline(void)
{
    ForearmMappingContext c, old;
    HumanPose2D p=sample();
    HumanForearmTarget t, prev;
    forearm_mapping_init(&c);
    assert(forearm_mapping_update(&c,&p,POSE_ARM_RIGHT,0.05f,&t)==-1 && !t.valid);
    init(&c);
    assert(forearm_mapping_update(&c,&p,POSE_ARM_RIGHT,0.05f,&t)==1);
    assert(t.valid && t.frame_id==123 && t.hand_fresh);
    old=c; prev=t;
    assert(forearm_mapping_update(&c,&p,POSE_ARM_RIGHT,100,&t)==0);
    assert(memcmp(&c,&old,sizeof(c))==0);
    near(t.forearm_yaw_deg,prev.forearm_yaw_deg,0);
    p.frame_id++; p.finger1.valid=0;
    assert(forearm_mapping_update(&c,&p,POSE_ARM_RIGHT,0.05f,&t)==1);
    assert(!t.hand_fresh && t.valid && t.frame_id==124);
    near(t.wrist_roll_deg,prev.wrist_roll_deg,0);
    for(int i=0;i<12;i++) {
        p.frame_id++;
        assert(forearm_mapping_update(&c,&p,POSE_ARM_RIGHT,0.05f,&t)==1);
        assert(t.valid && !t.hand_fresh);
        near(t.wrist_roll_deg,prev.wrist_roll_deg,0);
    }
    uint32_t last_fresh=p.frame_id;
    p.frame_id++; p.elbow.valid=0;
    assert(forearm_mapping_update(&c,&p,POSE_ARM_RIGHT,0.05f,&t)==0);
    assert(t.frame_id==last_fresh && t.valid && !t.yaw_observable);
    for(int i=0;i<10;i++) {p.frame_id++; forearm_mapping_update(&c,&p,POSE_ARM_RIGHT,0.05f,&t);}
    assert(!t.valid);
    p=sample(); p.frame_id=200;
    assert(forearm_mapping_update(&c,&p,POSE_ARM_RIGHT,NAN,&t)==1);
    p.frame_id++; p.valid=0;
    assert(forearm_mapping_update(&c,&p,POSE_ARM_RIGHT,1,&t)==-1);
    p=sample(); p.frame_id=300;
    assert(forearm_mapping_update(&c,&p,POSE_ARM_RIGHT,0.05f,&t)==1);
    /* Same-id side change must reset history, never reuse the other arm. */
    forearm_mapping_update(&c,&p,POSE_ARM_LEFT,0.05f,&t);
    assert(c.pose.last_arm_side==POSE_ARM_LEFT && c.table.valid);
    assert(agent1_forearm_stage_init(pm_vec3(0,0,1),pm_vec3(1,0,0),1)==0);
    assert(agent1_forearm_stage_run(&p,POSE_ARM_RIGHT,0.05f)==1);
    assert(agent1_forearm_stage_output()->valid);
    p.frame_id++; p.wrist.x=NAN;
    assert(forearm_mapping_update(&c,&p,POSE_ARM_LEFT,1,&t)==-1);
    p=sample(); p.frame_id=400;
    assert(forearm_mapping_update(&c,&p,POSE_ARM_RIGHT,0.05f,&t)==1);
}
static void test_uart(const char *path)
{
    FILE *f=fopen(path,"rb");
    PoseUartParser parser;
    HumanPose2D p;
    ForearmMappingContext c;
    HumanForearmTarget t;
    int byte, count=0;
    assert(f);
    pose_uart_parser_init(&parser); init(&c);
    while((byte=fgetc(f))!=EOF) {
        int r=pose_uart_parser_push(&parser,(uint8_t)byte,&p);
        assert(r>=0);
        if(r==1) {
            assert(forearm_mapping_update(&c,&p,POSE_ARM_RIGHT,0.05f,&t)==1);
            assert(t.valid && t.frame_id==p.frame_id && isfinite(t.forearm_pitch_deg));
            count++;
        }
    }
    fclose(f); assert(count==522); printf("UART forearm replay: %d PASS\n",count);
}
int main(int argc,char **argv)
{
    test_geometry(); test_temporal_geometry(); test_wrist(); test_pipeline();
    if(argc>1) test_uart(argv[1]);
    puts("Forearm geometry / wrist / temporal / pipeline: PASS");
    return 0;
}
