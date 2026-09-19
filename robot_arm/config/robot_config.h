#ifndef ROBOT_CONFIG_H
#define ROBOT_CONFIG_H

/*
 * Agent1 설정
 * - CNN 입력: 약 10~20 Hz
 * - Z는 절대 거리(mm/m)가 아니라 Shoulder Width = 1.0 기준 상대 깊이
 * - Robot joint limit / angular rate / PWM은 Agent2/3 담당
 */

/*
 * Camera: 최종 PoseNet 입력 해상도 1280x720.
 *
 * FX/FY=1108은 기존 640x480 bring-up 값(554px)을
 * 2배 pixel density로 옮긴 임시값이다.
 * 최종 Pcam intrinsic calibration을 수행하면 FX/FY/CX/CY만
 * 실제 calibration 값으로 교체하는 것을 권장한다.
 */
#define PM_CAMERA_WIDTH                         1280.0f
#define PM_CAMERA_HEIGHT                        720.0f
#define PM_CAMERA_FX                            1108.0f
#define PM_CAMERA_FY                            1108.0f
#define PM_CAMERA_CX                            639.5f
#define PM_CAMERA_CY                            359.5f

/* dt_sec가 유효하지 않을 때만 사용하는 기본 주기 */
#define PM_DEFAULT_FPS                          15.0f
#define PM_FILTER_DT_MIN_SEC                    0.020f
#define PM_FILTER_DT_MAX_SEC                    0.150f

/* 사람 상대 링크 길이: Shoulder Width = 1.0 */
#define PM_SHOULDER_WIDTH_UNIT                  1.0f
#define PM_UPPER_ARM_RATIO                      0.75f
#define PM_FOREARM_RATIO                        0.65f
#define PM_WRIST_TO_FINGER1_RATIO               0.35f
#define PM_WRIST_TO_FINGER2_RATIO               0.35f

/* Major pose 계산 실패/누락 시 마지막 정상 target 유지 시간 */
#define PM_TARGET_HOLD_SEC                      0.35f

/* Time-based EMA */
#define PM_INPUT_2D_TAU_SEC                     0.10f
#define PM_POSITION_XY_TAU_SEC                  0.12f
#define PM_POSITION_Z_TAU_SEC                   0.28f
#define PM_HAND_NORMAL_TAU_SEC                  0.22f
#define PM_BODY_FRAME_TAU_SEC                   0.20f
#define PM_JOINT_ANGLE_TAU_SEC                  0.12f
#define PM_ROLL_ANGLE_TAU_SEC                   0.25f

/*
 * 측면 자세 Body frame 안정화.
 * Shoulder 화면 간격이 작거나 3D shoulder axis가 한 frame에 크게 바뀌면
 * 해당 frame의 body-axis 반영량만 줄인다. 완전히 reject하지 않으므로
 * 예전 outlier lock-out처럼 영구 정지하지 않는다.
 */
#define PM_BODY_FRAME_LOW_CONF_SPAN_PX          50.0f
#define PM_BODY_FRAME_LOW_CONF_SCALE            0.25f
#define PM_BODY_FRAME_LARGE_JUMP_DEG            45.0f
#define PM_BODY_FRAME_LARGE_JUMP_SCALE          0.25f

/*
 * 3D reconstruction / gripper reference
 * - 새 reconstruction에서는 Shoulder 폭을 depth의 hard constraint로 쓰지 않는다.
 * - 이 값은 shoulder 폭이 너무 작아졌을 때 gripper 2D 정규화 reference의
 *   최소값으로만 사용한다.
 */
#define PM_MIN_SHOULDER_WIDTH_PX                80.0f
#define PM_MIN_BODY_DEPTH_UNIT                  0.5f
#define PM_MAX_BODY_DEPTH_UNIT                  20.0f

/*
 * Ray-Sphere가 정확히 만나지 않을 때 허용하는 soft link 오차.
 * 예: 0.20 = ray와 parent의 최소거리가 링크 길이보다 최대 20% 큰 경우까지만
 * closest-point fallback 허용.
 *
 * Major chain에서는 active shoulder depth를 탐색해서 exact/soft link residual이
 * 가장 작고 이전 frame과 연속적인 3D chain을 선택한다.
 */
#define PM_RAY_SOFT_LINK_TOLERANCE_RATIO        0.20f

/* Wrist/hand geometry 유효성 */
#define PM_MIN_HAND_PLANE_QUALITY               0.15f
#define PM_MIN_REFERENCE_QUALITY                0.15f

/* 사람 관절 추정값 노이즈 제거용. Robot rate limit이 아님. */
#define PM_JOINT_DEADBAND_DEG                   0.5f
#define PM_ROLL_DEADBAND_DEG                    1.0f
#define PM_ROLL_SPIKE_MARGIN_DEG                35.0f

/* Gripper 의도: 0.0=CLOSE, 1.0=OPEN */
#define PM_GRIPPER_OPEN_RATIO                   0.10f
#define PM_GRIPPER_CLOSE_RATIO                  0.08f

/* Human wrist roll zero calibration */
#define PM_ROLL_ZERO_CALIB_SEC                  0.80f
#define PM_ROLL_ZERO_MIN_SAMPLES                5U

#endif /* ROBOT_CONFIG_H */
