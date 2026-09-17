#ifndef POSE_MAPPING_H
#define POSE_MAPPING_H

#include <stdint.h>

#include "common/robot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Agent1 전용 좌/우 팔 선택 타입.
 * 공통 robot_types.h는 수정하지 않기 위해 Agent1 헤더 안에서만 정의한다.
 */
typedef enum {
    POSE_ARM_LEFT = 0,
    POSE_ARM_RIGHT = 1
} PoseArmSide;

/*
 * ================================================================
 * Agent 1 : Human Pose Interpretation / Joint Target Generation
 * ================================================================
 *
 * 입력:
 *   - CNN의 HumanPose2D
 *   - 현재 제어 중인 팔(POSE_ARM_LEFT / POSE_ARM_RIGHT)
 *   - 현재 CNN frame과 이전 CNN frame 사이 실제 시간 dt_sec
 *
 * 출력:
 *   - HumanJointTarget
 *
 * Agent1이 하는 일:
 *   1) CNN landmark 누락 / outlier 처리
 *   2) 10~20 Hz의 불규칙한 입력 주기를 고려한 time-based filtering
 *   3) 양 어깨 기준 상대 3D 자세 복원
 *   4) 사람 Base / Shoulder / Elbow / Wrist Pitch / Wrist Roll 계산
 *   5) 사람 손가락 간 거리로 Gripper OPEN/CLOSE 의도 생성
 *
 * Agent1이 하지 않는 일:
 *   - G51 servo offset / 방향 반전
 *   - Robot joint limit / collision safety
 *   - Robot joint angular velocity / motion rate limit
 *   - Angle -> PWM
 *   - 압력센서를 이용한 실제 Gripper 접촉/힘 제어
 */

/* Landmark 하나의 filtering / dropout 상태 */
typedef struct {
    Point2D value;          /* 최근 filter된 2D 좌표 */
    float age_sec;          /* 마지막 정상 검출 이후 지난 시간 */
    uint8_t initialized;    /* 한 번이라도 정상 좌표를 받은 적이 있는지 */
    uint8_t fresh;          /* 현재 CNN frame에서 실제로 새 좌표를 받은 경우 1 */
} PoseLandmarkState;

typedef struct {
    /* ------------------------------------------------------------
     * 2D landmark tracking 상태
     * ------------------------------------------------------------ */
    PoseLandmarkState shoulder_l;
    PoseLandmarkState shoulder_r;
    PoseLandmarkState elbow;
    PoseLandmarkState wrist;
    PoseLandmarkState finger1;
    PoseLandmarkState finger2;

    /* 같은 CNN frame을 PS loop에서 반복 처리하지 않기 위한 상태 */
    uint32_t last_frame_id;
    uint8_t last_frame_id_valid;

    /* 마지막으로 정상 major pose를 갱신한 뒤 지난 시간 */
    float target_age_sec;

    /* ------------------------------------------------------------
     * 내부 3D 좌표
     * ------------------------------------------------------------ */
    Point3D shoulder_l_3d;
    Point3D shoulder_r_3d;
    Point3D elbow_3d;
    Point3D wrist_3d;
    Point3D finger1_3d;
    Point3D finger2_3d;

    uint8_t major_pose3d_valid;
    uint8_t finger_pose3d_valid;

    /* ------------------------------------------------------------
     * Wrist Roll 보정 상태
     * ------------------------------------------------------------ */
    Point3D prev_hand_normal;
    uint8_t prev_hand_normal_valid;

    float prev_roll_raw_unwrapped_deg;
    uint8_t prev_roll_raw_valid;

    float last_stable_roll_raw_deg;
    uint8_t last_stable_roll_valid;

    /* Wrist Roll 0점 calibration */
    uint8_t roll_zero_calibrating;
    float roll_zero_elapsed_sec;
    uint32_t roll_zero_sample_count;
    float roll_zero_sin_sum;
    float roll_zero_cos_sum;

    float roll_zero_offset_deg;
    uint8_t roll_zero_calibrated;

    /* ------------------------------------------------------------
     * Joint angle filtering 상태
     * ------------------------------------------------------------ */
    float prev_base_deg;
    float prev_shoulder_deg;
    float prev_elbow_deg;
    float prev_wrist_pitch_deg;
    float prev_wrist_roll_deg;

    uint8_t major_angle_valid;
    uint8_t hand_angle_valid;

    /* Gripper OPEN/CLOSE 의도 상태 */
    uint8_t gripper_state;
    uint8_t gripper_initialized;

    /* 마지막 정상 출력 */
    HumanJointTarget last_target;
    uint8_t last_target_valid;

    /* 좌/우 팔 변경 감지 */
    PoseArmSide last_arm_side;
    uint8_t last_arm_side_valid;

    uint8_t initialized;
} PoseMappingContext;

/* Agent1 전체 상태 초기화 */
int pose_mapping_init(PoseMappingContext *ctx);

/* Tracking / filtering / calibration 상태를 초기화 */
void pose_mapping_reset(PoseMappingContext *ctx);

/*
 * Wrist Roll 0점 calibration 시작.
 * 호출 후 PM_ROLL_ZERO_CALIB_SEC 동안 손목을 기준 자세로 유지한다.
 */
int pose_mapping_start_roll_zero_calibration(PoseMappingContext *ctx);

/* 현재 roll zero calibration 진행 중이면 1 */
uint8_t pose_mapping_is_roll_zero_calibrating(const PoseMappingContext *ctx);

/* 저장된 roll zero offset 제거 */
void pose_mapping_clear_roll_zero(PoseMappingContext *ctx);

/*
 * ================================================================
 * 메인 API
 * ================================================================
 *
 * dt_sec:
 *   - 이전 "새 CNN frame"과 현재 "새 CNN frame" 사이의 실제 시간.
 *   - CNN이 10~20 Hz라면 보통 0.05~0.10 sec 정도다.
 *   - 같은 frame_id가 반복 입력되면 dt_sec는 사용하지 않고 재계산하지 않는다.
 *
 * return:
 *   1  : 현재 새 CNN frame을 사용해 Target 갱신
 *   0  : 중복 frame 또는 짧은 누락으로 이전 Target 유지
 *  -1  : 안전하게 사용할 Target이 없음(target.valid = 0)
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
