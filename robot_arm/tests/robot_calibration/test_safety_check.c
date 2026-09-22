#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include "robot_calibration/safety_check.h"

static void near(float a,float b) { assert(fabsf(a-b)<0.002f); }
static JointCommand command(float b,float s,float e,float w)
{
    JointCommand c={b,s,e,w,90,0.5f,1};
    return c;
}
static float length(RobotPoint3D a,RobotPoint3D b)
{
    return sqrtf((a.x_cm-b.x_cm)*(a.x_cm-b.x_cm)+
                 (a.y_cm-b.y_cm)*(a.y_cm-b.y_cm)+
                 (a.z_cm-b.z_cm)*(a.z_cm-b.z_cm));
}
static void test_known_poses(void)
{
    JointCommand c=command(90,90,90,90);
    RobotJointPositions3D p;
    RobotJointPositions2D projection;
    assert(robot_forward_kinematics_3d(&c,&p));
    near(p.tip.x_cm,0);near(p.tip.y_cm,0);near(p.tip.z_cm,-32);
    near(p.elbow.z_cm,-11);near(p.wrist_pitch.z_cm,-24);
    c=command(120,90,90,90);
    assert(robot_forward_kinematics_3d(&c,&p));
    near(p.tip.x_cm,0);near(p.tip.y_cm,16);near(p.tip.z_cm,-27.712813f);
    c=command(90,120,90,90);
    assert(robot_forward_kinematics_3d(&c,&p));
    near(p.tip.x_cm,16);near(p.tip.y_cm,0);near(p.tip.z_cm,-27.712813f);
    c=command(90,90,120,90);
    assert(robot_forward_kinematics_3d(&c,&p));
    near(p.elbow.y_cm,0);near(p.elbow.z_cm,-11);
    near(p.tip.y_cm,10.5f);near(p.tip.z_cm,-29.186533f);
    c=command(120,130,120,90);
    assert(robot_forward_kinematics_3d(&c,&p));
    near(p.elbow.x_cm,7.070664f);
    near(p.elbow.y_cm,4.213244f);
    near(p.elbow.z_cm,-7.297553f);
    assert(robot_forward_kinematics_2d(&c,&projection));
    near(projection.elbow.x_cm,p.elbow.y_cm);
    near(projection.elbow.z_cm,p.elbow.z_cm);
}
static void test_link_lengths_and_rotations(void)
{
    RobotJointPositions3D p,ref;
    SafetyCheckFlags flags,expected;
    JointCommand c=command(90,90,160,160);
    int accepted=safety_check_apply(&c,NULL,&expected);
    assert(robot_forward_kinematics_3d(&c,&ref));
    for(int b=20;b<=160;b+=10) for(int s=20;s<=160;s+=10) {
        c.base_deg=(float)b;c.shoulder_deg=(float)s;
        assert(robot_forward_kinematics_3d(&c,&p));
        near(length(p.shoulder,p.elbow),11);
        near(length(p.elbow,p.wrist_pitch),13);
        near(length(p.wrist_pitch,p.tip),8);
        near(length(p.shoulder,p.tip),length(ref.shoulder,ref.tip));
        assert(safety_check_apply(&c,NULL,&flags)==accepted);
        assert(flags==expected); /* Rigid rotation does not change self-clearance. */
    }
}
static void test_straight_and_collisions(void)
{
    RobotJointPositions3D p;
    SafetyCheckFlags flags=UINT32_MAX;
    JointCommand c=command(90,90,90,90);
    assert(safety_check_apply(&c,NULL,&flags)); assert(flags==0);
    /* Pure lateral straight arm has a collapsed side projection, but is safe.
     * Direct FK/safety exercise beyond configured servo limits is intentional. */
    c.shoulder_deg=180;
    assert(safety_check_apply(&c,NULL,&flags)); assert(flags==0);
    assert(robot_forward_kinematics_3d(&c,&p));
    near(p.tip.x_cm,32);near(p.tip.z_cm,0);
    c=command(120,130,246,90);
    assert(!safety_check_apply(&c,NULL,&flags));
    assert(flags==SAFETY_CHECK_SELF_COLLISION);
    /* Interior angles still exceed 25deg here; terminal link returns across
     * the upper link. This exercises actual 3D segment collision, not angle gate. */
    c=command(125,135,230,230);
    assert(!safety_check_apply(&c,NULL,&flags));
    assert(flags==SAFETY_CHECK_SELF_COLLISION);
    c=command(90,90,90,90); c.valid=0;
    assert(!safety_check_apply(&c,NULL,&flags));assert(flags==SAFETY_CHECK_INVALID_COMMAND);
    c.valid=1;c.base_deg=NAN;
    assert(!safety_check_apply(&c,NULL,&flags));assert(flags==SAFETY_CHECK_INVALID_COMMAND);
    assert(!robot_forward_kinematics_3d(&c,&p));
    assert(!robot_forward_kinematics_3d(NULL,&p));
    assert(!robot_forward_kinematics_3d(&c,NULL));
    assert(!robot_forward_kinematics_2d(&c,NULL));
    assert(!safety_check_apply(NULL,NULL,&flags));assert(flags==SAFETY_CHECK_INVALID_COMMAND);
}
int main(void)
{
    test_known_poses();
    test_link_lengths_and_rotations();
    test_straight_and_collisions();
    puts("test_safety_check: PASS (3D geometry, straight arms, self-collision)");
    return 0;
}
