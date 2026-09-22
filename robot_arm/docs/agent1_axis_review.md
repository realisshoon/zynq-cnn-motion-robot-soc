# Agent1 body-frame 검토 결과 (2026-09-22)

## 두 안을 비교하여 선택한 내용

| 항목 | 최종 선택과 이유 |
| --- | --- |
| 축 규약 | 두 안의 공통 방향 채택. 해부학적 X, projected camera-up Y, Z=X×Y. camera-forward 강제는 충돌하므로 제거. |
| 초기/필터 갱신 | 하나의 `complete_body_frame` helper를 공유하여 규약 중복·불일치 방지. |
| 퇴화 fallback | 프롬프트에서 허용한 기존 HOLD 정책 채택. 수직 부근에서 이전 Y로 임의 frame을 만들어 fresh로 출력하지 않음. raw/filtered 실패를 모두 상위로 전달. |
| 연속성 | 강화된 프롬프트 채택. 축뿐 아니라 shoulder/base/roll 시계열, 양방향 ±180 경계를 실제 관절 계산으로 테스트. |
| 단계 간 문서 | 강화된 프롬프트 채택. 여섯 출력값의 zero/sign/range 및 wrist fallback/zero calibration을 명시. |
| 통합 안전 영향 | `dev/robot` 통합 테스트에서 측면 6프레임이 수정 후 승인되는 것을 별도 확인. 이 feature 브랜치에는 통합 코드가 없어 커밋 범위에서 제외. |
| A2 전달문 | 원본의 분석 우선 범위 유지. 예상 규약을 확정 규약으로 바꾸고 미검증 로그 통계, reset, 입력 양자화, PWM 실측 경계를 명시. |

현재 작업 시작 시 실제 파일에는 과거 답변의 축 수정이 없었고 기존 Z 반전 코드가 있었다.
이번 결과는 현재 파일에 적용한 diff와 재실행 결과를 기준으로 한다.

## 원인과 재현

Z만 반전한 뒤 Y=Z×X로 재계산하면 Y도 반전한다.
제공된 P3 좌표를 직접 사용하는 회귀에서 기존 Y=(0,-1,0), shoulder=+76.9878°를
재현하고, 새 Y=(0,1,0), shoulder=-76.9878°, base=112.2811°를 확인했다.
elbow 검증용 wrist는 해당 회귀에서 합성 좌표이며 원본 로그 wrist라고 주장하지 않는다.
정확한 최종 계약은 [coordinate_system.md](coordinate_system.md)에 있다.

## 수정 파일

- `src/human_target_angle/pose_math.c`: 공통 frame 생성, up 의미 보존, raw/filtered 퇴화 실패 처리, 비유한 벡터 normalize 거부.
- `src/human_target_angle/pose_mapping_internal.h`: up projection 품질 최소값 0.01.
- `include/human_target_angle/pose_mapping.h`: public context의 축 의미 설명.
- `src/human_target_angle/pose_joint.c`, `pose_hand.c`: base/roll 기준 주석 명확화. 각도 수식은 유지.
- `tests/human_target_angle/test_body_frame.c`: 축·각도·퇴화·연속성 회귀.
- `tests/human_target_angle/run_axis_tests.sh`: 기존/추가 호스트 테스트 재실행.
- `docs/coordinate_system.md`, `docs/interface.md`: 좌표 및 출력 계약.
- 이 보고서와 [agent2_handoff_prompt.md](agent2_handoff_prompt.md): 선택 근거 및 전달문.

A2/A3 운영 코드, calibration, 안전검사, PWM 설정은 변경하지 않았다.
작업 시작부터 존재하던 README/PC logger/영상/도구/Vitis 등의 사용자 변경은 유지했다.

## 테스트

재실행: 프로젝트 `robot_arm`에서 다음 명령을 사용한다.

```bash
bash tests/human_target_angle/run_axis_tests.sh
```

GCC C99 `-Wall -Wextra -Wpedantic` 호스트 빌드. CMake에는 현재 존재하지 않는
타 모듈 source/test 경로가 있어 이번 범위에서 수정하지 않고 명시적 source 목록으로 빌드한다.
CSV 결과와 실행 파일은 스크립트가 출력하는 `/tmp/agent1-axis-tests.*` 경로에 저장한다.
결과 경로는 실행할 때 출력되는 `/tmp/agent1-axis-tests.*`다.

입력 SHA-256:

```text
example_pose2d_1280x720_20hz.csv: e311036ff2bd7099b8691725e814ab39e4c0e8517862eedba755f3d2e6e8d360
uart_pose_stream.bin:           7fec2e4d646a143410a48b1bc530ce914a656b99f9e0be14ac212baacbce931c
```

| 검증 | 결과 |
| --- | --- |
| 첫 P3 좌표 | PASS: shoulder +76.9878 → -76.9878, 새 base 112.2811 |
| 상완 위/수평/아래 | PASS: +90/0/-90 및 ±30 |
| elbow 의미 | PASS: straight 180, folded 0 |
| 정면/측면/반대편, 기울어진 어깨 | PASS: 직교·단위·오른손·projected up |
| 181프레임 yaw sweep | PASS: 화면 어깨 순서 변화, 축 flip 없음, base/shoulder/roll 변화량 검증 |
| base/roll ±180 | PASS: 양방향 경계에서 최단각 변화량 연속, wrapped 출력 범위 유지 |
| wrist 기준각 | PASS: roll 및 pitch 0/±30, front/side/back roll 기준 |
| 퇴화 | PASS: 초기 수직/근수직/겹침/NaN 실패, filtered blend만 수직인 경우 실패, 이전 frame 불변 및 reset 후 복구 |
| 기존 test_pose_mapping | PASS: 가변 dt, 중복 frame, dropout/복구, roll zero calibration |
| 기존 test_pose_csv | 실행 성공 + 522/522 fresh/valid 및 비유한 값 없음 검사 |
| 기존 test_pose_visual | 16초 mock stream 생성 성공. 시각적 실물 추종 정확도 판정은 아님 |
| 기존 UART + A1 | PASS: 522 decode/update, hold=0, invalid=0, CRC/format/range error=0 |

같은 UART binary를 수정 전후 right/0.05초/초기화 조건으로 재생했다.
첫 shoulder는 +76.963593→-76.963593, base는 67.714409→112.285576이었다.
반올림된 P3 직접 계산과 UART 정수 좌표 재복원 값은 약간 다르다.
CSV 도구는 첫 dt=1/15초, 소수 pixel 입력이므로 UART 도구와 수치를 그대로 동일시하지 않는다.

## 남은 한계와 A2 인계

- 단안 depth ambiguity와 body forward 추정 한계는 그대로다.
- 수직 pole 관통, normalized-X EMA의 정확한 반대 방향 정체는 일반 yaw sweep 통과로 해결되었다고 볼 수 없다.
- 수직 상완의 base 방위각, wrist roll reference의 Y→X 전환, 손 normal 부호 이력은 별도 한계다.
- 손목 zero를 사용했다면 새 규약에서 다시 잡아야 한다.
- 실제 base 반대 회전 원인은 A1 규약/A2 direction/실물 장착을 함께 확인해야 한다.
- 사용자 보고의 전체 로그 474/522 FLOOR_COLLISION은 이번 작업에서 원본 전체를 재검증하지 않았다.
  수정 후 전체 A2 거부 횟수도 아직 측정하지 않았다. `dev/robot`에서 별도 확인한
  측면 fixture 6개 통과를 전체 결과로 확대하지 않는다.
- 실물 flash/동작 검증은 하지 않았다. 호스트 PWM write/모의 출력은 실제 위치 측정이 아니다.
