#include "robot_calibration/safety_check.h"

#include <math.h>
#include <stddef.h>

#define PI_F 3.14159265358979323846f

#define LINK_SHOULDER_ELBOW_CM 11.0f
#define LINK_ELBOW_WRIST_CM 13.0f
#define LINK_WRIST_TIP_CM 8.0f
#define TOTAL_REACH_CM 32.0f

/* Mechanical clearances are conservative starting values pending hardware tests. */
#define MIN_JOINT_INTERIOR_DEG 25.0f
#define BASE_CLEARANCE_CM 2.0f
#define LINK_CLEARANCE_CM 1.0f
#define SINGULARITY_ANGLE_TOLERANCE_DEG 5.0f
#define REACH_BOUNDARY_RATIO 0.98f
#define GEOMETRY_EPSILON 0.00001f

static float degrees_to_radians(float degrees)
{
    return degrees * (PI_F / 180.0f);
}

static int command_is_finite(const JointCommand *command)
{
    return isfinite(command->base_deg) && isfinite(command->shoulder_deg) &&
           isfinite(command->elbow_deg) && isfinite(command->wrist_pitch_deg) &&
           isfinite(command->wrist_roll_deg) && isfinite(command->gripper_norm);
}

static RobotPoint2D point_on_link(RobotPoint2D start, float length_cm, float angle_rad)
{
    RobotPoint2D result;

    result.x_cm = start.x_cm + length_cm * cosf(angle_rad);
    result.z_cm = start.z_cm + length_cm * sinf(angle_rad);
    return result;
}

static float squared_distance(RobotPoint2D first, RobotPoint2D second)
{
    const float dx = first.x_cm - second.x_cm;
    const float dz = first.z_cm - second.z_cm;

    return dx * dx + dz * dz;
}

static float point_segment_distance(RobotPoint2D point,
                                    RobotPoint2D segment_start,
                                    RobotPoint2D segment_end)
{
    const float segment_x = segment_end.x_cm - segment_start.x_cm;
    const float segment_z = segment_end.z_cm - segment_start.z_cm;
    const float length_squared = segment_x * segment_x + segment_z * segment_z;
    float projection;
    RobotPoint2D closest;

    if (length_squared == 0.0f) {
        return sqrtf(squared_distance(point, segment_start));
    }

    projection = ((point.x_cm - segment_start.x_cm) * segment_x +
                  (point.z_cm - segment_start.z_cm) * segment_z) /
                 length_squared;
    projection = fmaxf(0.0f, fminf(1.0f, projection));
    closest.x_cm = segment_start.x_cm + projection * segment_x;
    closest.z_cm = segment_start.z_cm + projection * segment_z;
    return sqrtf(squared_distance(point, closest));
}

static float cross_product(RobotPoint2D first, RobotPoint2D second, RobotPoint2D third)
{
    return (second.x_cm - first.x_cm) * (third.z_cm - first.z_cm) -
           (second.z_cm - first.z_cm) * (third.x_cm - first.x_cm);
}

static int point_is_on_segment(RobotPoint2D point,
                               RobotPoint2D segment_start,
                               RobotPoint2D segment_end)
{
    return point.x_cm >= fminf(segment_start.x_cm, segment_end.x_cm) - GEOMETRY_EPSILON &&
           point.x_cm <= fmaxf(segment_start.x_cm, segment_end.x_cm) + GEOMETRY_EPSILON &&
           point.z_cm >= fminf(segment_start.z_cm, segment_end.z_cm) - GEOMETRY_EPSILON &&
           point.z_cm <= fmaxf(segment_start.z_cm, segment_end.z_cm) + GEOMETRY_EPSILON;
}

static int segments_intersect(RobotPoint2D first_start,
                              RobotPoint2D first_end,
                              RobotPoint2D second_start,
                              RobotPoint2D second_end)
{
    const float first_side_start = cross_product(first_start, first_end, second_start);
    const float first_side_end = cross_product(first_start, first_end, second_end);
    const float second_side_start = cross_product(second_start, second_end, first_start);
    const float second_side_end = cross_product(second_start, second_end, first_end);

    if (((first_side_start > GEOMETRY_EPSILON && first_side_end < -GEOMETRY_EPSILON) ||
         (first_side_start < -GEOMETRY_EPSILON && first_side_end > GEOMETRY_EPSILON)) &&
        ((second_side_start > GEOMETRY_EPSILON && second_side_end < -GEOMETRY_EPSILON) ||
         (second_side_start < -GEOMETRY_EPSILON && second_side_end > GEOMETRY_EPSILON))) {
        return 1;
    }

    if (fabsf(first_side_start) <= GEOMETRY_EPSILON &&
        point_is_on_segment(second_start, first_start, first_end)) {
        return 1;
    }
    if (fabsf(first_side_end) <= GEOMETRY_EPSILON &&
        point_is_on_segment(second_end, first_start, first_end)) {
        return 1;
    }
    if (fabsf(second_side_start) <= GEOMETRY_EPSILON &&
        point_is_on_segment(first_start, second_start, second_end)) {
        return 1;
    }
    return fabsf(second_side_end) <= GEOMETRY_EPSILON &&
           point_is_on_segment(first_end, second_start, second_end);
}

static float segment_segment_distance(RobotPoint2D first_start,
                                      RobotPoint2D first_end,
                                      RobotPoint2D second_start,
                                      RobotPoint2D second_end)
{
    float distance;

    if (segments_intersect(first_start, first_end, second_start, second_end)) {
        return 0.0f;
    }

    distance = point_segment_distance(first_start, second_start, second_end);
    distance = fminf(distance, point_segment_distance(first_end, second_start, second_end));
    distance = fminf(distance, point_segment_distance(second_start, first_start, first_end));
    distance = fminf(distance, point_segment_distance(second_end, first_start, first_end));
    return distance;
}

static float joint_interior_angle(float servo_angle_deg)
{
    float bend_deg = fmodf(fabsf(servo_angle_deg - 90.0f), 360.0f);

    if (bend_deg > 180.0f) {
        bend_deg = 360.0f - bend_deg;
    }
    return 180.0f - bend_deg;
}

static int has_self_collision(const JointCommand *command,
                              const RobotJointPositions2D *positions)
{
    const RobotPoint2D base = positions->shoulder;

    if (joint_interior_angle(command->elbow_deg) < MIN_JOINT_INTERIOR_DEG ||
        joint_interior_angle(command->wrist_pitch_deg) < MIN_JOINT_INTERIOR_DEG) {
        return 1;
    }

    if (point_segment_distance(base, positions->elbow, positions->wrist_pitch) <
            BASE_CLEARANCE_CM ||
        point_segment_distance(base, positions->wrist_pitch, positions->tip) <
            BASE_CLEARANCE_CM) {
        return 1;
    }

    return segment_segment_distance(positions->shoulder,
                                    positions->elbow,
                                    positions->wrist_pitch,
                                    positions->tip) < LINK_CLEARANCE_CM;
}

int robot_forward_kinematics_2d(const JointCommand *command,
                                RobotJointPositions2D *positions)
{
    float shoulder_angle;
    float elbow_link_angle;
    float wrist_link_angle;

    if (command == NULL || positions == NULL || !isfinite(command->shoulder_deg) ||
        !isfinite(command->elbow_deg) || !isfinite(command->wrist_pitch_deg)) {
        return 0;
    }

    /*
     * Unverified assembly convention: servo neutral (90 deg) means that the
     * elbow/wrist link continues straight from the preceding link.
     */
    shoulder_angle = degrees_to_radians(command->shoulder_deg);
    elbow_link_angle = shoulder_angle + degrees_to_radians(command->elbow_deg - 90.0f);
    wrist_link_angle =
        elbow_link_angle + degrees_to_radians(command->wrist_pitch_deg - 90.0f);

    positions->shoulder.x_cm = 0.0f;
    positions->shoulder.z_cm = 0.0f;
    positions->elbow =
        point_on_link(positions->shoulder, LINK_SHOULDER_ELBOW_CM, shoulder_angle);
    positions->wrist_pitch =
        point_on_link(positions->elbow, LINK_ELBOW_WRIST_CM, elbow_link_angle);
    positions->tip =
        point_on_link(positions->wrist_pitch, LINK_WRIST_TIP_CM, wrist_link_angle);
    return 1;
}

int safety_check_apply(const JointCommand *command,
                       RobotJointPositions2D *positions_out,
                       SafetyCheckFlags *issues_out)
{
    RobotJointPositions2D positions;
    SafetyCheckFlags issues = SAFETY_CHECK_OK;
    float elbow_bend_deg;
    float wrist_bend_deg;
    float tip_reach_cm;

    if (issues_out != NULL) {
        *issues_out = SAFETY_CHECK_OK;
    }

    if (command == NULL || command->valid == 0u || !command_is_finite(command) ||
        !robot_forward_kinematics_2d(command, &positions)) {
        if (issues_out != NULL) {
            *issues_out = SAFETY_CHECK_INVALID_COMMAND;
        }
        return 0;
    }

    if (has_self_collision(command, &positions)) {
        issues |= SAFETY_CHECK_SELF_COLLISION;
    }

    if (positions.elbow.z_cm < 0.0f || positions.wrist_pitch.z_cm < 0.0f ||
        positions.tip.z_cm < 0.0f) {
        issues |= SAFETY_CHECK_FLOOR_COLLISION;
    }

    elbow_bend_deg = fabsf(command->elbow_deg - 90.0f);
    wrist_bend_deg = fabsf(command->wrist_pitch_deg - 90.0f);
    if (elbow_bend_deg <= SINGULARITY_ANGLE_TOLERANCE_DEG &&
        wrist_bend_deg <= SINGULARITY_ANGLE_TOLERANCE_DEG) {
        issues |= SAFETY_CHECK_NEAR_SINGULARITY;
    }

    tip_reach_cm = sqrtf(squared_distance(positions.shoulder, positions.tip));
    if (tip_reach_cm >= TOTAL_REACH_CM * REACH_BOUNDARY_RATIO) {
        issues |= SAFETY_CHECK_REACH_BOUNDARY;
    }

    if (positions_out != NULL) {
        *positions_out = positions;
    }
    if (issues_out != NULL) {
        *issues_out = issues;
    }
    return issues == SAFETY_CHECK_OK;
}
