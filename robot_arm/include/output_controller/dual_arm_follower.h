#ifndef OUTPUT_CONTROLLER_DUAL_ARM_FOLLOWER_H
#define OUTPUT_CONTROLLER_DUAL_ARM_FOLLOWER_H

#include "robot_calibration/forearm_calibration.h"

typedef enum {
    DUAL_ARM_OFF = 0,
    DUAL_ARM_TRACKING,
    DUAL_ARM_HOLDING,
    DUAL_ARM_REJOINING,
    DUAL_ARM_HAL_FAULT
} DualArmFollowerMode;

typedef struct {
    ForearmJointCommand applied;
    ForearmMotionState motion;
    uint32_t hold_ticks, tracking_ticks, rejoin_ticks;
    uint32_t safety_flags;
    uint8_t have_command, enabled, need_rejoin;
    DualArmFollowerMode mode;
} DualArmFollower;

void dual_arm_follower_init(DualArmFollower *state);
void dual_arm_follower_pause(DualArmFollower *state);
int dual_arm_follower_prepare(DualArmFollower *state,
    const ForearmJointCommand *right, int advance, ForearmJointCommand *left);
int dual_arm_left_is_safe(const ForearmJointCommand *command, uint32_t *issues);
const char *dual_arm_follower_mode_name(DualArmFollowerMode mode);

#endif
