#include "robot_calibration/forearm_safety_check.h"

#include <math.h>
#include <stddef.h>

#define DEG_TO_RAD 0.01745329251994329577f

/*
 * *** 사용자가 사진으로 준 실측값(2026-09-22): 두 구간이 24cm/10cm ***.
 * 어느 쪽이 "팔꿈치-손목(전완)"이고 어느 쪽이 "손목-그리퍼(손)"인지는
 * 사진만으로 확정할 수 없어서, 일반적으로 전완이 손보다 긴 점을 근거로
 * 24cm=전완, 10cm=손으로 가정했다. *** 이 가정은 사용자 확인이 필요하다 ***
 * (반대라면 아래 두 상수만 바꾸면 된다. FK 형태 자체는 안 바뀐다).
 */
#define LINK_ELBOW_WRIST_CM 24.0f
#define LINK_WRIST_TIP_CM 10.0f

/*
 * 사용자 확인(2026-09-22): 팔꿈치(원점)가 테이블면보다 +5cm 위에 있다.
 * 좌표계가 원점=팔꿈치, +Z=위(테이블에서 멀어지는 방향)이므로 테이블면은
 * z=-5cm. 링크 두께(팔 굵기)는 여전히 모델에 없다 -- 중심선만 검사하므로
 * 실제로는 이보다 일찍 접촉할 수 있다.
 */
#define TABLE_SURFACE_Z_CM -5.0f

/* 기존 6축 safety_check.c와 같은 개념의 여유(기계적 간섭 방지, 토크/하중
 * 보호 아님). */
#define MIN_WRIST_INTERIOR_DEG 25.0f
#define BASE_CLEARANCE_CM 2.0f
#define GEOMETRY_EPSILON 0.000001f

static float clamp01(float v)
{
    return fmaxf(0.0f, fminf(1.0f, v));
}

static int command_is_finite(const ForearmJointCommand *c)
{
    return isfinite(c->elbow_roll_deg) && isfinite(c->elbow_pitch_deg) &&
           isfinite(c->wrist_pitch_deg) && isfinite(c->wrist_roll_deg) &&
           isfinite(c->gripper_norm);
}

/* safety_check.c의 것과 동일한 순수 기하 helper. 기존 legacy 경로를 전혀
 * 건드리지 않기 위해 export 대신 그대로 복제했다(코드량이 작다). */
static RobotPoint3D sub(RobotPoint3D a, RobotPoint3D b)
{
    RobotPoint3D r = {a.x_cm-b.x_cm, a.y_cm-b.y_cm, a.z_cm-b.z_cm};
    return r;
}

static float dot(RobotPoint3D a, RobotPoint3D b)
{
    return a.x_cm*b.x_cm + a.y_cm*b.y_cm + a.z_cm*b.z_cm;
}

static RobotPoint3D along(RobotPoint3D p, RobotPoint3D v, float distance)
{
    RobotPoint3D r = {p.x_cm + distance*v.x_cm,
                      p.y_cm + distance*v.y_cm,
                      p.z_cm + distance*v.z_cm};
    return r;
}

static float distance(RobotPoint3D a, RobotPoint3D b)
{
    RobotPoint3D d = sub(a, b);
    return sqrtf(dot(d, d));
}

static float point_segment_distance(RobotPoint3D p, RobotPoint3D a, RobotPoint3D b)
{
    RobotPoint3D v = sub(b, a);
    float len2 = dot(v, v);
    float t = len2 > GEOMETRY_EPSILON ? clamp01(dot(sub(p, a), v)/len2) : 0.0f;
    return distance(p, along(a, v, t));
}

static float joint_interior_angle(float servo_angle_deg)
{
    float bend = fabsf(fmodf(servo_angle_deg - 90.0f, 360.0f));
    if (bend > 180.0f) bend = 360.0f - bend;
    return 180.0f - bend;
}

static RobotPoint3D cross(RobotPoint3D a, RobotPoint3D b)
{
    RobotPoint3D r = {a.y_cm*b.z_cm - a.z_cm*b.y_cm,
                      a.z_cm*b.x_cm - a.x_cm*b.z_cm,
                      a.x_cm*b.y_cm - a.y_cm*b.x_cm};
    return r;
}

/* Rodrigues 회전: v를 단위축 axis 주위로 오른손 회전(cos_a=cos(각도), sin_a=sin(각도)). */
static RobotPoint3D rotate_about_axis(RobotPoint3D v, RobotPoint3D axis, float cos_a, float sin_a)
{
    float k_dot_v = dot(axis, v);
    RobotPoint3D k_cross_v = cross(axis, v);
    RobotPoint3D r;
    r.x_cm = v.x_cm*cos_a + k_cross_v.x_cm*sin_a + axis.x_cm*k_dot_v*(1.0f-cos_a);
    r.y_cm = v.y_cm*cos_a + k_cross_v.y_cm*sin_a + axis.y_cm*k_dot_v*(1.0f-cos_a);
    r.z_cm = v.z_cm*cos_a + k_cross_v.z_cm*sin_a + axis.z_cm*k_dot_v*(1.0f-cos_a);
    return r;
}

int forearm_robot_forward_kinematics_3d(const ForearmJointCommand *c, ForearmJointPositions3D *p)
{
    float roll, pitch, wroll, wpitch;
    RobotPoint3D forward0, up0;
    RobotPoint3D forearm_dir, bend_perp, hand_before_roll, hand_dir;

    if (c == NULL || p == NULL || !isfinite(c->elbow_roll_deg) ||
        !isfinite(c->elbow_pitch_deg) || !isfinite(c->wrist_pitch_deg) ||
        !isfinite(c->wrist_roll_deg)) return 0;

    /*
     * 서보 각도(90=기준 자세)를 라디안으로. elbow_roll이 원점 주위 방위각
     * (+Z 오른손 회전). elbow_pitch=90(중립)은 사용자 확인(2026-09-22):
     * "지면과 수직으로 서는 상태" -- 즉 전완이 똑바로 위(+Z)를 향하는 게
     * 기준 자세이고, elbow_pitch가 거기서부터 앞/뒤(roll 방위 평면 안)로
     * 기울이는 각이다. 차렷 수평축의 legacy base/shoulder 합성과는 다른
     * yaw/vertical-elevation 모델이다.
     * 손목은 사용자 확인(2026-09-22): wrist_pitch(굽힘)가 먼저 적용된 뒤
     * wrist_roll(비틀림)이 전완 축 주위로 그 결과를 돌린다 -- 기구학적으로
     * 더 안정적이라는 판단. 이전(roll먼저) 모델과 반대 순서다.
     */
    roll = (fmodf(c->elbow_roll_deg, 360.0f) - 90.0f) * DEG_TO_RAD;
    pitch = (fmodf(c->elbow_pitch_deg, 360.0f) - 90.0f) * DEG_TO_RAD;
    wroll = (fmodf(c->wrist_roll_deg, 360.0f) - 90.0f) * DEG_TO_RAD;
    wpitch = (fmodf(c->wrist_pitch_deg, 360.0f) - 90.0f) * DEG_TO_RAD;

    /* Neutral is robot +Y: Table +X -> robot +Y, Table +Y -> robot -X.
     * Rz(+yaw) sends +Y toward -X, not +X. Both frames are right-handed. */
    forward0 = (RobotPoint3D){-sinf(roll), cosf(roll), 0.0f};
    up0 = (RobotPoint3D){0.0f, 0.0f, 1.0f};

    /* pitch=0(servo=90)에서 forearm_dir=up0(똑바로 위). pitch가 늘면 roll이
     * 정한 방위(forward0) 쪽으로 눕는다. forward0/up0는 서로 수직인
     * 단위벡터라 pitch로 만든 forearm_dir/bend_perp도 서로 수직인
     * 단위벡터다(둘 다 forward0,up0의 회전 조합). */
    forearm_dir = along(along((RobotPoint3D){0,0,0}, up0, cosf(pitch)), forward0, sinf(pitch));
    bend_perp = along(along((RobotPoint3D){0,0,0}, forward0, cosf(pitch)), up0, -sinf(pitch));

    /* 손목: wpitch로 (forearm_dir,bend_perp) 평면 안에서 먼저 굽히고, 그
     * 결과를 wroll로 전완 축(forearm_dir) 주위로 돌린다(Rodrigues). */
    hand_before_roll = along(along((RobotPoint3D){0,0,0}, forearm_dir, cosf(wpitch)), bend_perp, sinf(wpitch));
    hand_dir = rotate_about_axis(hand_before_roll, forearm_dir, cosf(wroll), sinf(wroll));

    p->elbow = (RobotPoint3D){0,0,0};
    p->wrist = along(p->elbow, forearm_dir, LINK_ELBOW_WRIST_CM);
    p->tip = along(p->wrist, hand_dir, LINK_WRIST_TIP_CM);
    return 1;
}

static int has_self_collision(const ForearmJointCommand *command, const ForearmJointPositions3D *p)
{
    if (joint_interior_angle(command->wrist_pitch_deg) < MIN_WRIST_INTERIOR_DEG) return 1;
    /* elbow(원점)-손 구간만 확인한다: 전완 구간은 원점에서 바로 시작해
     * point_segment_distance가 항상 0에 가까워 의미가 없다(인접 링크). */
    if (point_segment_distance(p->elbow, p->wrist, p->tip) < BASE_CLEARANCE_CM) return 1;
    return 0;
}

static int has_table_collision(const ForearmJointPositions3D *p)
{
    /* 직선 구간이므로 두 끝점만 확인하면 충분하다(테이블은 수평면이고
     * z는 구간을 따라 선형이라 최솟값이 항상 끝점에서 나온다). */
    return p->wrist.z_cm <= TABLE_SURFACE_Z_CM || p->tip.z_cm <= TABLE_SURFACE_Z_CM;
}

int forearm_safety_check_apply(const ForearmJointCommand *command,
                               ForearmSafetyCheckFlags *issues_out)
{
    ForearmJointPositions3D p;
    ForearmSafetyCheckFlags issues = FOREARM_SAFETY_CHECK_OK;

    if (issues_out != NULL) *issues_out = FOREARM_SAFETY_CHECK_OK;
    if (command == NULL || !command->valid || !command_is_finite(command) ||
        !forearm_robot_forward_kinematics_3d(command, &p)) {
        if (issues_out != NULL) *issues_out = FOREARM_SAFETY_CHECK_INVALID_COMMAND;
        return 0;
    }

    if (has_self_collision(command, &p)) issues |= FOREARM_SAFETY_CHECK_SELF_COLLISION;
    if (has_table_collision(&p)) issues |= FOREARM_SAFETY_CHECK_TABLE_COLLISION;

    if (issues_out != NULL) *issues_out = issues;
    return issues == FOREARM_SAFETY_CHECK_OK;
}
