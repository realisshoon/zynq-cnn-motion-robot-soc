#include "robot_calibration/forearm_calibration_config.h"

/*
 * 미실측 임시값이다. 아래 근거로 채웠다:
 *  - 90=중립: 기존 6축 서보들과 같은 조립 관례(1500us=90도)를 그대로 가정했다.
 *    이 관절들도 실제로 90도가 물리적 중립인지는 확인되지 않았다.
 *  - scale=1, direction=1: 방향 반전 여부를 전혀 모르므로 항등 매핑을 기본값으로
 *    두었다. 실물에서 반대로 움직이면 -1로 바꿔야 한다(기존 elbow처럼).
 *  - min/max=[20,160]: 기존 MG996R 5축에서 쓰던 기계적 끝단 회피 안전 여유를
 *    그대로 가져왔다. 이 새 관절들의 실제 가동범위가 그와 같다는 근거는 없고,
 *    특히 elbow_roll/elbow_pitch는 기구 형상이 달라 실제 최대각이 더 좁을 수 있다.
 *  - max_delta_deg=0.6(20ms 틱당, 초당 30도): legacy의 낮은 명령 속도를
 *    재사용했다. 하중/토크/가속도 또는 실제 새 기구의 안전 보장은 아니다.
 *  - amax_deg_s2=120(초당 120도의 초당): D:\Working\robot-motion-harness에서
 *    Codex가 검증한 SPEED_ACCEL 프로토타입 데모값을 그대로 가져왔다. 실제
 *    서보/부하로 측정한 값이 아니다 — Cortex-A9 실행시간도 아직 미검증.
 *
 * 실물 적용 전 필요한 실측 목록(2026-09-22):
 *  1) elbow_roll: 실제 회전 + 방향, 90도가 테이블 위 어느 방위를 가리키는지,
 *     진짜 가동범위(기계적 끝단).
 *  2) elbow_pitch: 실제 굽힘 방향, 90도가 정말 수평(FK가 가정하는 기준)인지,
 *     가동범위.
 *  3) wrist_pitch/wrist_roll: 방향, 90도 기준, 가동범위 — 기존 6축 wrist와는
 *     다른 새 관절이므로 이전 실측값을 재사용할 수 없다.
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
    .wrist_roll  = { .scale = 1.0f, .direction = 1, .zero_offset_deg = 90.0f,
                      .min_deg = 20.0f, .max_deg = 160.0f, .max_delta_deg = 0.6f },
    /* 순서: elbow_roll, elbow_pitch, wrist_pitch, wrist_roll. */
    .amax_deg_s2 = { 120.0f, 120.0f, 120.0f, 120.0f },
};
