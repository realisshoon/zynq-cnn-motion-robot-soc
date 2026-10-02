#ifndef FOREARM_MAPPING_H
#define FOREARM_MAPPING_H

#include "human_target_angle/pose_mapping.h"

#ifdef __cplusplus
extern "C" {
#endif

/* HumanForearmTarget is shared through common/robot_types.h.
 * The mono shoulder-derived stable BodyFrame lives in pose below.
 * No robot installation inputs are needed by Agent1. */
typedef struct {
    PoseMappingContext pose; /* unchanged tracking/reconstruction/hand state */
    HumanForearmTarget last_target;
    float elbow_roll_unwrapped_deg, elbow_pitch_deg;
    float raw_elbow_roll_deg, raw_elbow_pitch_deg; /* before angle EMA */
    Point3D wrist_reference;
    uint8_t angle_valid, elbow_roll_initialized, elbow_roll_singular, last_target_valid;
    uint8_t stereo_input_active;
} ForearmMappingContext;

/* BodyFrame is estimated from the input shoulders during mono update. */
int forearm_mapping_init(ForearmMappingContext *ctx);
/* Same fresh=1 / HOLD or duplicate=0 / invalid=-1 convention as legacy API.
 * A side change resets history, including wrist roll zero calibration. */
int forearm_mapping_update(ForearmMappingContext *ctx, const HumanPose2D *pose,
                           PoseArmSide side, float dt_sec,
                           HumanForearmTarget *target);
int forearm_mapping_update_stereo(ForearmMappingContext *ctx, const HumanPose2D *image_pose,
                                  const HumanPose3D *measured_pose, PoseArmSide side,
                                  float dt_sec, HumanForearmTarget *target);

#ifdef __cplusplus
}
#endif
#endif
