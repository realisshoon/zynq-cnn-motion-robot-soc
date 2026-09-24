#ifndef POSE_MAPPING_H
#define POSE_MAPPING_H

#include <stdint.h>
#include "common/robot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    POSE_ARM_LEFT = 0,
    POSE_ARM_RIGHT = 1
} PoseArmSide;

/*
 * Landmark tracking은 의도적으로 단순하게 유지한다.
 * value       : time-based EMA가 적용된 최신 2D 좌표
 * initialized : 한 번이라도 정상 좌표를 받은 적이 있는지
 * fresh       : 이번 CNN frame에서 이 landmark가 실제로 들어왔는지
 *
 * 이전 버전의 landmark별 age/outlier reject는 실제 영상에서 lock-out을
 * 만들 가능성이 있어 최종 baseline에서는 제거했다.
 */
typedef struct {
    Point2D value;
    uint8_t initialized;
    uint8_t fresh;
} PoseLandmarkState;

typedef struct {
    PoseLandmarkState shoulder_l;
    PoseLandmarkState shoulder_r;
    PoseLandmarkState elbow;
    PoseLandmarkState wrist;
    PoseLandmarkState finger1;
    PoseLandmarkState finger2;

    uint32_t last_frame_id;
    uint8_t last_frame_id_valid;

    /* 마지막 정상 target 갱신 이후 시간 */
    float target_age_sec;

    /* 내부 relative 3D pose */
    Point3D shoulder_l_3d;
    Point3D shoulder_r_3d;
    Point3D elbow_3d;
    Point3D wrist_3d;
    Point3D finger1_3d;
    Point3D finger2_3d;
    uint8_t major_pose3d_valid;
    uint8_t finger_pose3d_valid;
    /* Wrist at the last successful finger reconstruction. Finger continuity
     * and smoothing use offsets from this parent, not absolute camera depth. */
    Point3D finger_parent_wrist;

    /*
     * 안정화된 Human Body Coordinate.
     * X = anatomical left shoulder -> right shoulder (time-filtered).
     * Y = camera up projected perpendicular to X; Z = X cross Y.
     * Z is NOT forced toward camera +Z. See docs/coordinate_system.md.
     * 측면 자세에서 한쪽 Shoulder landmark가 흔들려도 Base/Roll 기준축이
     * 한 frame 만에 크게 뒤집히지 않도록 3D shoulder axis를 시간 필터링한다.
     */
    Point3D body_x_axis;
    Point3D body_y_axis;
    Point3D body_z_axis;
    uint8_t body_frame_valid;

    /*
     * 카메라 roll의 프레임 단위 추정치(도). PM_CAMERA_ROLL_ADAPT_* 참고.
     * valid=0이면 아직 시딩 전 — 다음 성공 frame에서 PM_CAMERA_ROLL_DEG로 시딩된다.
     */
    float camera_roll_estimate_deg;
    uint8_t camera_roll_estimate_valid;

    /* Wrist roll */
    Point3D prev_hand_normal;
    uint8_t prev_hand_normal_valid;
    float prev_roll_raw_unwrapped_deg;
    uint8_t prev_roll_raw_valid;
    float prev_pitch_raw_unwrapped_deg;
    uint8_t prev_pitch_raw_valid;

    /* Human wrist roll zero calibration */
    uint8_t roll_zero_calibrating;
    float roll_zero_elapsed_sec;
    uint32_t roll_zero_sample_count;
    float roll_zero_sin_sum;
    float roll_zero_cos_sum;
    float roll_zero_offset_deg;
    uint8_t roll_zero_calibrated;

    /* 사람 관절각 filtering 상태 */
    float prev_base_deg;
    float prev_shoulder_deg;
    float prev_elbow_deg;
    float prev_wrist_pitch_deg;
    float prev_wrist_roll_deg;
    uint8_t major_angle_valid;
    uint8_t hand_angle_valid;

    /* 0=CLOSE, 1=OPEN 의도 */
    uint8_t gripper_state;
    uint8_t gripper_initialized;

    HumanJointTarget last_target;
    uint8_t last_target_valid;

    PoseArmSide last_arm_side;
    uint8_t last_arm_side_valid;
    uint8_t initialized;
} PoseMappingContext;

int pose_mapping_init(PoseMappingContext *ctx);
void pose_mapping_reset(PoseMappingContext *ctx);

int pose_mapping_start_roll_zero_calibration(PoseMappingContext *ctx);
uint8_t pose_mapping_is_roll_zero_calibrating(const PoseMappingContext *ctx);
void pose_mapping_clear_roll_zero(PoseMappingContext *ctx);

/*
 * return:
 *   1  새 CNN frame으로 target 갱신
 *   0  짧은 dropout/reconstruction 실패로 마지막 정상 target HOLD
 *  -1  사용할 target 없음(target.valid=0)
 */
int pose_mapping_update(
    PoseMappingContext *ctx,
    const HumanPose2D *pose,
    PoseArmSide active_arm,
    float dt_sec,
    HumanJointTarget *target
);

#ifdef __cplusplus
}
#endif

#endif /* POSE_MAPPING_H */
