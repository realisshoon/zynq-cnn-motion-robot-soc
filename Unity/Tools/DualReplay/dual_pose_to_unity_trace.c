
/*
 * dual_pose_to_unity_trace.c
 * Five-axis forearm software replay. No physical or PWM output.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/robot_types.h"
#include "human_target_angle/forearm_mapping.h"
#include "robot_calibration/forearm_calibration.h"

#define TICK_SEC 0.020f
#define HOME_DEG 90.0f
#define MAX_LINE 8192
#define MAX_FIELDS 64

typedef struct {
  ForearmMappingContext pose_ctx;
  ForearmAngleUnwrapState unwrap;
  ForearmMotionState motion;
  ForearmJointCommand last_command;
  uint8_t command_valid;
  ForearmJointCommand output;
  uint32_t accepted;
  uint32_t held;
  uint32_t invalid;
  uint32_t rejected_validate;
  uint32_t rejected_safety;
  uint32_t reject_invalid_command;
  uint32_t reject_self_collision;
  uint32_t reject_table_collision;
  uint32_t blocked_ticks;
  uint32_t blocked_self_collision;
  uint32_t blocked_table_collision;
} ArmPreview;

typedef struct {
  uint32_t frame_id;
  float time_sec;
  uint8_t frame_valid;
  Point2D shoulder_l, shoulder_r;
  Point2D elbow_l, wrist_l;
  Point2D elbow_r, wrist_r;
  Point2D finger1_l, finger2_l;
  Point2D finger1_r, finger2_r;
} DualPoseRow;

static int same_command(const ForearmJointCommand *a, const ForearmJointCommand *b) {
  return a->elbow_roll_deg == b->elbow_roll_deg &&
         a->elbow_pitch_deg == b->elbow_pitch_deg &&
         a->wrist_pitch_deg == b->wrist_pitch_deg &&
         a->wrist_roll_deg == b->wrist_roll_deg &&
         a->gripper_norm == b->gripper_norm && a->valid == b->valid;
}

static int init_preview(ArmPreview *a) {
  ForearmJointCommand home;
  memset(a, 0, sizeof(*a));
  if (forearm_mapping_init(&a->pose_ctx) != 0) return 0;
  forearm_motion_control_unwrap_state_init(&a->unwrap);
  forearm_calibration_state_init(&a->motion);

  memset(&home, 0, sizeof(home));
  home.elbow_roll_deg = HOME_DEG;
  home.elbow_pitch_deg = HOME_DEG;
  home.wrist_pitch_deg = HOME_DEG;
  home.wrist_roll_deg = HOME_DEG;
  home.gripper_norm = 0.5f;
  home.valid = 1U;

  if (!forearm_safety_check_apply(&home, NULL)) return 0;

  forearm_calibration_set_target(&a->motion, &home);
  forearm_calibration_step(&a->motion, &a->output);
  a->last_command = home;
  a->command_valid = 1U;
  return 1;
}

static HumanPose2D make_pose(const DualPoseRow *r, PoseArmSide side) {
  HumanPose2D p;
  memset(&p, 0, sizeof(p));
  p.shoulder_l = r->shoulder_l;
  p.shoulder_r = r->shoulder_r;
  p.frame_id = r->frame_id;
  p.valid = r->frame_valid;

  if (side == POSE_ARM_LEFT) {
    p.elbow = r->elbow_l;
    p.wrist = r->wrist_l;
    p.finger1 = r->finger1_l;
    p.finger2 = r->finger2_l;
  } else {
    p.elbow = r->elbow_r;
    p.wrist = r->wrist_r;
    p.finger1 = r->finger1_r;
    p.finger2 = r->finger2_r;
  }
  return p;
}

static void update_arm(ArmPreview *a, const HumanPose2D *pose, PoseArmSide side,
                       float dt_sec) {
  HumanForearmTarget target;
  ForearmJointCommand command;
  ForearmSafetyCheckFlags issues;
  int rc;

  memset(&target, 0, sizeof(target));
  rc = forearm_mapping_update(&a->pose_ctx, pose, side, dt_sec, &target);

  if (!target.valid) {
    a->invalid++;
    return;
  }

  if (rc == 0) a->held++;

  if (!forearm_motion_control_validate_target(&target)) {
    a->rejected_validate++;
    return;
  }

  forearm_motion_control_unwrap_target(&a->unwrap, &target);
  memset(&command, 0, sizeof(command));
  if (!forearm_calibration_apply(&target, &command)) {
    /* apply leaves mapped angles available but clears valid on safety rejection. */
    command.valid = 1U;
    issues = FOREARM_SAFETY_CHECK_OK;
    forearm_safety_check_apply(&command, &issues);
    a->rejected_safety++;
    if (issues & FOREARM_SAFETY_CHECK_INVALID_COMMAND) a->reject_invalid_command++;
    if (issues & FOREARM_SAFETY_CHECK_SELF_COLLISION) a->reject_self_collision++;
    if (issues & FOREARM_SAFETY_CHECK_TABLE_COLLISION) a->reject_table_collision++;
    return; /* previous approved target stays active */
  }

  if (!a->command_valid || !same_command(&command, &a->last_command)) {
    forearm_calibration_set_target(&a->motion, &command);
    a->last_command = command;
    a->command_valid = 1U;
  }

  a->accepted++;
}

static void tick_arm(ArmPreview *a) {
  forearm_calibration_step(&a->motion, &a->output);
  if (a->motion.blocked_flags != FOREARM_SAFETY_CHECK_OK) {
    a->blocked_ticks++;
    if (a->motion.blocked_flags & FOREARM_SAFETY_CHECK_SELF_COLLISION) a->blocked_self_collision++;
    if (a->motion.blocked_flags & FOREARM_SAFETY_CHECK_TABLE_COLLISION) a->blocked_table_collision++;
  }
}

/* Portable CSV tokenizer for GCC/MinGW. */
static int parse_numbers(char *line, double fields[], int max_fields) {
  int n = 0;
  char *tok = strtok(line, ",");

  while (tok != NULL && n < max_fields) {
    fields[n++] = strtod(tok, NULL);
    tok = strtok(NULL, ",");
  }

  return n;
}

static Point2D pt(double x, double y, double valid) {
  Point2D p;
  p.x = (float)x;
  p.y = (float)y;
  p.valid = (uint8_t)(valid != 0.0);
  return p;
}

static int row_from_fields(const double f[], int n, DualPoseRow *r) {
  if (n < 33 || r == NULL)
    return 0;

  memset(r, 0, sizeof(*r));
  r->frame_id = (uint32_t)f[0];
  r->time_sec = (float)f[1];
  r->frame_valid = (uint8_t)(f[2] != 0.0);

  r->shoulder_l = pt(f[3], f[4], f[5]);
  r->shoulder_r = pt(f[6], f[7], f[8]);

  r->elbow_l = pt(f[9], f[10], f[11]);
  r->wrist_l = pt(f[12], f[13], f[14]);

  r->elbow_r = pt(f[15], f[16], f[17]);
  r->wrist_r = pt(f[18], f[19], f[20]);

  r->finger1_l = pt(f[21], f[22], f[23]);
  r->finger2_l = pt(f[24], f[25], f[26]);

  r->finger1_r = pt(f[27], f[28], f[29]);
  r->finger2_r = pt(f[30], f[31], f[32]);

  return 1;
}

static void write_header(FILE *o) {
  fprintf(o, "tick_id,time_sec,source_frame_id,"
             "left_elbow_roll_deg,left_elbow_pitch_deg,left_wrist_pitch_"
             "deg,left_wrist_roll_deg,left_gripper_norm,left_valid,"
             "right_elbow_roll_deg,right_elbow_pitch_deg,right_wrist_"
             "pitch_deg,right_wrist_roll_deg,right_gripper_norm,right_valid\n");
}

static void write_tick(FILE *o, uint32_t tick_id, float time_sec,
                       uint32_t source_frame_id, const ForearmJointCommand *left_unity,
                       const ForearmJointCommand *right_unity) {
  fprintf(o,
          "%u,%.6f,%u,"
          "%.9g,%.9g,%.9g,%.9g,%.9g,%u,"
          "%.9g,%.9g,%.9g,%.9g,%.9g,%u\n",
          tick_id, time_sec, source_frame_id,

          left_unity->elbow_roll_deg, left_unity->elbow_pitch_deg,
          left_unity->wrist_pitch_deg, left_unity->wrist_roll_deg,
          left_unity->gripper_norm, (unsigned)left_unity->valid,

          right_unity->elbow_roll_deg, right_unity->elbow_pitch_deg,
          right_unity->wrist_pitch_deg,
          right_unity->wrist_roll_deg, right_unity->gripper_norm,
          (unsigned)right_unity->valid);
}

int main(int argc, char **argv) {
  FILE *in;
  FILE *out;
  char line[MAX_LINE];
  double fields[MAX_FIELDS];

  DualPoseRow *rows = NULL;
  size_t count = 0;
  size_t cap = 0;
  size_t i;

  ArmPreview human_left;
  ArmPreview human_right;

  uint32_t tick_id = 1;
  float tick_time = 0.0f;

  if (argc != 3) {
    fprintf(stderr, "usage: %s <dual_pose.csv> <dual_joint_trace.csv>\n",
            argv[0]);
    return 2;
  }

  in = fopen(argv[1], "rb");
  if (in == NULL) {
    fprintf(stderr, "cannot open input: %s\n", argv[1]);
    return 3;
  }

  /* Skip header. */
  if (fgets(line, sizeof(line), in) == NULL) {
    fclose(in);
    fprintf(stderr, "empty CSV\n");
    return 4;
  }

  while (fgets(line, sizeof(line), in) != NULL) {
    int n;
    DualPoseRow row;

    n = parse_numbers(line, fields, MAX_FIELDS);

    if (!row_from_fields(fields, n, &row)) {
      fprintf(stderr, "bad CSV row at index %zu (fields=%d, expected >=33)\n",
              count + 1, n);
      free(rows);
      fclose(in);
      return 5;
    }

    if (count == cap) {
      size_t new_cap = cap ? cap * 2 : 512;
      DualPoseRow *new_rows =
          (DualPoseRow *)realloc(rows, new_cap * sizeof(*rows));

      if (new_rows == NULL) {
        free(rows);
        fclose(in);
        fprintf(stderr, "out of memory\n");
        return 6;
      }

      rows = new_rows;
      cap = new_cap;
    }

    rows[count++] = row;
  }

  fclose(in);

  if (count == 0) {
    free(rows);
    fprintf(stderr, "no pose rows\n");
    return 7;
  }

  if (!init_preview(&human_left) || !init_preview(&human_right)) {
    free(rows);
    fprintf(stderr, "forearm home/init failed safety check\n");
    return 8;
  }

  out = fopen(argv[2], "wb");
  if (out == NULL) {
    free(rows);
    fprintf(stderr, "cannot open output: %s\n", argv[2]);
    return 9;
  }

  write_header(out);

  for (i = 0; i < count; ++i) {
    float dt = (i == 0) ? 0.05f : (rows[i].time_sec - rows[i - 1].time_sec);

    float next_source_time =
        (i + 1 < count) ? rows[i + 1].time_sec : (rows[i].time_sec + 0.05f);

    HumanPose2D pose_left = make_pose(&rows[i], POSE_ARM_LEFT);

    HumanPose2D pose_right = make_pose(&rows[i], POSE_ARM_RIGHT);

    if (dt <= 0.0f || dt > 1.0f)
      dt = 0.05f;

    update_arm(&human_left, &pose_left, POSE_ARM_LEFT, dt);

    update_arm(&human_right, &pose_right, POSE_ARM_RIGHT, dt);

    while (tick_time + 1.0e-6f < next_source_time) {
      tick_arm(&human_left);
      tick_arm(&human_right);

      /*
       * Camera faces the person:
       * Human RIGHT -> viewer LEFT -> Unity RobotArm_L
       * Human LEFT  -> viewer RIGHT -> Unity RobotArm_R
       */
      write_tick(out, tick_id, tick_time, rows[i].frame_id, &human_right.output,
                 &human_left.output);

      tick_id++;
      tick_time += TICK_SEC;
    }
  }

  fclose(out);

  printf("DUAL UNITY PREVIEW TRACE COMPLETE\n"
         "pose_rows=%zu\n"
         "ticks=%u\n"
         "human_left: accepted=%u hold=%u invalid=%u reject_validate=%u reject_safety=%u (invalid_command=%u self_collision=%u table_collision=%u) blocked_ticks=%u (self_collision=%u table_collision=%u)\n"
         "human_right: accepted=%u hold=%u invalid=%u reject_validate=%u reject_safety=%u (invalid_command=%u self_collision=%u table_collision=%u) blocked_ticks=%u (self_collision=%u table_collision=%u)\n"
         "mapping: Human RIGHT -> Unity LEFT, Human LEFT -> Unity RIGHT\n"
         "No physical/PWM output.\n",
         count, tick_id - 1, human_left.accepted, human_left.held,
         human_left.invalid, human_left.rejected_validate, human_left.rejected_safety,
         human_left.reject_invalid_command, human_left.reject_self_collision,
         human_left.reject_table_collision, human_left.blocked_ticks,
         human_left.blocked_self_collision, human_left.blocked_table_collision,
         human_right.accepted, human_right.held, human_right.invalid,
         human_right.rejected_validate, human_right.rejected_safety,
         human_right.reject_invalid_command, human_right.reject_self_collision,
         human_right.reject_table_collision, human_right.blocked_ticks,
         human_right.blocked_self_collision, human_right.blocked_table_collision);

  free(rows);
  return 0;
}
