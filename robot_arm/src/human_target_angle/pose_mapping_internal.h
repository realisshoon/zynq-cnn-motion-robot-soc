#ifndef POSE_MAPPING_INTERNAL_H
#define POSE_MAPPING_INTERNAL_H

#include <stdint.h>

#include "human_target_angle/pose_mapping.h"
#include "robot_config.h"

#define PM_PI_F        3.14159265358979323846f
#define PM_RAD_TO_DEG  (180.0f / PM_PI_F)
#define PM_DEG_TO_RAD  (PM_PI_F / 180.0f)
#define PM_EPS         1.0e-6f

typedef Point3D Vec3;

/* pose_math.c */
float pm_clampf(float v, float lo, float hi);
float pm_sqrf(float v);
float pm_alpha_from_tau(float dt_sec, float tau_sec);
float pm_sanitize_filter_dt(float dt_sec);
Vec3 pm_vec3(float x, float y, float z);
Vec3 pm_vadd(Vec3 a, Vec3 b);
Vec3 pm_vsub(Vec3 a, Vec3 b);
Vec3 pm_vscale(Vec3 a, float s);
float pm_vdot(Vec3 a, Vec3 b);
Vec3 pm_vcross(Vec3 a, Vec3 b);
float pm_vlen(Vec3 a);
int pm_vnormalize(Vec3 *v);
Vec3 pm_project_perpendicular(Vec3 v, Vec3 axis);
float pm_wrap180(float deg);
float pm_unwrap_near(float deg, float reference);
float pm_distance_2d(Point2D a, Point2D b);
int pm_build_body_frame(Point3D shoulder_l, Point3D shoulder_r,
                        Vec3 *body_x, Vec3 *body_y, Vec3 *body_z);
int pm_update_stable_body_frame(PoseMappingContext *ctx, float dt_filter_sec);
int pm_get_stable_body_frame(const PoseMappingContext *ctx,
                             Vec3 *body_x, Vec3 *body_y, Vec3 *body_z);
float pm_filter_angle_continuous(float prev, float current, float tau_sec,
                                 float deadband_deg, float dt_filter_sec,
                                 uint8_t use_unwrap);

/* pose_tracking.c */
void pm_update_all_landmarks(PoseMappingContext *ctx,
                             const HumanPose2D *pose,
                             float dt_filter_sec);
uint8_t pm_major_all_fresh(const PoseMappingContext *ctx);
uint8_t pm_fingers_both_fresh(const PoseMappingContext *ctx);

/* pose_reconstruction.c */
int pm_reconstruct_major_pose3d(PoseMappingContext *ctx,
                                PoseArmSide active_arm,
                                float dt_filter_sec,
                                float *shoulder_span_px_out);
int pm_reconstruct_finger_pose3d(PoseMappingContext *ctx,
                                 float dt_filter_sec);

/* pose_joint.c */
int pm_calculate_major_angles(PoseMappingContext *ctx,
                              PoseArmSide active_arm,
                              float dt_filter_sec,
                              HumanJointTarget *out);

/* pose_hand.c */
int pm_calculate_hand_angles_and_gripper(PoseMappingContext *ctx,
                                         float shoulder_span_px,
                                         float dt_age_sec,
                                         float dt_filter_sec,
                                         HumanJointTarget *out);

#endif /* POSE_MAPPING_INTERNAL_H */
