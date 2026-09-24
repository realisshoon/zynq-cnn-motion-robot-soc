#include "robot_calibration/forearm_calibration_config.h"

/*
 * direction/zero_offset_deg 실측 현황(2026-09-23):
 *  - elbow_roll, elbow_pitch: BodyFrame 정의(pose_math.c)와 forearm_mapping.c의
 *    atan2 수식을 대조해서 "표값 = raw+90"이 대수적으로 정확히 성립함을 코드로
 *    확인했다. direction=1, zero_offset_deg=90 확정.
 *  - wrist_pitch: idle.jpg/straight.jpg 두 사진 raw 비교로 방향만 확인(손이
 *    더 아래로 굽은 idle이 실제로 더 낮은 표값으로 나옴) → direction=1 확정.
 *    offset 정밀값은 아직 미실측이라 기존 90 유지.
 *  - wrist_roll: 서보에 30/90/160을 직접 보내 실물 팔의 물리각을 표 기준으로
 *    실측(결과 30/90/170) → 최소제곱으로 zero_offset_deg≈87.15 산출. direction=1로
 *    확정(사람 사진 기반 초기 추정치 -1은 손가락 랜드마크 노이즈로 인한 오판으로
 *    판명, 폐기).
 *  - wrist_pitch/wrist_roll scale: example2 영상 재생 분석에서 클램프 경계에
 *    거의 항상 박혀 있는 걸 보고 한때 0.55/0.80으로 압축했었으나, example2와
 *    example3가 거의 같은 물리적 자세에서 시작했는데도 raw 각도가 전혀
 *    다르게 나온 걸로 봐서(예: elbow_roll 195도 차이) 그 "넓은 범위"의 상당
 *    부분이 실제 사람 움직임이 아니라 카메라 roll(기울어짐)이 BodyFrame에
 *    새어 들어간 결과였음이 드러났다(pose_math.c의 PM_CAMERA_ROLL_DEG 보정
 *    참고). scale=1로 롤백 — 카메라 roll 보정이 반영된 뒤 깨끗한 데이터로
 *    재검증 필요.
 *
 * 아직 미실측(자리표시자 유지):
 *  - min/max=[20,160]: 기존 MG996R 5축 안전 여유를 그대로 가져온 값이고, 이
 *    관절들의 실제 가동범위와 무관하다. 특히 elbow_roll/elbow_pitch는 기구
 *    형상이 달라 실제 최대각이 더 좁을 수 있다.
 *  - max_delta_deg=0.6(20ms 틱당, 초당 30도): legacy의 낮은 명령 속도를
 *    재사용했다. 하중/토크/가속도 또는 실제 새 기구의 안전 보장은 아니다.
 *  - amax_deg_s2=120(초당 120도의 초당): D:\Working\robot-motion-harness에서
 *    Codex가 검증한 SPEED_ACCEL 프로토타입 데모값을 그대로 가져왔다. 실제
 *    서보/부하로 측정한 값이 아니다 — Cortex-A9 실행시간도 아직 미검증.
 * 이 값들이 확인되기 전에는 forearm_calibration_apply()의 출력을 실제 서보로
 * 내보내지 말 것(문서/README에서 확인 절차를 안내한다).
 */
const ForearmCalibrationConfig forearm_calibration_config = {
    .elbow_roll  = { .scale = 1.0f, .direction = 1, .zero_offset_deg = 90.0f,
                      .min_deg = 20.0f, .max_deg = 160.0f, .max_delta_deg = 0.6f },
    .elbow_pitch = { .scale = 1.0f, .direction = 1, .zero_offset_deg = 90.0f,
                      .min_deg = 20.0f, .max_deg = 160.0f, .max_delta_deg = 0.6f },
    .wrist_pitch = { .scale = 1.0f, .direction = 1, .zero_offset_deg = 90.0f,
                      .min_deg = 20.0f, .max_deg = 160.0f, .max_delta_deg = 0.6f },
    .wrist_roll  = { .scale = 1.0f, .direction = 1, .zero_offset_deg = 87.0f,
                      .min_deg = 20.0f, .max_deg = 160.0f, .max_delta_deg = 0.6f },
    /* 순서: elbow_roll, elbow_pitch, wrist_pitch, wrist_roll. */
    .amax_deg_s2 = { 120.0f, 120.0f, 120.0f, 120.0f },
};
