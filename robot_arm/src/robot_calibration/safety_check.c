#include "robot_calibration/safety_check.h"

#include <math.h>
#include <stddef.h>

#define DEG_TO_RAD 0.01745329251994329577f
#define LINK_SHOULDER_ELBOW_CM 11.0f
#define LINK_ELBOW_WRIST_CM 13.0f
/* Existing wrist-to-tip approximation; measure the actual gripper endpoint. */
#define LINK_WRIST_TIP_CM 8.0f

/* Existing provisional mechanical clearances, not motor torque protection. */
#define MIN_JOINT_INTERIOR_DEG 25.0f
#define BASE_CLEARANCE_CM 2.0f
#define LINK_CLEARANCE_CM 1.0f
#define GEOMETRY_EPSILON 0.000001f

static float clamp01(float v)
{
    return fmaxf(0.0f, fminf(1.0f, v));
}

static int command_is_finite(const JointCommand *c)
{
    return isfinite(c->base_deg) && isfinite(c->shoulder_deg) &&
           isfinite(c->elbow_deg) && isfinite(c->wrist_pitch_deg) &&
           isfinite(c->wrist_roll_deg) && isfinite(c->gripper_norm);
}

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

/* Closest points of two finite 3D segments, including parallel/degenerate cases.
 * Projecting to a side view first would create false collisions at abduction. */
static float segment_segment_distance(RobotPoint3D p, RobotPoint3D q,
                                      RobotPoint3D r, RobotPoint3D s)
{
    RobotPoint3D u = sub(q, p), v = sub(s, r), w = sub(p, r);
    float a = dot(u,u), b = dot(u,v), c = dot(v,v);
    float d = dot(u,w), e = dot(v,w), determinant = a*c-b*b;
    float first, second;

    if (a < GEOMETRY_EPSILON) return point_segment_distance(p, r, s);
    if (c < GEOMETRY_EPSILON) return point_segment_distance(r, p, q);
    first = determinant > GEOMETRY_EPSILON*a*c
        ? clamp01((b*e-c*d)/determinant) : 0.0f;
    second = (b*first+e)/c;
    if (second < 0.0f) {
        second = 0.0f;
        first = clamp01(-d/a);
    } else if (second > 1.0f) {
        second = 1.0f;
        first = clamp01((b-d)/a);
    }
    return distance(along(p,u,first), along(r,v,second));
}

static float joint_interior_angle(float servo_angle_deg)
{
    float bend = fabsf(fmodf(servo_angle_deg - 90.0f, 360.0f));
    if (bend > 180.0f) bend = 360.0f - bend;
    return 180.0f - bend;
}

static int has_self_collision(const JointCommand *command,
                              const RobotJointPositions3D *p)
{
    if (joint_interior_angle(command->elbow_deg) < MIN_JOINT_INTERIOR_DEG ||
        joint_interior_angle(command->wrist_pitch_deg) < MIN_JOINT_INTERIOR_DEG) {
        return 1;
    }
    if (point_segment_distance(p->shoulder, p->elbow, p->wrist_pitch) < BASE_CLEARANCE_CM ||
        point_segment_distance(p->shoulder, p->wrist_pitch, p->tip) < BASE_CLEARANCE_CM) {
        return 1;
    }
    return segment_segment_distance(p->shoulder, p->elbow, p->wrist_pitch, p->tip)
        < LINK_CLEARANCE_CM;
}

int robot_forward_kinematics_3d(const JointCommand *c, RobotJointPositions3D *p)
{
    float b, a, e, w;
    RobotPoint3D upper, forward, forearm, hand;

    if (c == NULL || p == NULL || !isfinite(c->base_deg) ||
        !isfinite(c->shoulder_deg) || !isfinite(c->elbow_deg) ||
        !isfinite(c->wrist_pitch_deg)) return 0;

    /* Physical servo axes, independent of human->servo calibration offsets:
     * base flexion -> local shoulder abduction -> local elbow flexion.
     * All 90 = straight down. Increasing the first three commands means
     * forward / outward / flexion (user-confirmed).
     * Matrix form: Rx(b) Ry(-a) Rx(e), acting on the neutral down vector.
     */
    b = (fmodf(c->base_deg,360.0f)-90.0f)*DEG_TO_RAD;
    a = (fmodf(c->shoulder_deg,360.0f)-90.0f)*DEG_TO_RAD;
    e = (fmodf(c->elbow_deg,360.0f)-90.0f)*DEG_TO_RAD;
    w = (fmodf(c->wrist_pitch_deg,360.0f)-90.0f)*DEG_TO_RAD;
    upper = (RobotPoint3D){sinf(a), cosf(a)*sinf(b), -cosf(a)*cosf(b)};
    forward = (RobotPoint3D){0.0f, cosf(b), sinf(b)};
    forearm = along((RobotPoint3D){0,0,0}, upper, cosf(e));
    forearm = along(forearm, forward, sinf(e));
    /* Provisional wrist pitch convention: positive bends toward local forward.
     * A2 locks wrist commands to 90 until wrist directions are measured.
     * Axial wrist roll does not displace this centerline tip approximation. */
    hand = along((RobotPoint3D){0,0,0}, upper, cosf(e+w));
    hand = along(hand, forward, sinf(e+w));

    p->shoulder = (RobotPoint3D){0,0,0};
    p->elbow = along(p->shoulder, upper, LINK_SHOULDER_ELBOW_CM);
    p->wrist_pitch = along(p->elbow, forearm, LINK_ELBOW_WRIST_CM);
    p->tip = along(p->wrist_pitch, hand, LINK_WRIST_TIP_CM);
    return 1;
}

static RobotPoint2D side_view(RobotPoint3D p)
{
    RobotPoint2D result = {p.y_cm, p.z_cm};
    return result;
}

static void project_side_view(const RobotJointPositions3D *p, RobotJointPositions2D *out)
{
    out->shoulder = side_view(p->shoulder);
    out->elbow = side_view(p->elbow);
    out->wrist_pitch = side_view(p->wrist_pitch);
    out->tip = side_view(p->tip);
}

int robot_forward_kinematics_2d(const JointCommand *command, RobotJointPositions2D *positions)
{
    RobotJointPositions3D p;
    if (positions == NULL || !robot_forward_kinematics_3d(command, &p)) return 0;
    project_side_view(&p, positions);
    return 1;
}

int safety_check_apply(const JointCommand *command,
                       RobotJointPositions2D *positions_out,
                       SafetyCheckFlags *issues_out)
{
    RobotJointPositions3D p;
    SafetyCheckFlags issues = SAFETY_CHECK_OK;
    if (issues_out != NULL) *issues_out = SAFETY_CHECK_OK;
    if (command == NULL || !command->valid || !command_is_finite(command) ||
        !robot_forward_kinematics_3d(command, &p)) {
        if (issues_out != NULL) *issues_out = SAFETY_CHECK_INVALID_COMMAND;
        return 0;
    }
    if (has_self_collision(command, &p)) issues |= SAFETY_CHECK_SELF_COLLISION;

    /* Floor remains disabled per the existing installation decision. To restore
     * it, measure shoulder height H and compare 3D z against -H, not zero.
     * Straight/reach-boundary poses are allowed for direct joint tracking:
     * idle and lateral raises must not be rejected just for full extension.
     * This is not a torque or load guarantee; no load model is available yet. */
    if (positions_out != NULL) project_side_view(&p, positions_out);
    if (issues_out != NULL) *issues_out = issues;
    return issues == SAFETY_CHECK_OK;
}
