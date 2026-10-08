#include "robot_calibration/forearm_safety_check.h"
#include "dual_arm_config.h"

#include <math.h>
#include <stddef.h>

#define DEG_TO_RAD 0.01745329251994329577f

/*
 * 사용자 확인 실측값(2026-10-04): 팔꿈치-손목(전완) 16cm,
 * 손목-그리퍼 끝(손) 20cm. 이전 사진 기반 24cm/10cm 가정을 대체한다.
 */
#define LINK_ELBOW_WRIST_CM 16.0f
#define LINK_WRIST_TIP_CM 20.0f

/*
 * 사용자 확인(2026-10-04): 팔꿈치(원점)가 테이블면보다 +10cm 위에 있다.
 * 좌표계가 원점=팔꿈치, +Z=위(테이블에서 멀어지는 방향)이므로 테이블면은
 * z=-10cm. 링크 두께(팔 굵기)는 여전히 모델에 없다 -- 중심선만 검사하므로
 * 실제로는 이보다 일찍 접촉할 수 있다.
 */
#define TABLE_SURFACE_Z_CM -10.0f

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

/* 전완 FK/안전검사 내부에서 사용하는 순수 기하 helper. */
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

/* straight_ref_deg = 이 관절이 "완전히 펴진(굽힘 0)" 서보 각도. 그 기준에서
 * 얼마나 굽었는지를 180도(완전히 폄)에서 뺀 "내각"으로 돌려준다(0도에
 * 가까울수록 완전히 접힘). */
static float joint_interior_angle(float servo_angle_deg, float straight_ref_deg)
{
    float bend = fabsf(fmodf(servo_angle_deg - straight_ref_deg, 360.0f));
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

static int right_forward_kinematics_3d(const ForearmJointCommand *c, ForearmJointPositions3D *p)
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
     * wrist_pitch=90(중립)은 사용자 확인(2026-09-22): 완전히 편 자세(0도
     * 굽힘)가 아니라 전완 방향 기준으로 90도 굽은 자세다 -- 완전히 펴두면
     * 하드웨어가 덜덜거려서 안정적인 굽힌 자세를 idle로 잡았다. 그래서
     * wrist_pitch만 다른 관절과 달리 -90 기준이 아니라 0을 기준으로 삼는다
     * (servo=0 -> 0도 굽힘/완전히 폄, servo=90 -> 90도 굽힘).
     */
    roll = (fmodf(c->elbow_roll_deg, 360.0f) - 90.0f) * DEG_TO_RAD;
    pitch = (fmodf(c->elbow_pitch_deg, 360.0f) - 90.0f) * DEG_TO_RAD;
    wroll = (fmodf(c->wrist_roll_deg, 360.0f) - 90.0f) * DEG_TO_RAD;
    wpitch = fmodf(c->wrist_pitch_deg, 360.0f) * DEG_TO_RAD;

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
    /* wrist_pitch는 0도가 완전히 폄 기준이다(위 FK 주석 참고) -- 다른
     * 관절처럼 90도가 기준이 아니다. */
    if (joint_interior_angle(command->wrist_pitch_deg, 0.0f) < MIN_WRIST_INTERIOR_DEG) return 1;
    /* elbow(원점)-손 구간만 확인한다: 전완 구간은 원점에서 바로 시작해
     * point_segment_distance가 항상 0에 가까워 의미가 없다(인접 링크). */
    if (point_segment_distance(p->elbow, p->wrist, p->tip) < BASE_CLEARANCE_CM) return 1;
    return 0;
}

#if !ROBOT_SPLIT_BOARD_CONTROL || !defined(ROBOT_STEREO_LEFT)
static int has_table_collision(const ForearmJointPositions3D *p)
{
    /* 직선 구간이므로 두 끝점만 확인하면 충분하다(테이블은 수평면이고
     * z는 구간을 따라 선형이라 최솟값이 항상 끝점에서 나온다). */
    return p->wrist.z_cm <= TABLE_SURFACE_Z_CM || p->tip.z_cm <= TABLE_SURFACE_Z_CM;
}
#endif

int forearm_robot_forward_kinematics_geometry(const ForearmJointCommand *command,
    const ForearmRobotGeometry *geometry, ForearmJointPositions3D *positions,
    RobotPoint3D *roll_motor_end)
{
    ForearmJointPositions3D reference;
    RobotPoint3D forearm_direction, hand_direction;
    if (geometry == NULL || positions == NULL || roll_motor_end == NULL ||
        !isfinite(geometry->elbow_wrist_cm) || !isfinite(geometry->wrist_roll_end_cm) ||
        !isfinite(geometry->roll_end_tip_cm) || !isfinite(geometry->table_z_cm) ||
        geometry->elbow_wrist_cm <= 0.0f || geometry->wrist_roll_end_cm <= 0.0f ||
        geometry->roll_end_tip_cm <= 0.0f ||
        !right_forward_kinematics_3d(command, &reference)) return 0;
    forearm_direction = sub(reference.wrist, reference.elbow);
    forearm_direction.x_cm /= LINK_ELBOW_WRIST_CM;
    forearm_direction.y_cm /= LINK_ELBOW_WRIST_CM;
    forearm_direction.z_cm /= LINK_ELBOW_WRIST_CM;
    hand_direction = sub(reference.tip, reference.wrist);
    hand_direction.x_cm /= LINK_WRIST_TIP_CM;
    hand_direction.y_cm /= LINK_WRIST_TIP_CM;
    hand_direction.z_cm /= LINK_WRIST_TIP_CM;
    positions->elbow = reference.elbow;
    positions->wrist = along(reference.elbow, forearm_direction, geometry->elbow_wrist_cm);
    *roll_motor_end = along(positions->wrist, hand_direction, geometry->wrist_roll_end_cm);
    positions->tip = along(positions->wrist, hand_direction,
                          geometry->wrist_roll_end_cm + geometry->roll_end_tip_cm);
    return 1;
}

const ForearmRobotGeometry *forearm_robot_selected_geometry(void)
{
#if ROBOT_SPLIT_BOARD_CONTROL && defined(ROBOT_STEREO_LEFT)
    static const ForearmRobotGeometry geometry = {
        ROBOT_LEFT_FOREARM_CM, ROBOT_LEFT_WRIST_ROLL_END_CM,
        ROBOT_LEFT_ROLL_END_TIP_CM, ROBOT_LEFT_TABLE_Z_CM
    };
#else
    static const ForearmRobotGeometry geometry = {
        ROBOT_RIGHT_FOREARM_CM, ROBOT_RIGHT_WRIST_ROLL_END_CM,
        ROBOT_RIGHT_ROLL_END_TIP_CM, ROBOT_RIGHT_TABLE_Z_CM
    };
#endif
    return &geometry;
}

int forearm_robot_forward_kinematics_3d(const ForearmJointCommand *command,
                                      ForearmJointPositions3D *positions)
{
#if ROBOT_SPLIT_BOARD_CONTROL && defined(ROBOT_STEREO_LEFT)
    RobotPoint3D roll_motor_end;
    return forearm_robot_forward_kinematics_geometry(command,
        forearm_robot_selected_geometry(), positions, &roll_motor_end);
#else
    return right_forward_kinematics_3d(command, positions);
#endif
}

int forearm_safety_check_geometry(const ForearmJointCommand *command,
    const ForearmRobotGeometry *geometry, uint32_t *issues)
{
    ForearmJointPositions3D positions;
    RobotPoint3D roll_motor_end;
    uint32_t flags = FOREARM_SAFETY_CHECK_OK;
    if (command == NULL || !command->valid || !command_is_finite(command) ||
        !forearm_robot_forward_kinematics_geometry(command, geometry,
                                                  &positions, &roll_motor_end)) {
        if (issues != NULL) *issues = FOREARM_SAFETY_CHECK_INVALID_COMMAND;
        return 0;
    }
    if (has_self_collision(command, &positions)) flags |= FOREARM_SAFETY_CHECK_SELF_COLLISION;
    if (positions.wrist.z_cm <= geometry->table_z_cm ||
        positions.tip.z_cm <= geometry->table_z_cm ||
        roll_motor_end.z_cm <= geometry->table_z_cm) flags |= FOREARM_SAFETY_CHECK_TABLE_COLLISION;
    if (issues != NULL) *issues = flags;
    return flags == FOREARM_SAFETY_CHECK_OK;
}

int forearm_safety_check_apply(const ForearmJointCommand *command,
                               ForearmSafetyCheckFlags *issues_out)
{
#if ROBOT_SPLIT_BOARD_CONTROL && defined(ROBOT_STEREO_LEFT)
    return forearm_safety_check_geometry(command,
        forearm_robot_selected_geometry(), issues_out);
#else
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
#endif
}
