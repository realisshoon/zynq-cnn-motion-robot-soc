#ifndef ROBOT_CALIBRATION_FOREARM_CALIBRATION_CONFIG_H
#define ROBOT_CALIBRATION_FOREARM_CALIBRATION_CONFIG_H

#include "robot_calibration/robot_calibration_config.h"

/*
 * 새 5축(팔꿈치부터 시작하는 수평 설치) 서보 보정.
 *
 * 기존 base/shoulder/elbow/wrist_pitch/wrist_roll 6축 구조가 아니라,
 * "팔꿈치 관절이 회전+굽힘 2자유도(M0 elbow_roll, M1 elbow_pitch)를 가지고
 * 그 다음 wrist_pitch(M2), wrist_roll(M3), gripper(M4)로 이어지는" 새 물리
 * 구조용이다. JointCalibration 자체는 기존 robot_calibration_config.h의
 * 것을 그대로 재사용한다(scale/direction/zero_offset_deg/min_deg/max_deg/
 * max_delta_deg, 의미 동일). gripper는 legacy와 마찬가지로 A1이 0/1로 양자화해
 * 그대로 전달하므로 여기 보정 대상에 없다.
 *
 * *** 아래 forearm_calibration_config.c의 값은 전부 미실측 임시값이다.
 * 실물 서보 방향/범위를 조그(jog) 테스트로 확인하기 전에는 이 값으로
 * 실제 서보를 구동하지 말 것. *** 자세한 목록은 .c 파일 주석 참고. ***
 */
typedef struct {
    JointCalibration elbow_roll;   /* M0: 팔꿈치 방위각(구 base 역할) */
    JointCalibration elbow_pitch;  /* M1: 팔꿈치 고도각(구 shoulder 역할) */
    JointCalibration wrist_pitch;  /* M2 */
    JointCalibration wrist_roll;   /* M3 */
    /* motion.c(Motion)의 가속도 제한(deg/s^2). 순서는 위와 동일(elbow_roll,
     * elbow_pitch, wrist_pitch, wrist_roll). JointCalibration에는 안 넣었다 --
     * 그 구조체는 legacy 6축과 공유하는 타입이라 가속도 제한이 없는 legacy
     * 경로(motion_limits.c/motion_smoothing.c)에 불필요한 필드를 얹고 싶지
     * 않았다. 값 자체는 다른 필드들과 마찬가지로 미실측 임시값이다. */
    float amax_deg_s2[4];
} ForearmCalibrationConfig;

extern const ForearmCalibrationConfig forearm_calibration_config;

#endif /* ROBOT_CALIBRATION_FOREARM_CALIBRATION_CONFIG_H */
