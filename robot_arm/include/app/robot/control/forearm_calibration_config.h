#ifndef ROBOT_CALIBRATION_FOREARM_CALIBRATION_CONFIG_H
#define ROBOT_CALIBRATION_FOREARM_CALIBRATION_CONFIG_H

/* Servo command = human angle * scale * direction + zero_offset_deg.
 * This input mapping does not define the physical axes used by FK. */
typedef struct {
    float scale; // 사람 관절 움직임의 크기 조정
    int direction; // 회전 방향 반전, 1 또는 -1
    float zero_offset_deg; // 로봇 관절의 중립 위치 보정
    float min_deg; // 로봇 관절 최소 허용각
    float max_deg; // 로봇 관절 최대 허용각
    float max_delta_deg; // 로봇 관절 최대 변화각
} JointCalibration;

/* Active five-servo mechanism: elbow_roll, elbow_pitch, wrist_pitch,
 * wrist_roll, gripper. Gripper is passed through; the four rotation joints
 * use JointCalibration defined above.
 * See forearm_calibration_config.c for measured and provisional settings. */
typedef struct {
    JointCalibration elbow_roll;   /* M0: 팔꿈치 방위각(구 base 역할) */
    JointCalibration elbow_pitch;  /* M1: 팔꿈치 고도각(구 shoulder 역할) */
    JointCalibration wrist_pitch;  /* M2 */
    JointCalibration wrist_roll;   /* M3 */
    /* Per-axis acceleration limits (deg/s^2), in the order above. */
    float amax_deg_s2[4];
} ForearmCalibrationConfig;

extern const ForearmCalibrationConfig forearm_calibration_config;

#endif /* ROBOT_CALIBRATION_FOREARM_CALIBRATION_CONFIG_H */
