#ifndef COMMON_ROBOT_TYPES_H
#define COMMON_ROBOT_TYPES_H

#include <stdint.h>

/* A point measured in camera pixel coordinates. */
typedef struct {
    float x;
    float y;
    uint8_t valid;
} Point2D;

/* A point expressed in three-dimensional coordinates. */
typedef struct {
    float x;
    float y;
    float z;
    uint8_t valid;
} Point3D;

/* Pose detector output consumed by the human target angle module. */
typedef struct {
    Point2D finger1;
    Point2D finger2;
    Point2D elbow;
    Point2D wrist;
    Point2D shoulder_l;
    Point2D shoulder_r;

    uint32_t frame_id;
    uint8_t valid;
} HumanPose2D;

/* Legacy six-axis human target. Existing Agent2/integration still consumes
 * this type; do not reinterpret HumanForearmTarget as this structure. */
typedef struct {
    float base_deg;

    float shoulder_deg;
    float elbow_deg;
    float wrist_pitch_deg;
    float wrist_roll_deg;

    float gripper_norm;

    uint8_t valid;
} HumanJointTarget;

/* Five-axis HUMAN angles: Agent1 -> Agent2 (not robot servo commands).
 * elbow_roll: Body +Y azimuth, zero +Z, positive toward +X, [-180,180).
 * elbow_pitch: elevation above Body XZ, +Body Y positive, [-90,90].
 * Neither field is the legacy anatomical elbow inner angle.
 * wrist_pitch: positive about projected Finger1->Finger2 axis, [-180,180).
 * wrist_roll: RH about Elbow->Wrist relative to Body/forearm reference,
 * [-180,180). See docs/agent1_forearm.md for zero and singularities.
 * gripper: 0=CLOSE, 1=OPEN.
 * frame_id retains the last fresh major measurement's ID during HOLD.
 * valid covers major geometry only; elbow_roll_observable and hand_fresh
 * distinguish a fresh observation from retained/default values. */
typedef struct {
    float elbow_roll_deg, elbow_pitch_deg;
    float wrist_pitch_deg, wrist_roll_deg, gripper_norm;
    uint32_t frame_id;
    uint8_t valid, elbow_roll_observable, hand_fresh;
} HumanForearmTarget;

/* Calibrated robot command: robot_calibration -> output_controller. */
typedef struct {
    float base_deg;

    float shoulder_deg;
    float elbow_deg;
    float wrist_pitch_deg;
    float wrist_roll_deg;

    float gripper_norm;

    uint8_t valid;
} JointCommand;

#endif /* COMMON_ROBOT_TYPES_H */
