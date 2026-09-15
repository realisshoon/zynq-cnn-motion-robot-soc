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

/* Human joint-space target: human_target_angle -> robot_calibration. */
typedef struct {
    float base_deg;

    float shoulder_deg;
    float elbow_deg;
    float wrist_pitch_deg;
    float wrist_roll_deg;

    float gripper_norm;

    uint8_t valid;
} HumanJointTarget;

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
