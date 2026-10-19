#include <stdint.h>
#include <string.h>

#include "forearm_mapping_internal.h"
#include "human_target_angle/forearm_mapping.h"

#include "robot_calibration/forearm_calibration.h"
#include "robot_calibration/forearm_motion_control.h"
#include "robot_calibration/forearm_safety_check.h"

#ifdef _WIN32
#define BRIDGE_API __declspec(dllexport)
#else
#define BRIDGE_API
#endif

/*
 * Webcam LivePose adapter
 *
 * IMPORTANT
 * ---------
 * M0/M1 geometry is NOT reimplemented here.
 *
 * Actual Git functions are called:
 *
 *   pm_update_all_landmarks()
 *   pm_reconstruct_major_pose3d()
 *   pm_update_stable_body_frame()
 *   fm_calculate_angles()
 *   forearm_motion_control_unwrap_target()
 *   forearm_motion_control_map_target()
 *   forearm_motion_control_apply_limits()
 *   forearm_safety_check_apply()
 *   forearm_calibration_set_target()
 *   forearm_calibration_step()
 *
 * Webcam MoveNet has no finger landmarks.
 * Therefore M2/M3/M4 are explicitly held at the Git agent_pipeline HOME pose.
 */

typedef struct {
  ForearmMappingContext map;
  ForearmAngleUnwrapState unwrap;
  ForearmMotionState motion;

  ForearmJointCommand target;
  ForearmJointCommand output;

  uint8_t initialized;
  uint8_t target_valid;
} RobotGitSideContext;

static RobotGitSideContext g_side[2];

/*
 * Same HOME command currently used by:
 *
 * robot_arm/src/integration/agent_pipeline.c
 */
static const ForearmJointCommand k_home_pose = {.elbow_roll_deg = 90.0f,
                                                .elbow_pitch_deg = 70.0f,
                                                .wrist_pitch_deg = 100.0f,
                                                .wrist_roll_deg = 90.0f,
                                                .gripper_norm = 0.05f,
                                                .valid = 1U};

static void write_command(const ForearmJointCommand *command, float *out) {
  if (command == NULL || out == NULL)
    return;

  out[0] = command->elbow_roll_deg;
  out[1] = command->elbow_pitch_deg;
  out[2] = command->wrist_pitch_deg;
  out[3] = command->wrist_roll_deg;
  out[4] = command->gripper_norm;
}

static int init_side(int side) {
  RobotGitSideContext *s;

  if (side < 0 || side > 1)
    return -1;

  s = &g_side[side];

  memset(s, 0, sizeof(*s));

  /*
   * Actual Git Agent1 context initialization.
   */
  if (forearm_mapping_init(&s->map) != 0)
    return -1;

  /*
   * Actual Git Agent2 state initialization.
   */
  forearm_motion_control_unwrap_state_init(&s->unwrap);

  forearm_calibration_state_init(&s->motion);

  /*
   * Same bootstrap rule as agent_pipeline.c:
   * seed motion state from HOME.
   */
  forearm_calibration_set_target(&s->motion, &k_home_pose);

  forearm_calibration_step(&s->motion, &s->output);

  s->target = k_home_pose;
  s->target_valid = 1U;
  s->initialized = 1U;

  return 0;
}

BRIDGE_API int robot_git_reset(void) {
  if (init_side(0) != 0)
    return -1;

  if (init_side(1) != 0)
    return -1;

  return 0;
}

/*
 * side:
 *   0 = Human LEFT
 *   1 = Human RIGHT
 *
 * out_xyz[12]
 *   shoulder_l xyz
 *   shoulder_r xyz
 *   elbow xyz
 *   wrist xyz
 *
 * out_human[2]
 *   Git Agent1 elbow_roll
 *   Git Agent1 elbow_pitch
 *
 * out_target[5]
 *   Git Agent2 calibrated target
 *
 * return:
 *    1 = fresh target accepted
 *    0 = no fresh major reconstruction
 *   -1 = invalid argument / geometry
 *   -2 = Agent2 validation failure
 *   -3 = safety rejection
 */
BRIDGE_API int robot_git_update(
    int side, uint32_t frame_id, float dt_sec,

    float shoulder_l_x, float shoulder_l_y, uint8_t shoulder_l_valid,

    float shoulder_r_x, float shoulder_r_y, uint8_t shoulder_r_valid,

    float elbow_x, float elbow_y, uint8_t elbow_valid,

    float wrist_x, float wrist_y, uint8_t wrist_valid,

    float *out_xyz, float *out_human, float *out_target,
    uint8_t *out_roll_observable, uint32_t *out_safety_flags) {
  RobotGitSideContext *s;
  PoseMappingContext *p;

  HumanPose2D pose;
  HumanForearmTarget human;

  ForearmJointCommand target;

  ForearmSafetyCheckFlags safety_flags = FOREARM_SAFETY_CHECK_OK;

  float dt_filter;
  float shoulder_span = 0.0f;

  float agent1_roll;
  float agent1_pitch;

  if (side < 0 || side > 1 || out_xyz == NULL || out_human == NULL ||
      out_target == NULL || out_roll_observable == NULL ||
      out_safety_flags == NULL)
    return -1;

  if (!g_side[side].initialized) {
    if (init_side(side) != 0)
      return -1;
  }

  s = &g_side[side];
  p = &s->map.pose;

  memset(&pose, 0, sizeof(pose));

  pose.frame_id = frame_id;
  pose.valid = 1U;

  pose.shoulder_l.x = shoulder_l_x;
  pose.shoulder_l.y = shoulder_l_y;
  pose.shoulder_l.valid = shoulder_l_valid;

  pose.shoulder_r.x = shoulder_r_x;
  pose.shoulder_r.y = shoulder_r_y;
  pose.shoulder_r.valid = shoulder_r_valid;

  pose.elbow.x = elbow_x;
  pose.elbow.y = elbow_y;
  pose.elbow.valid = elbow_valid;

  pose.wrist.x = wrist_x;
  pose.wrist.y = wrist_y;
  pose.wrist.valid = wrist_valid;

  /*
   * Webcam mock has no finger detector.
   * Do NOT fabricate measured finger coordinates.
   */
  pose.finger1.valid = 0U;
  pose.finger2.valid = 0U;

  dt_filter = pm_sanitize_filter_dt(dt_sec);

  /*
   * ===== Git Agent1 major-pose path =====
   */
  pm_update_all_landmarks(p, &pose, dt_filter);

  if (!pm_major_all_fresh(p))
    return 0;

  if (pm_reconstruct_major_pose3d(p, side == 0 ? POSE_ARM_LEFT : POSE_ARM_RIGHT,
                                  dt_filter, &shoulder_span) != 0)
    return 0;

  if (pm_update_stable_body_frame(p, dt_filter) != 0)
    return -1;

  /*
   * THIS IS THE ACTUAL Git forearm_mapping.c FUNCTION.
   *
   * No atan2 / BodyFrame math is duplicated in this bridge.
   */
  memset(&human, 0, sizeof(human));

  if (fm_calculate_angles(&s->map, dt_filter, &human) != 0)
    return -1;

  human.frame_id = frame_id;
  human.valid = 1U;

  /*
   * No measured hand data.
   *
   * These values are not treated as measurements.
   * Robot M2/M3/M4 are replaced with k_home_pose below.
   */
  human.wrist_pitch_deg = 0.0f;
  human.wrist_roll_deg = 0.0f;
  human.gripper_norm = k_home_pose.gripper_norm;
  human.hand_fresh = 0U;

  agent1_roll = human.elbow_roll_deg;
  agent1_pitch = human.elbow_pitch_deg;

  /*
   * ===== Git Agent2 contract =====
   */
  if (!forearm_motion_control_validate_target(&human))
    return -2;

  forearm_motion_control_unwrap_target(&s->unwrap, &human);

  forearm_motion_control_map_target(&human, &target);

  /*
   * Fingerless mock:
   *
   * M0/M1 = actual Git mapping
   * M2/M3/M4 = actual Git HOME robot command
   */
  target.wrist_pitch_deg = k_home_pose.wrist_pitch_deg;

  target.wrist_roll_deg = k_home_pose.wrist_roll_deg;

  target.gripper_norm = k_home_pose.gripper_norm;

  target.valid = 1U;

  forearm_motion_control_apply_limits(&target);

  if (!forearm_safety_check_apply(&target, &safety_flags)) {
    *out_safety_flags = (uint32_t)safety_flags;

    write_command(&target, out_target);

    return -3;
  }

  /*
   * Same Agent2 motion target mechanism used by Git pipeline.
   *
   * robot_git_tick() performs the fixed 20ms ramp step.
   */
  forearm_calibration_set_target(&s->motion, &target);

  s->target = target;
  s->target_valid = 1U;

  /*
   * Debug / telemetry outputs.
   */
  out_xyz[0] = p->shoulder_l_3d.x;
  out_xyz[1] = p->shoulder_l_3d.y;
  out_xyz[2] = p->shoulder_l_3d.z;

  out_xyz[3] = p->shoulder_r_3d.x;
  out_xyz[4] = p->shoulder_r_3d.y;
  out_xyz[5] = p->shoulder_r_3d.z;

  out_xyz[6] = p->elbow_3d.x;
  out_xyz[7] = p->elbow_3d.y;
  out_xyz[8] = p->elbow_3d.z;

  out_xyz[9] = p->wrist_3d.x;
  out_xyz[10] = p->wrist_3d.y;
  out_xyz[11] = p->wrist_3d.z;

  out_human[0] = agent1_roll;
  out_human[1] = agent1_pitch;

  *out_roll_observable = human.elbow_roll_observable;

  *out_safety_flags = FOREARM_SAFETY_CHECK_OK;

  write_command(&target, out_target);

  return 1;
}

/*
 * Actual Git Agent2 fixed-control-tick path.
 *
 * Call every 20 ms.
 *
 * out_command:
 *   [0] M0 elbow roll
 *   [1] M1 elbow pitch
 *   [2] M2 wrist pitch
 *   [3] M3 wrist roll
 *   [4] M4 gripper
 */
BRIDGE_API int robot_git_tick(int side, float *out_command) {
  RobotGitSideContext *s;

  if (side < 0 || side > 1 || out_command == NULL)
    return -1;

  if (!g_side[side].initialized) {
    if (init_side(side) != 0)
      return -1;
  }

  s = &g_side[side];

  forearm_calibration_step(&s->motion, &s->output);

  if (!s->output.valid)
    return 0;

  write_command(&s->output, out_command);

  return 1;
}
