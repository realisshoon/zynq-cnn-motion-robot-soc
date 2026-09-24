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
 * distinguish fresh elbow heading/wrist angles from retained/default values.
 * A fresh 2D gripper observation may update while wrist angles are held. */
typedef struct {
    float elbow_roll_deg, elbow_pitch_deg;
    float wrist_pitch_deg, wrist_roll_deg, gripper_norm;
    uint32_t frame_id;
    uint8_t valid, elbow_roll_observable, hand_fresh;
} HumanForearmTarget;

/* Calibrated robot command: robot_calibration -> output_controller.
 * legacy 6축(base/shoulder/elbow) 경로용. 보드는 아직 이 경로로만 돈다 --
 * 지우거나 필드를 바꾸지 않는다(ForearmJointCommand가 대체 아님). */
typedef struct {
    float base_deg;

    float shoulder_deg;
    float elbow_deg;
    float wrist_pitch_deg;
    float wrist_roll_deg;

    float gripper_norm;

    uint8_t valid;
} JointCommand;

/* Calibrated robot command, 새 5축(팔꿈치부터 시작하는 수평 설치) 경로:
 * robot_calibration -> output_controller. 2026-09-22 사용자 확인: Agent2/3
 * 실물 통합에 이 구조체로 교체한다. JointCommand는 legacy 경로가 계속
 * 쓰므로 그대로 둔다. 필드 의미는 include/robot_calibration/
 * forearm_safety_check.h, forearm_motion_control.h 주석 참고. */
typedef struct {
    float elbow_roll_deg;   /* M0 */
    float elbow_pitch_deg;  /* M1 */
    float wrist_pitch_deg;  /* M2 */
    float wrist_roll_deg;   /* M3 */
    float gripper_norm;     /* M4, 0=CLOSE, 1=OPEN */

    uint8_t valid;
} ForearmJointCommand;

#endif /* COMMON_ROBOT_TYPES_H */
