#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include "robot_calibration/motion_control.h"
#include "robot_calibration/robot_calibration.h"
#include "robot_calibration/safety_check.h"

static void near(float actual, float expected)
{
    assert(fabsf(actual - expected) < 0.002f);
}

static HumanJointTarget human(float azimuth, float elevation, float interior)
{
    HumanJointTarget h = {azimuth, elevation, interior, 0, 0, 0.5f, 1};
    return h;
}

static JointCommand apply(HumanJointTarget h)
{
    JointCommand c;
    assert(robot_calibration_apply(&h, &c));
    assert(c.valid);
    return c;
}

static void test_rest_and_single_axes(void)
{
    JointCommand c;
    /* Human downward direction has no meaningful azimuth. */
    for (int az = -180; az <= 180; az += 30) {
        c = apply(human((float)az, -90, 180));
        near(c.base_deg,90); near(c.shoulder_deg,90); near(c.elbow_deg,90);
        near(c.wrist_pitch_deg,90); near(c.wrist_roll_deg,90);
    }
    c = apply(human(0,-60,180)); /* Forward 30 degrees. */
    near(c.base_deg,120); near(c.shoulder_deg,90);
    c = apply(human(180,-60,180)); /* Backward 30 degrees. */
    near(c.base_deg,60); near(c.shoulder_deg,90);
    c = apply(human(90,-60,180)); /* Outward 30 degrees. */
    near(c.base_deg,90); near(c.shoulder_deg,120);
    c = apply(human(-90,-60,180)); /* Inward 30 degrees. */
    near(c.base_deg,90); near(c.shoulder_deg,60);
    c = apply(human(0,-90,150)); near(c.elbow_deg,120);
    c = apply(human(0,-90,110)); near(c.elbow_deg,160);
    c = apply(human(0,-90,90)); near(c.elbow_deg,160); /* User's 70deg bend cap. */
}

static void test_combined_direction(void)
{
    /* Independent reference: rotating down first 40deg outward, then 30deg
     * forward yields right=.642787610, forward=.383022222, up=-.663413948.
     * This distinguishes serial axes from independently adding A1 angles. */
    const float right = 0.642787610f, forward = 0.383022222f, up = -0.663413948f;
    HumanJointTarget h = human(atan2f(right,forward)*57.295779513f,
                              asinf(up)*57.295779513f,150);
    JointCommand c = apply(h);
    RobotJointPositions3D p;
    near(c.base_deg,120); near(c.shoulder_deg,130); near(c.elbow_deg,120);
    assert(robot_forward_kinematics_3d(&c,&p));
    near(p.elbow.x_cm,11*right);
    near(p.elbow.y_cm,11*forward);
    near(p.elbow.z_cm,11*up);

    /* Round-trip observed below-horizontal directions inside the robot range.
     * Check unit direction, link length, and no fixed base clamp. */
    for (int az=-170; az<=170; az+=17) {
        for (int elev=-85; elev<=-45; elev+=10) {
            h = human((float)az,(float)elev,150);
            c = apply(h);
            assert(robot_forward_kinematics_3d(&c,&p));
            near(p.elbow.x_cm/11,cosf(elev*0.01745329252f)*sinf(az*0.01745329252f));
            near(p.elbow.y_cm/11,cosf(elev*0.01745329252f)*cosf(az*0.01745329252f));
            near(p.elbow.z_cm/11,sinf(elev*0.01745329252f));
        }
    }
}

static void test_periodic_inputs_and_wrist_lock(void)
{
    HumanAngleUnwrapState state;
    HumanJointTarget h = human(179,-60,180);
    JointCommand first = apply(h), c;
    motion_control_unwrap_state_init(&state);
    motion_control_unwrap_target(&state,&h);
    h = human(-179,-60,180);
    motion_control_unwrap_target(&state,&h);
    near(h.base_deg,181); /* Existing integration state/API is unchanged. */
    c = apply(h);
    assert(fabsf(c.base_deg-first.base_deg) < 0.05f);
    assert(fabsf(c.shoulder_deg-first.shoulder_deg) < 1.1f);
    h.base_deg += 720;
    h.wrist_pitch_deg = -1250;
    h.wrist_roll_deg = 602.5f;
    h.gripper_norm = 1;
    c = apply(h);
    near(c.wrist_pitch_deg,90); near(c.wrist_roll_deg,90);
    near(c.gripper_norm,1);
    near(c.base_deg,first.base_deg);
    h = human(90,0,180); /* Pure lateral pole: choose neutral base, clamp abduction. */
    c = apply(h); near(c.base_deg,90); near(c.shoulder_deg,160);
}

static void test_limits_and_validation(void)
{
    HumanJointTarget h = human(0,-90,180);
    JointCommand c = {-100,200,500,-50,300,2,1};
    motion_control_apply_limits(&c);
    near(c.base_deg,20); near(c.shoulder_deg,160); near(c.elbow_deg,160);
    near(c.wrist_pitch_deg,20); near(c.wrist_roll_deg,160);
    near(c.gripper_norm,2); /* A2 does not clamp gripper in the joint-limit helper. */
    h.valid = 0; assert(!robot_calibration_apply(&h,&c)); assert(!c.valid);
    h.valid = 1; h.elbow_deg=NAN; assert(!motion_control_validate_target(&h));
    h=human(0,-91,180); assert(!motion_control_validate_target(&h));
    h=human(0,-90,181); assert(!motion_control_validate_target(&h));
    h=human(0,-90,180); h.gripper_norm=2; assert(!motion_control_validate_target(&h));
    assert(!motion_control_validate_target(NULL));
    c.valid=1; assert(!robot_calibration_apply(NULL,&c)); assert(!c.valid);
    assert(!robot_calibration_apply(&h,NULL));
    motion_control_apply_limits(NULL);
    motion_control_unwrap_state_init(NULL);
    motion_control_unwrap_target(NULL,&h);
}

static void test_command_speed_and_retarget(void)
{
    RobotMotionState s;
    JointCommand rest=apply(human(0,-90,180)), goal=rest, c, prev;
    robot_calibration_state_init(&s);
    robot_calibration_set_target(&s,&rest);
    robot_calibration_step(&s,&c);
    near(c.base_deg,90);
    goal.base_deg=150; goal.shoulder_deg=120; goal.elbow_deg=110;
    goal.gripper_norm=1;
    robot_calibration_set_target(&s,&goal);
    prev=c;
    for (int tick=0;tick<200;tick++) {
        robot_calibration_step(&s,&c);
        assert(fabsf(c.base_deg-prev.base_deg)<=0.6001f);
        assert(fabsf(c.shoulder_deg-prev.shoulder_deg)<=0.6001f);
        assert(fabsf(c.elbow_deg-prev.elbow_deg)<=0.6001f);
        near(c.gripper_norm,1);
        if(tick<40) { assert(c.base_deg<150); assert(c.shoulder_deg<120); }
        prev=c;
    }
    near(c.base_deg,150); near(c.shoulder_deg,120); near(c.elbow_deg,110);
    robot_calibration_set_target(&s,&rest);
    for(int tick=0;tick<200;tick++) {
        robot_calibration_step(&s,&c);
        assert(fabsf(c.base_deg-prev.base_deg)<=0.6001f);
        prev=c;
    }
    near(c.base_deg,90); near(c.shoulder_deg,90); near(c.elbow_deg,90);
    robot_calibration_state_init(NULL);
    robot_calibration_set_target(NULL,&rest);
    robot_calibration_set_target(&s,NULL);
    robot_calibration_step(NULL,&c);
    robot_calibration_step(&s,NULL);
}

static void test_streaming_retarget(void)
{
    RobotMotionState s;
    JointCommand rest=apply(human(0,-90,180)), goal=rest, c=rest, prev;
    robot_calibration_state_init(&s);
    robot_calibration_set_target(&s,&rest);
    /* 20Hz changing targets and a 50Hz control clock, starting far from goal.
     * Restarting smoothstep on every frame used to stall this stream. */
    for (int tick=0;tick<50;tick++) {
        if (tick%5==0 || tick%5==3) {
            goal.base_deg=150.0f+0.1f*tick;
            goal.shoulder_deg=120.0f+0.05f*tick;
            robot_calibration_set_target(&s,&goal);
        }
        prev=c;
        robot_calibration_step(&s,&c);
        assert(c.base_deg>=prev.base_deg);
        assert(fabsf(c.base_deg-prev.base_deg)<=0.6001f);
        assert(fabsf(c.shoulder_deg-prev.shoulder_deg)<=0.6001f);
    }
    assert(c.base_deg>110.0f);
    /* Reversal during a stream must still obey the same speed bound. */
    robot_calibration_set_target(&s,&rest);
    for (int tick=0;tick<150;tick++) {
        prev=c;
        robot_calibration_step(&s,&c);
        assert(fabsf(c.base_deg-prev.base_deg)<=0.6001f);
        assert(fabsf(c.shoulder_deg-prev.shoulder_deg)<=0.6001f);
    }
    near(c.base_deg,90); near(c.shoulder_deg,90);
}

int main(void)
{
    test_rest_and_single_axes();
    test_combined_direction();
    test_periodic_inputs_and_wrist_lock();
    test_limits_and_validation();
    test_command_speed_and_retarget();
    test_streaming_retarget();
    puts("test_robot_calibration: PASS (physical axes, mapping, bounds, speed)");
    return 0;
}
