#ifndef FOREARM_MAPPING_H
#define FOREARM_MAPPING_H

#include "human_target_angle/pose_mapping.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Camera-space axes. Z=table up, X=neutral forward projected onto table,
 * Y=Z cross X. Calibration is supplied by the application, not inferred
 * from shoulders. A valid demonstration frame is NOT physical calibration. */
typedef struct {
    Point3D x, y, z;
    uint8_t valid;
    uint8_t calibrated;
} TableFrame;

/* Human semantics, NOT servo commands. Do not cast to HumanJointTarget.
 * yaw [-180,180): azimuth about table +Z, zero +X.
 * pitch [-90,90]: elevation above table; NOT anatomical elbow angle.
 * wrist pitch [-180,180): flexion toward hand normal (up at neutral roll).
 * wrist roll [-180,180): RH rotation about elbow->wrist, reference documented
 * in docs/agent1_forearm.md. Gripper is binary 0=CLOSE, 1=OPEN.
 * frame_id identifies the last fresh major target (unchanged during HOLD).
 * valid covers major geometry only. yaw_observable=0 means hold M0;
 * hand_fresh=0 means retained/default wrist+gripper, not a new measurement.
 * calibrated=0 MUST NOT be treated as physical robot alignment. */
typedef struct {
    float forearm_yaw_deg, forearm_pitch_deg;
    float wrist_pitch_deg, wrist_roll_deg, gripper_norm;
    uint32_t frame_id;
    uint8_t valid, yaw_observable, hand_fresh, calibrated;
} HumanForearmTarget;

typedef struct {
    PoseMappingContext pose; /* unchanged tracking/reconstruction/hand state */
    TableFrame table;
    HumanForearmTarget last_target;
    float yaw_unwrapped_deg, pitch_deg;
    float raw_yaw_deg, raw_pitch_deg; /* diagnostic, before angle EMA */
    Point3D wrist_reference;
    uint8_t angle_valid, yaw_initialized, yaw_singular, last_target_valid;
} ForearmMappingContext;

/* No implicit TableFrame. update is invalid until explicitly configured. */
int forearm_mapping_init(ForearmMappingContext *ctx);
/* Invalid arguments do not alter an existing configuration. Successful
 * reconfiguration clears temporal state and wrist zero calibration. */
int forearm_mapping_set_table(ForearmMappingContext *ctx, Point3D up,
                             Point3D forward, uint8_t calibrated);
/* Same fresh=1 / HOLD or duplicate=0 / invalid=-1 convention as legacy API.
 * A side change resets history but preserves the fixed TableFrame. */
int forearm_mapping_update(ForearmMappingContext *ctx, const HumanPose2D *pose,
                           PoseArmSide side, float dt_sec,
                           HumanForearmTarget *target);

#ifdef __cplusplus
}
#endif
#endif
