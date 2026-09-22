#ifndef FOREARM_MAPPING_INTERNAL_H
#define FOREARM_MAPPING_INTERNAL_H
#include "human_target_angle/forearm_mapping.h"
#include "pose_mapping_internal.h"

/* Geometric observability thresholds, not additional temporal smoothing.
 * sin(angle from pole): enter ~1.15 deg, leave ~2.29 deg. */
#define FM_AZIMUTH_ENTER 0.02f
#define FM_AZIMUTH_LEAVE 0.04f
float fm_wrap180(float degrees);
/* Uses the stable BodyFrame already updated by the public pipeline.
 * Separating projection allows rigid rotation tests of supplied frames. */
int fm_calculate_angles(ForearmMappingContext *ctx, float filter_dt,
                        HumanForearmTarget *out);
int fm_calculate_hand(ForearmMappingContext *ctx, float shoulder_span,
                     float age_dt, float filter_dt, HumanForearmTarget *out);
#endif
