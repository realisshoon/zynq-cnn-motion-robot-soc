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
 * 카메라 설치 시 좌우로 기울어진 각도(roll, 도 단위). 이미지 위쪽(+Y)이 실제
 * 중력 위쪽과 이루는 각도를 수평계/폰 각도계 앱으로 측정해서 넣는다. 카메라를
 * 위/아래로 향하는 각도(pitch)는 이미지의 up 벡터 자체를 안 바꾸므로 여기서
 * 다루지 않는다 — roll만 BodyFrame의 up 기준(pose_math.c)을 틀어지게 만든다.
 * 부호는 실물로 확인 후 반대로 나오면 뒤집는다(다른 PM_* 실측 상수들과 동일).
 * 기본값 0 = 카메라가 수평이라고 가정하는 기존 동작과 동일.
 */
#define PM_CAMERA_ROLL_DEG                       0.0f

/*
 * 카메라가 세션 중에도 계속 움직일 수 있어서(핸드헬드/진동/팔 마운트),
 * PM_CAMERA_ROLL_DEG 고정값만으로는 부족하다. 아래는 "사람이 어깨를 한쪽으로
 * 오래 기울인 채 버티는 일은 드물다"는 가정으로, 어깨선(BodyX)의 이미지평면
 * 기울기를 느리게 따라가며 카메라 roll을 매 프레임 추정하는 휴리스틱이다
 * (pose_math.c의 pm_camera_roll_estimate_from_x() + pm_update_stable_body_frame()).
 * 완벽하지 않다 — 사람이 실제로 수십 초 이상 어깨를 기울이면 그것도 카메라
 * roll로 오인해 서서히 지운다. "정확하지 않아도 되니 지금의 큰 오차만 줄이면
 * 된다"는 절충으로 채택했다(hip landmark/IMU 없이 6점 입력만으로 가능한 범위).
 * 카메라 자체는 격렬하게 흔들리기보다 천천히 조금씩 틀어지는 쪽에 가깝다는
 * 전제로 아래 두 값을 잡았다 — 둘 다 실측 아님, 출발값.
 */
#define PM_CAMERA_ROLL_ADAPT_ENABLE               1
/* 저역통과 시정수(초). 사람 동작(<2초)과는 안 섞이면서, 카메라가 서서히
 * 안착하는 변화는 너무 늦지 않게 따라가도록 5초로 잡음. */
#define PM_CAMERA_ROLL_ADAPT_TAU_SEC               5.0f
/* 추정치 clamp. 카메라 흔들림 폭 자체가 크지 않다는 전제라 넓게 열어둘 필요가
 * 없고, 사람이 오래 어깨를 기울인 경우와 헷갈리더라도 피해 범위를 작게
 * 묶어두기 위해 좁게(±20°) 잡는다. */
#define PM_CAMERA_ROLL_ADAPT_MAX_DEG              20.0f
/* 이 추정은 사람이 카메라를 대체로 정면으로 보고 있다는 가정에 기대므로,
 * BodyX의 x성분이 충분히 음수(-1에 가까움 = 정면)일 때만 신뢰한다. 사람이
 * 몸을 돌리면(yaw) 이 가정이 깨지므로 그런 frame은 추정 갱신에서 제외한다.
 * 0.85 = 카메라 기준 약 32도 이상 돌아서면 이 frame의 관측을 버린다는 뜻. */
#define PM_CAMERA_ROLL_ADAPT_MIN_FRONTAL          0.85f

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
#define PM_PITCH_SPIKE_MARGIN_DEG               35.0f

/* Gripper 의도: 0.0=CLOSE, 1.0=OPEN */
#define PM_GRIPPER_OPEN_RATIO                   0.10f
#define PM_GRIPPER_CLOSE_RATIO                  0.08f

/* Human wrist roll zero calibration */
#define PM_ROLL_ZERO_CALIB_SEC                  0.80f
#define PM_ROLL_ZERO_MIN_SAMPLES                5U

#endif /* ROBOT_CONFIG_H */
