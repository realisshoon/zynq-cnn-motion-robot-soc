#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "robot_calibration/forearm_safety_check.h"

static void near(float a, float b) { assert(fabsf(a - b) < 0.002f); }

static ForearmJointCommand command(float roll, float pitch, float wp, float wr)
{
    ForearmJointCommand c;
    c.elbow_roll_deg = roll; c.elbow_pitch_deg = pitch;
    c.wrist_pitch_deg = wp; c.wrist_roll_deg = wr;
    c.gripper_norm = 0.5f; c.valid = 1;
    return c;
}

static float length(RobotPoint3D a, RobotPoint3D b)
{
    return sqrtf((a.x_cm-b.x_cm)*(a.x_cm-b.x_cm) +
                 (a.y_cm-b.y_cm)*(a.y_cm-b.y_cm) +
                 (a.z_cm-b.z_cm)*(a.z_cm-b.z_cm));
}

/* 2026-09-22 사용자 확인: elbow_pitch=90(중립)은 전완이 지면과 수직으로
 * 서는(똑바로 위) 자세. wrist는 pitch 먼저 굽히고 roll이 그 결과를
 * 전완 축 주위로 돌린다. 아래 기대값은 이 규약으로 다시 계산한 것이다. */
static void test_known_poses(void)
{
    ForearmJointCommand c = command(90, 90, 90, 90);
    ForearmJointPositions3D p;

    assert(forearm_robot_forward_kinematics_3d(&c, &p));
    near(p.elbow.x_cm, 0); near(p.elbow.y_cm, 0); near(p.elbow.z_cm, 0);
    near(p.wrist.x_cm, 0); near(p.wrist.y_cm, 0); near(p.wrist.z_cm, 24); /* 똑바로 위 */
    near(p.tip.x_cm, 0); near(p.tip.y_cm, 0); near(p.tip.z_cm, 34);

    c = command(90, 180, 90, 90); /* elbow_pitch +90: 수평(roll=90 방위, +Y) */
    assert(forearm_robot_forward_kinematics_3d(&c, &p));
    near(p.wrist.x_cm, 0); near(p.wrist.y_cm, 24); near(p.wrist.z_cm, 0);

    /* 수평(pitch+90)에서 roll을 더 돌리면(+90) +Z RH 규칙대로 +Y -> -X. */
    c = command(180, 180, 90, 90);
    assert(forearm_robot_forward_kinematics_3d(&c, &p));
    near(p.wrist.x_cm, -24); near(p.wrist.y_cm, 0); near(p.wrist.z_cm, 0);

    c = command(90, 90, 180, 90); /* wrist_pitch +90, roll 중립: 굽힘은 +Y쪽(수평) */
    assert(forearm_robot_forward_kinematics_3d(&c, &p));
    near(p.tip.x_cm, 0); near(p.tip.y_cm, 10); near(p.tip.z_cm, 24);

    c = command(90, 90, 180, 180); /* wrist_pitch +90 뒤 wrist_roll +90: 굽힘 평면이 옆으로 회전 */
    assert(forearm_robot_forward_kinematics_3d(&c, &p));
    near(p.tip.x_cm, -10); near(p.tip.y_cm, 0); near(p.tip.z_cm, 24);

    c = command(90, 90, 90, 180); /* wrist_roll만 돌리고 안 굽히면 중심선은 안 움직인다 */
    assert(forearm_robot_forward_kinematics_3d(&c, &p));
    near(p.tip.x_cm, 0); near(p.tip.y_cm, 0); near(p.tip.z_cm, 34);
}

static void test_link_lengths_and_rotations(void)
{
    ForearmJointPositions3D p;
    for (int roll = 20; roll <= 160; roll += 13) {
        for (int pitch = 20; pitch <= 160; pitch += 13) {
            for (int wr = 20; wr <= 160; wr += 31) {
                ForearmJointCommand c = command((float)roll, (float)pitch, 77.0f, (float)wr);
                assert(forearm_robot_forward_kinematics_3d(&c, &p));
                near(length(p.elbow, p.wrist), 24.0f);
                near(length(p.wrist, p.tip), 10.0f);
            }
        }
    }
}

/* Independent reference: Rodrigues rotation about Cartesian unit axes, composed
 * to match the code's documented model: forearm_dir = Rz(roll) Rx(-pitch) (0,0,1),
 * hand_dir = Rz(roll) Rx(-pitch) Rz(wrist_roll) Rx(-wrist_pitch) (0,0,1) (pitch
 * bends first, then roll twists the result about the forearm axis).
 * Unlike a link-length check this detects a reflected axis or wrong wrist order. */
static RobotPoint3D rotate(RobotPoint3D v, RobotPoint3D axis, float degrees)
{
    double angle = degrees * 0.01745329251994329577;
    double c = cos(angle), s = sin(angle);
    double d = axis.x_cm*v.x_cm + axis.y_cm*v.y_cm + axis.z_cm*v.z_cm;
    RobotPoint3D r = {
        (float)(v.x_cm*c + (axis.y_cm*v.z_cm-axis.z_cm*v.y_cm)*s + axis.x_cm*d*(1-c)),
        (float)(v.y_cm*c + (axis.z_cm*v.x_cm-axis.x_cm*v.z_cm)*s + axis.y_cm*d*(1-c)),
        (float)(v.z_cm*c + (axis.x_cm*v.y_cm-axis.y_cm*v.x_cm)*s + axis.z_cm*d*(1-c))
    };
    return r;
}

static void test_independent_rotation_composition(void)
{
    const RobotPoint3D x = {1,0,0}, z = {0,0,1};
    for (int q=-70; q<=70; q+=35) for (int p=-70; p<=70; p+=35)
    for (int w=-70; w<=70; w+=35) for (int r=-70; r<=70; r+=35) {
        ForearmJointCommand c = command(90+q,90+p,90+w,90+r);
        ForearmJointPositions3D actual;
        RobotPoint3D forearm_dir = rotate(rotate((RobotPoint3D){0,0,1}, x, (float)-p), z, (float)q);
        RobotPoint3D hand_dir = rotate(rotate(rotate(rotate((RobotPoint3D){0,0,1},
            x, (float)-w), z, (float)r), x, (float)-p), z, (float)q);
        RobotPoint3D wrist = {24.0f*forearm_dir.x_cm, 24.0f*forearm_dir.y_cm, 24.0f*forearm_dir.z_cm};
        RobotPoint3D hand = {10.0f*hand_dir.x_cm, 10.0f*hand_dir.y_cm, 10.0f*hand_dir.z_cm};
        assert(forearm_robot_forward_kinematics_3d(&c,&actual));
        near(actual.wrist.x_cm,wrist.x_cm); near(actual.wrist.y_cm,wrist.y_cm);
        near(actual.wrist.z_cm,wrist.z_cm);
        near(actual.tip.x_cm,wrist.x_cm+hand.x_cm);
        near(actual.tip.y_cm,wrist.y_cm+hand.y_cm);
        near(actual.tip.z_cm,wrist.z_cm+hand.z_cm);
        near(length(actual.elbow,actual.wrist),24);
        near(length(actual.wrist,actual.tip),10);
    }
}

static void test_self_collision(void)
{
    ForearmSafetyCheckFlags flags = UINT32_MAX;
    /* wrist_pitch가 90에서 아주 많이 벗어나 내각이 25도 미만(기계적 간섭 우려).
     * joint_interior_angle()은 servo 값만 보므로 elbow_pitch 좌표계 변경과 무관. */
    ForearmJointCommand c = command(90, 90, 90 - (180 - 25) - 1, 90);
    assert(!forearm_safety_check_apply(&c, &flags));
    assert(flags & FOREARM_SAFETY_CHECK_SELF_COLLISION);

    /* 정상 범위 내 자세는 자기충돌이 없어야 한다. */
    c = command(90, 90, 90, 90);
    assert(forearm_safety_check_apply(&c, &flags));
    assert(flags == FOREARM_SAFETY_CHECK_OK);
}

static void test_table_collision(void)
{
    ForearmSafetyCheckFlags flags = UINT32_MAX;
    ForearmJointCommand c;

    /* elbow_pitch=90(중립, 똑바로 위)에서 벗어나 수평 아래로 넘어가면 손목부터
     * 테이블(-5cm)을 뚫는다. pitch=-13에서 wrist.z=-5.399. */
    c = command(90, -13, 90, 90);
    assert(!forearm_safety_check_apply(&c, &flags));
    assert(flags & FOREARM_SAFETY_CHECK_TABLE_COLLISION);

    /* 손목은 테이블 위(수평, wrist.z=0)지만 손만 굽혀 테이블을 뚫는 경우:
     * roll=0,pitch=0,wrist_roll=90(중립)에서 tip.z=10*sin(wp_rad),
     * wp_rad=(wp_deg-90). 경계 wp_deg=60(tip.z=-5.0000 정확히, 계산으로 확인). */
    c = command(0, 0, 59.0f, 90);
    assert(!forearm_safety_check_apply(&c, &flags));
    assert(flags == FOREARM_SAFETY_CHECK_TABLE_COLLISION);
    c.wrist_pitch_deg = 61.0f;
    assert(forearm_safety_check_apply(&c, &flags));
    c.wrist_pitch_deg = 60.0f;
    assert(!forearm_safety_check_apply(&c, &flags));
    assert(flags == FOREARM_SAFETY_CHECK_TABLE_COLLISION);
    {
        ForearmJointPositions3D p;
        assert(forearm_robot_forward_kinematics_3d(&c, &p));
        assert(p.wrist.z_cm > -5.0f); /* 손목 자체는 안전 */
        assert(p.tip.z_cm <= -5.0f);  /* 손끝만 충돌 */
    }

    /* 중립(똑바로 위)은 테이블과 무관해야 한다. */
    c = command(90, 90, 90, 90);
    assert(forearm_safety_check_apply(&c, &flags));
    assert(!(flags & FOREARM_SAFETY_CHECK_TABLE_COLLISION));
}

static void test_invalid(void)
{
    ForearmSafetyCheckFlags flags = UINT32_MAX;
    ForearmJointCommand c = command(90, 90, 90, 90);
    ForearmJointPositions3D p;

    c.valid = 0;
    assert(!forearm_safety_check_apply(&c, &flags));
    assert(flags == FOREARM_SAFETY_CHECK_INVALID_COMMAND);

    c.valid = 1; c.elbow_roll_deg = NAN;
    assert(!forearm_safety_check_apply(&c, &flags));
    assert(flags == FOREARM_SAFETY_CHECK_INVALID_COMMAND);
    assert(!forearm_robot_forward_kinematics_3d(&c, &p));
    assert(!forearm_robot_forward_kinematics_3d(NULL, &p));
    assert(!forearm_robot_forward_kinematics_3d(&c, NULL));
    assert(!forearm_safety_check_apply(NULL, &flags));
    assert(flags == FOREARM_SAFETY_CHECK_INVALID_COMMAND);
}

int main(void)
{
    test_known_poses();
    test_link_lengths_and_rotations();
    test_independent_rotation_composition();
    test_self_collision();
    test_table_collision();
    test_invalid();
    puts("test_forearm_safety_check: PASS (3D FK, link lengths, self/table collision)");
    return 0;
}
