#ifndef ROBOT_CALIBRATION_SAFETY_CHECK_H
#define ROBOT_CALIBRATION_SAFETY_CHECK_H

#include <stdint.h>
#include "common/robot_types.h"

/* Robot shoulder origin, cm: +X outward/right, +Y forward, +Z up. */
typedef struct {
    float x_cm;
    float y_cm;
    float z_cm;
} RobotPoint3D;

typedef struct {
    RobotPoint3D shoulder;
    RobotPoint3D elbow;
    RobotPoint3D wrist_pitch;
    RobotPoint3D tip;
} RobotJointPositions3D;

/* Legacy side-view API. x_cm is forward (3D y), z_cm is up; lateral position
 * is discarded. Use the 3D API for geometry, never this projection for safety. */
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
    /* Reserved for wire/log compatibility; no longer emitted. Floor disabled
     * by installation policy; direct joint tracking permits straight arms. */
    SAFETY_CHECK_FLOOR_COLLISION = 1u << 2,
    SAFETY_CHECK_NEAR_SINGULARITY = 1u << 3,
    SAFETY_CHECK_REACH_BOUNDARY = 1u << 4
};

/* Serial base flexion -> shoulder abduction -> elbow flexion -> wrist pitch.
 * 90 at every joint is straight down. Positive command changes lift forward,
 * abduct outward, and flex the elbow. Both proximal joints have horizontal
 * axes at idle; base is NOT yaw. Geometry ignores command.valid intentionally.
 * Wrist pitch sign/tool offset remain provisional; mapping locks both wrists
 * at 90 for this bring-up. The 8 cm terminal centerline retains the old estimate.
 */
int robot_forward_kinematics_3d(const JointCommand *command, RobotJointPositions3D *positions);
int robot_forward_kinematics_2d(const JointCommand *command, RobotJointPositions2D *positions);

/* Finite/valid command plus 3D geometric self-collision check. Joint angle
 * limits are applied by the calibration layer. No floor/load/torque protection.
 * Existing signature retained for pipeline/trace; optional positions are the
 * lossy side view. Returns 1 iff issues == OK. */
int safety_check_apply(const JointCommand *command,
                       RobotJointPositions2D *positions_out,
                       SafetyCheckFlags *issues_out);

#endif
