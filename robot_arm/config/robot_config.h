#ifndef ROBOT_CONFIG_H
#define ROBOT_CONFIG_H

/*
 * ================================================================
 * Agent 1 : Human Target Angle / Pose Mapping 설정
 * ================================================================
 *
 * 이 파일에는 Agent1에서 자주 조정할 가능성이 있는 값을 모아 둔다.
 * pose_mapping.c의 알고리즘은 그대로 두고 이 값들만 바꿔서 튜닝하는 것이 목적이다.
 *
 * 중요:
 *   - CNN 좌표 입력 주기는 고정 30 Hz로 가정하지 않는다.
 *   - 실제 운용 범위는 약 10~20 Hz를 예상한다.
 *   - 필터는 frame 수가 아니라 dt_sec(실제 시간)를 기준으로 동작한다.
 *   - Gripper는 0/1 "의도"만 출력한다.
 *     실제 물체 접촉/압력 제어는 Agent3의 압력센서 feedback에서 처리한다.
 *
 * 좌표계:
 *   Camera image : +x = 오른쪽, +y = 아래
 *   Agent1 3D    : +X = 오른쪽, +Y = 위, +Z = 카메라에서 사람 방향
 */

/* ----------------------------------------------------------------
 * 카메라 기본값
 * ---------------------------------------------------------------- */
#ifndef PM_CAMERA_WIDTH
#define PM_CAMERA_WIDTH                         640.0f
#endif

#ifndef PM_CAMERA_HEIGHT
#define PM_CAMERA_HEIGHT                        480.0f
#endif

/*
 * 640x480 bring-up용 초기값.
 * 실제 Pcam intrinsic calibration 후 fx/fy/cx/cy를 교체하는 것을 권장한다.
 */
#ifndef PM_CAMERA_FX
#define PM_CAMERA_FX                            554.0f
#endif

#ifndef PM_CAMERA_FY
#define PM_CAMERA_FY                            554.0f
#endif

#ifndef PM_CAMERA_CX
#define PM_CAMERA_CX                            319.5f
#endif

#ifndef PM_CAMERA_CY
#define PM_CAMERA_CY                            239.5f
#endif

/* ----------------------------------------------------------------
 * CNN 입력 주기
 * ---------------------------------------------------------------- */
/* dt_sec가 들어오지 않았을 때만 사용하는 기본값: 15 Hz */
#ifndef PM_DEFAULT_FPS
#define PM_DEFAULT_FPS                          15.0f
#endif

/*
 * 필터 계산에 사용할 dt의 제한.
 * 실제 dropout 시간 누적에는 원래 dt를 사용하고,
 * EMA 계산에서만 비정상적으로 큰/작은 dt가 들어오는 것을 막는다.
 */
#ifndef PM_FILTER_DT_MIN_SEC
#define PM_FILTER_DT_MIN_SEC                    0.020f
#endif

#ifndef PM_FILTER_DT_MAX_SEC
#define PM_FILTER_DT_MAX_SEC                    0.150f
#endif

/* ----------------------------------------------------------------
 * 사람 상대 링크 길이
 * ----------------------------------------------------------------
 * 절대 mm가 아니라 Shoulder Width = 1.0 기준 상대 길이.
 * Joint-space 각도 계산이 목적이므로 절대 길이가 필수는 아니다.
 */
#ifndef PM_SHOULDER_WIDTH_UNIT
#define PM_SHOULDER_WIDTH_UNIT                  1.0f
#endif

#ifndef PM_UPPER_ARM_RATIO
#define PM_UPPER_ARM_RATIO                      0.75f

#endif

#ifndef PM_FOREARM_RATIO
#define PM_FOREARM_RATIO                        0.65f
#endif

#ifndef PM_WRIST_TO_FINGER1_RATIO
#define PM_WRIST_TO_FINGER1_RATIO               0.35f
#endif

#ifndef PM_WRIST_TO_FINGER2_RATIO
#define PM_WRIST_TO_FINGER2_RATIO               0.35f
#endif

/* ----------------------------------------------------------------
 * Landmark 누락 / Dropout 처리 시간
 * ----------------------------------------------------------------
 * CNN이 10 Hz면 1 frame = 약 100 ms, 20 Hz면 1 frame = 약 50 ms.
 * 그래서 frame 개수가 아니라 실제 시간(sec)으로 설정한다.
 */

/* Shoulder / Elbow / Wrist의 짧은 누락을 내부적으로 유지할 수 있는 시간 */
#ifndef PM_MAJOR_LANDMARK_HOLD_SEC
#define PM_MAJOR_LANDMARK_HOLD_SEC              0.18f
#endif

/* Finger1 / Finger2의 짧은 누락 허용 시간 */
#ifndef PM_FINGER_LANDMARK_HOLD_SEC
#define PM_FINGER_LANDMARK_HOLD_SEC             0.30f
#endif

/*
 * Shoulder/Elbow/Wrist 중 하나가 빠져 새 자세 계산을 못할 때
 * 마지막 정상 HumanJointTarget을 유지하는 최대 시간.
 */
#ifndef PM_TARGET_HOLD_SEC
#define PM_TARGET_HOLD_SEC                      0.35f
#endif

/*
 * Finger가 이 시간보다 오래 안 잡히면 전체 Target을 invalid 처리한다.
 * 손목/집게 명령이 지나치게 오래 stale 상태로 남는 것을 방지한다.
 */
#ifndef PM_FINGER_LOSS_STOP_SEC
#define PM_FINGER_LOSS_STOP_SEC                 0.60f
#endif

/* ----------------------------------------------------------------
 * Landmark Outlier Rejection
 * ----------------------------------------------------------------
 * 한 frame 사이 landmark가 비정상적으로 크게 튀는 경우를 제거한다.
 * 속도 기준은 "어깨 폭 / sec" 단위다.
 * 예: 6.0이면 1초 동안 어깨 폭의 6배보다 빠르게 이동하는 점은 outlier 후보.
 */
#ifndef PM_ENABLE_LANDMARK_OUTLIER_REJECTION
#define PM_ENABLE_LANDMARK_OUTLIER_REJECTION    1
#endif

#ifndef PM_MAX_LANDMARK_SPEED_SHOULDER_PER_SEC
#define PM_MAX_LANDMARK_SPEED_SHOULDER_PER_SEC  6.0f
#endif

/* ----------------------------------------------------------------
 * Time-based EMA 필터 시간상수 tau(sec)
 * ----------------------------------------------------------------
 * alpha = 1 - exp(-dt / tau)
 * tau가 클수록 더 부드럽지만 반응은 느려진다.
 */
#ifndef PM_INPUT_2D_TAU_SEC
#define PM_INPUT_2D_TAU_SEC                     0.10f
#endif

#ifndef PM_POSITION_XY_TAU_SEC
#define PM_POSITION_XY_TAU_SEC                  0.12f
#endif

/* 단안 카메라의 Z가 가장 불안정하므로 X/Y보다 강하게 필터 */
#ifndef PM_POSITION_Z_TAU_SEC
#define PM_POSITION_Z_TAU_SEC                   0.28f
#endif

/* Wrist roll용 hand-plane normal 자체 필터 */
#ifndef PM_HAND_NORMAL_TAU_SEC
#define PM_HAND_NORMAL_TAU_SEC                  0.22f
#endif

/* Base / Shoulder / Elbow / Wrist Pitch */
#ifndef PM_JOINT_ANGLE_TAU_SEC
#define PM_JOINT_ANGLE_TAU_SEC                  0.12f
#endif

/* Wrist Roll은 가장 민감하므로 더 강하게 */
#ifndef PM_ROLL_ANGLE_TAU_SEC
#define PM_ROLL_ANGLE_TAU_SEC                   0.25f
#endif

/* ----------------------------------------------------------------
 * 3D 복원 유효성 조건
 * ---------------------------------------------------------------- */
#ifndef PM_MIN_SHOULDER_WIDTH_PX
#define PM_MIN_SHOULDER_WIDTH_PX                40.0f
#endif

#ifndef PM_MIN_BODY_DEPTH_UNIT
#define PM_MIN_BODY_DEPTH_UNIT                  0.5f
#endif

#ifndef PM_MAX_BODY_DEPTH_UNIT
#define PM_MAX_BODY_DEPTH_UNIT                  20.0f
#endif

/* Ray-Sphere 판별식이 Noise 때문에 약간 음수일 때 허용할 범위 */
#ifndef PM_RAY_DISC_RELAX_RATIO
#define PM_RAY_DISC_RELAX_RATIO                 0.10f
#endif

/* Finger/Wrist가 거의 일직선일 때 hand plane을 신뢰하지 않는 기준 */
#ifndef PM_MIN_HAND_PLANE_QUALITY
#define PM_MIN_HAND_PLANE_QUALITY               0.15f
#endif

/* Roll reference plane 퇴화 판단 기준 */
#ifndef PM_MIN_REFERENCE_QUALITY
#define PM_MIN_REFERENCE_QUALITY                0.15f
#endif

/* ----------------------------------------------------------------
 * 각도 노이즈 제거
 * ---------------------------------------------------------------- */
#ifndef PM_JOINT_DEADBAND_DEG
#define PM_JOINT_DEADBAND_DEG                   0.5f
#endif

#ifndef PM_ROLL_DEADBAND_DEG
#define PM_ROLL_DEADBAND_DEG                    1.0f
#endif


/*
 * 계산된 raw roll이 직전 raw roll에서 이 값 이상 순간 점프하면
 * CNN/단안 3D 복원에 의한 spike로 보고 이전 raw roll을 유지한다.
 * 로봇의 실제 각속도 제한은 Agent2가 담당한다.
 */
#ifndef PM_ROLL_SPIKE_MARGIN_DEG
#define PM_ROLL_SPIKE_MARGIN_DEG                35.0f
#endif

/* ----------------------------------------------------------------
 * Gripper 0/1 의도 판정
 * ----------------------------------------------------------------
 * finger1-finger2 pixel 거리 / shoulder pixel 거리 비율을 사용한다.
 * Hysteresis를 위해 Open/Close threshold를 다르게 둔다.
 *
 * 출력 의미:
 *   0 = CLOSE 의도
 *   1 = OPEN 의도
 *
 * 실제 물체를 어느 압력까지 잡을지는 Agent3 + 압력센서가 담당한다.
 */
#ifndef PM_GRIPPER_OPEN_RATIO
#define PM_GRIPPER_OPEN_RATIO                   0.10f
#endif

#ifndef PM_GRIPPER_CLOSE_RATIO
#define PM_GRIPPER_CLOSE_RATIO                  0.08f
#endif

/* ----------------------------------------------------------------
 * Wrist Roll Zero Calibration
 * ----------------------------------------------------------------
 * 고정 frame 수가 아니라 시간으로 평균낸다.
 * 사용자가 기준 자세를 유지한 채 약 0.8초 동안 calibration한다.
 */
#ifndef PM_ROLL_ZERO_CALIB_SEC
#define PM_ROLL_ZERO_CALIB_SEC                  0.80f
#endif

#ifndef PM_ROLL_ZERO_MIN_SAMPLES
#define PM_ROLL_ZERO_MIN_SAMPLES                5U
#endif

#endif /* ROBOT_CONFIG_H */
