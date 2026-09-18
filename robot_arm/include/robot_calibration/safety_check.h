#ifndef ROBOT_CALIBRATION_SAFETY_CHECK_H
#define ROBOT_CALIBRATION_SAFETY_CHECK_H

#include <stdint.h>

#include "common/robot_types.h"

typedef struct {
    float x_cm;
    float z_cm;
} RobotPoint2D;

typedef struct {
    RobotPoint2D shoulder;
    RobotPoint2D elbow;
    RobotPoint2D wrist_pitch;
    RobotPoint2D tip;
} RobotJointPositions2D;

typedef uint32_t SafetyCheckFlags;

enum {
    SAFETY_CHECK_OK = 0u,
    SAFETY_CHECK_INVALID_COMMAND = 1u << 0,
    SAFETY_CHECK_SELF_COLLISION = 1u << 1,
    SAFETY_CHECK_FLOOR_COLLISION = 1u << 2,
    SAFETY_CHECK_NEAR_SINGULARITY = 1u << 3,
    SAFETY_CHECK_REACH_BOUNDARY = 1u << 4
};

/*
 * Computes planar joint positions in centimetres, with the shoulder axis at
 * (0, 0), +X forward and +Z up.
 *
 * Assembly assumption requiring physical verification:
 *   - shoulder_deg is an absolute angle from +X;
 *   - elbow_deg == 90 and wrist_pitch_deg == 90 continue the preceding link
 *     in a straight line.
 * Therefore the relative elbow and wrist bends are (servo_angle - 90 deg).
 * Base yaw and wrist roll do not affect positions in the 2D control plane.
 *
 * Returns 1 when the required angles are finite and both pointers are valid.
 * The command's valid field is intentionally not needed for this geometric
 * calculation; safety_check_apply() validates the complete command.
 */
int robot_forward_kinematics_2d(const JointCommand *command,
                                RobotJointPositions2D *positions);

/*
 * Returns 1 only when no safety issue is detected. positions_out and
 * issues_out are optional. Multiple SafetyCheckFlags may be set at once.
 */
int safety_check_apply(const JointCommand *command,
                       RobotJointPositions2D *positions_out,
                       SafetyCheckFlags *issues_out);

#endif /* ROBOT_CALIBRATION_SAFETY_CHECK_H */
