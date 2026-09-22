# Agent1 Forearm 5축 변경 기록 (2026-09-22)

## 1. Current Code Analysis

기존 `pose_mapping_update`: frame 중복 검사 → 2D time EMA/valid mask → 어깨를 포함한
monocular relative 3D → BodyFrame 기반 base/shoulder + anatomical elbow → wrist/gripper → HOLD/cache.
`HumanJointTarget`은 `include/common/robot_types.h`에 정의되며 Agent2 motion_control,
robot_calibration, integration 및 trace가 사용한다. 필드를 같은 이름으로 바꾸면 다른 팀 코드가
컴파일돼도 물리적 의미가 달라지므로 교체하지 않았다.

재사용: `pose_tracking.c`, `pose_reconstruction.c`, `pose_math.c`의 utility,
`pose_hand.c`의 normal EMA/unwrap/spike 완화/roll zero/gripper hysteresis.
BodyFrame은 reconstruction의 직접 기준이 아니며 기존 major/wrist 계산에서 쓰인다.
기존 경로와 과거 Y축 회귀검증을 위해 삭제하지 않았다.

## 2. New Motion Model

새 `ForearmMappingContext`는 기존 `PoseMappingContext`를 포함하여 상태를 재사용한다.
제어의 기하학적 원점은 Elbow, 방향은 F=normalize(Wrist−Elbow).
어깨 landmark는 여전히 재구성/valid 판정에 필요하다. 어깨가 빠지면 기존처럼 major HOLD다.
어깨 모터를 없앤다는 것이 어깨 입력을 없앤다는 뜻은 아니다.

M0 elbow_roll←forearm_yaw, M1 elbow_pitch←forearm_pitch,
M2 wrist_pitch, M3 wrist_roll, M4 gripper.
elbow_roll이라는 기구 이름과 전완 자체 축 회전(손목 roll)을 혼동하지 않는다.

## 3. TableFrame Convention

카메라 공간에서 실측된 up/neutral forward를 받는다.
Z=normalize(up), X=normalize(forward−dot(forward,Z)Z), Y=Z×X; 마지막에 X=Y×Z.
유한값/영벡터/거의 평행한 입력을 거부한다. 기존 보정은 실패 시 유지한다.
유효한 설정 변경은 재구성·각도·roll zero 상태를 초기화한다.
초기화만 한 상태는 TableFrame이 없으므로 invalid다. 몸을 돌려도 TableFrame은 바뀌지 않는다.
카메라 이동 시에는 외부에서 보정값을 다시 공급해야 한다.

데모만 Z=(0,1,0), X=(0,0,1), Y=(1,0,0)을 명시적으로 사용한다.
`calibrated=0`이며 카메라가 테이블과 정렬됐다는 증거는 없다.
`calibrated=1`은 호출자의 보정 완료 선언이지 센서 측정의 정확성을 보증하지 않는다.
camera 좌표 (영상 오른쪽, 영상 위, 카메라에서 멀어짐)를 그대로 유지하므로 설치좌표로의
물리적인 정합·축 방향은 실제 중립 자세와 여러 기준 동작으로 확인해야 한다.

## 4. New Angle Convention

fx=F·X, fy=F·Y, fz=F·Z.

| 출력 | zero / positive | 범위 / wrap |
|---|---|---|
| forearm_yaw_deg | +X=0, +Z 오른손 방향 | [-180,180), wrap |
| forearm_pitch_deg | 테이블 평면=0, +Z 위쪽 양수 | [-90,90], no wrap |
| wrist_pitch_deg | 손 방향=F가 0, 손 normal 쪽 굽힘 양수 | [-180,180), wrap |
| wrist_roll_deg | 아래 R과 hand normal 일치가 0, F 오른손 회전 양수 | [-180,180), wrap |
| gripper_norm | 0=CLOSE, 1=OPEN | 이산값, no wrap |

전완 yaw=atan2(fy,fx), pitch=atan2(fz,hypot(fx,fy)). 기존 joint EMA/deadband를 재사용한다.
내부 yaw는 unwrap, 출력만 [-180,180). pitch는 unwrap하지 않는다.
기존 elbow 내각(펴진 팔 180°)은 새 전완 pitch와 전혀 다르다.

### 수직 특이점

정규화 F의 수평 길이 q<0.02(수직에서 약 1.15°)에 진입, q>=0.04(약 2.29°)에 이탈.
각도 smoothing 상수를 강화한 것이 아니라 방위각 관측 가능성의 hysteresis다.
특이점 안에서는 출력 yaw와 raw heading을 그대로 유지하고 pitch만 갱신한다.
첫 프레임부터 수직이면 yaw=0은 임시값이고 yaw_observable=0.
관측 가능한 heading이 한 번도 없으면 손목 기준도 임의이므로 손 target을 갱신하지 않는다.
수직을 통과해 반대쪽으로 나오면 정확한 방위각 자체가 크게 달라진다. 범위 pitch∈[-90,90]와
정확한 azimuth를 유지하면서 모든 pole crossing의 연속성을 보장할 수는 없다.
이탈 시 기존 EMA로 재수렴하며 robot rate limit은 A2 책임이다.

### Wrist 수식 / 한계

H=normalize(finger midpoint−Wrist), S=normalize(Finger2−Finger1),
A=normalize(project(S,F perpendicular)).
새 wrist pitch = −atan2(A·(F×H),F·H). 중립 roll에서 손이 테이블 위쪽으로 굽으면 양수다.
일반적으로는 손의 굽힘 평면 기준이며, 손을 roll시키면 그 굽힘 평면도 회전한다.
기존 pitch 계산을 공통 helper에서 보존하고 **새 API에서만** 부호 규약을 변환했다.
서보 mounting 방향 보정이 아니다. Legacy wrist pitch는 변경하지 않았다.
A가 불안정하면 reference×F fallback을 시도하되 hand plane 품질도 실패하면 hand HOLD다.

hand normal N=normalize(project(H×S,F perpendicular)). 이전 N과 음의 내적이면 부호 정렬,
기존 normal EMA 적용. raw roll=atan2(F·(R×N),R·N), unwrap,
기존 frame당 35° raw spike 완화, roll zero 차감, 기존 roll EMA 적용.

R은 table-up의 전완 수직 투영에 해당하는 elevation tangent:
h=cos(yaw_raw)X+sin(yaw_raw)Y,
R=normalize(project(cos(pitch_raw)Z−sin(pitch_raw)h,F perpendicular)).
평상시 table Z 투영과 동일, 수직에서는 held raw yaw를 사용하므로 영벡터가 되지 않는다.
BodyFrame은 새 wrist 경로에서 사용하지 않는다. Pole 이탈로 raw yaw가 바뀌면 R도 변한다.

손 landmark 두 개로 모든 손목 DOF를 정확히 관측하는 것은 불가능하다.
손 방향/손가락 span 평행, 투영 normal 소멸, ±90° flexion 근처는 품질 실패/HOLD가 가능하다.
N 부호 연속화는 palm/dorsal 180° 모호성과 한 프레임의 90° 초과 회전을 복원하지 못한다.
Finger1/2 라벨 뒤바뀜은 roll 부호 정렬로 완화하지만 flexion 부호를 보장하지 않는다.
raw spike cap 역시 물리적 모터 속도 제한이 아니다.

## 5. Interface Change / 통합 경계

Before: `HumanJointTarget` (base/shoulder/elbow/wrist/gripper).
After: 별도 `HumanForearmTarget` (`include/human_target_angle/forearm_mapping.h`).
old→new struct cast나 field alias는 없다. 기존 main/A2/A3는 아직 legacy 경로를 호출한다.
새 singleton wrapper는 `agent1_forearm_stage_init/run/output`이다.
직접 context API는 `forearm_mapping_init/set_table/update`.
roll zero는 embedded pose의 기존 API 또는 새 stage wrapper를 사용한다.
ROBOT_TRACE getter는 legacy/new 모두 실제 context를 const 포인터로 제공한다.

- valid: major geometry 목표 유효. 물리적 안전 승인이나 손의 현재 관측 성공이 아님.
- frame_id: 마지막 fresh major target의 입력 ID. HOLD에서는 이전 ID를 보존한다.
- yaw_observable: 현재 heading 측정 가능. singular/duplicate/major HOLD에서는 0.
- hand_fresh: 이번 프레임 손 갱신 성공. missing/degenerate/duplicate/HOLD에서는 0.
- calibrated: caller가 선언한 TableFrame calibration 여부.
- 반환값: fresh=1, HOLD/duplicate=0, invalid=-1.
- 같은 frame_id는 재필터링/시간 aging하지 않는다. 측면 변경은 같은 ID여도 history 초기화.
- major dropout: 실제 dt 누적 0.35초까지 HOLD, 초과 invalid; stale hand history 초기화.
- finger dropout: major는 계속 갱신, 손만 마지막 값 유지. 이전 손값도 없으면 0/0/OPEN,
  hand_fresh=0. 손 장기 dropout의 실제 모터 정책은 A2/3가 정해야 한다.
- 비유한 2D 좌표는 새 경로에서 missing으로 처리하여 EMA state 오염을 방지한다.

## 6. Modified Files

- `forearm_mapping.h`, `forearm_mapping.c`, `forearm_mapping_internal.h`: 새 타입/고정 TableFrame/전완 추정/상태 연결.
- `agent1_forearm_stage.h/.c`: 기존 A2에 혼입되지 않는 새 통합 진입점.
- `pose_hand.c`, `pose_mapping_internal.h`: 명시적 손목 reference 선택만 추가, 공통 알고리즘 유지.
- `CMakeLists.txt`: Agent1 소스 등록과 정식 AGENT1_ONLY 모드, 회귀 타깃. A2/A3 소스 미수정.
- `test_pose_csv.c`, `run_axis_tests.sh`: 기존 입력 replay 유지, 새 출력 명칭과 명시적 TableFrame.
- `test_forearm.c`, `test_agent1_trace.c`, `test_forearm_viewer.py`: 신규 검증.
- `agent1_video_xyz_viewer_v3.py`: header 기반 신/구 schema 판별, 새 각도와 table/forearm 화살표.
- `analyze_forearm_replay.py`: frame 통계/범위/wrap/합성 depth 민감도.
- README/본 문서/새 A2 prompt 및 기존 문서 안내: 실행/계약/이전 계약 구분.

기존 미커밋 `agent1_stage.h/.c`의 TRACE getter 복구,
`pc/send_pose_uart_with_video.py` 자동 로그 저장은 이번 변경으로 덮어쓰지 않았다.

## 7. Python / Test Asset Status

| 자산 | 역할 | 이번 상태 |
|---|---|---|
| tools/extract_real_person_pose_v3.py | A: Pose2D 생성 | UNCHANGED |
| pc/send_pose_uart.py | B: 기존 UART 입력 | UNCHANGED |
| pc/send_pose_uart_with_video.py | B: UART+영상+로그 | UNCHANGED; 선행 미커밋 복구 보존 |
| tools/agent1_video_xyz_viewer_v3.py | C: 결과 표시 | MODIFIED; legacy 읽기 유지 |
| test_pose_csv | C: 실제 Pose2D replay/결과 기록 | MODIFIED |
| test_pose_mapping / test_body_frame | D: tracking/body/기존 wrist 회귀 | UNCHANGED |
| test_pose_visual / test_uart_agent1_pc | C/D: legacy path 검증 | UNCHANGED; 5축 검증을 대체하지 않음 |
| run_axis_tests.sh | D: 기존 suite 묶음 | MODIFIED; 새 replay 호출 옵션 |
| 기존 base/shoulder/elbow 공개 제어 계약 | 옛 control path | DEPRECATED for 새 5축만; 보드 legacy 보존 |
| A2/A3 파일 | E: mapping/FK/safety/PWM | UNCHANGED |

REMOVED 테스트/입력 도구 없음. 별도 신규 전완·UART·TRACE·뷰어 테스트를 추가했다.

## 8. Test Results

- GCC 15.2, Debug `-Wall -Wextra -Wpedantic -Werror` clean build 성공.
- CTest 7/7 PASS: pose_mapping, body_frame, forearm, forearm_uart, forearm_csv,
  agent1_trace, forearm_replay_stats.
- run_axis_tests.sh PASS: 과거 +77→−77 shoulder 회귀, 정면/측면/축 순서/퇴화/unwrap,
  기존 pose_mapping/mock/legacy UART 522프레임 포함.
- 새 geometry: ±X/±Y, ±Z, ±45° pitch, 회전된 table/직교성, 영벡터/NaN,
  ±179 wrap/EMA, 위아래 pole noisy yaw HOLD/hysteresis, 어깨와 direct angle reference 분리 PASS.
- 새 wrist: neutral/±pitch/±roll/조합/wrap, pole reference 연속성,
  초기 pole reference 미관측, 퇴화 plane HOLD, roll zero, gripper hysteresis PASS.
- 새 pipeline: valid/invalid/duplicate(frame state 불변)/frame_id/major timeout/
  장기 hand dropout/side 변경/NaN 후 회복, UART 재생 PASS.
- Python viewer 신/구 CSV 로딩·panel 렌더 2/2 PASS. 0.3초 720px 20fps preview 생성 성공.
- ASan+UBSan 7/7 PASS (`ASAN_OPTIONS=detect_leaks=0`). 최초 LeakSanitizer는
  실행환경의 ptrace 제약으로 실패했으므로 **누수 검사는 검증하지 못함**.
- Vitis 신규 5축 전체 빌드/보드 flash/서보 동작: NOT VERIFIED. 호스트 TRACE compile/link만 검증.

## 9. 522-frame Replay Result

`etc/example_pose2d_1280x720_20hz.csv`, right arm, DEMO TableFrame, 실제 timestamp dt.
522 fresh/valid, invalid 0, major HOLD 0, yaw 미관측 1(frame 31), hand HOLD 9,
gripper 전환 23. full output: `build/agent1/forearm_result.csv` (generated artifact).

| 각도 (°) | min | max | 최대 연속프레임 변화 | 발생 frame |
|---|---:|---:|---:|---:|
| forearm yaw | -178.55 | 179.48 | 72.10 | 32 |
| forearm pitch | -84.55 | 16.48 | 12.97 | 509 |
| wrist pitch | -81.18 | -5.29 | 10.96 | 511 |
| wrist roll | -179.48 | 179.76 | 32.45 | 175 |

yaw/손목 delta는 shortest-angle. Yaw wrap frames: 3,13,60,68,117,124,145,153,171.
Roll wrap: 93,96,185,232,233. Wrist pitch wrap 없음.
각도 EMA 전 raw yaw max delta 160.23° → 출력 72.10°,
raw pitch RMS delta 3.57° → 출력 2.86°. 이는 실제 motion도 포함하므로 센서 noise 표준편차가 아니다.
**이 수치는 안전 PASS가 아니다.** frame31 pitch raw≈−89.21°에서 yaw HOLD;
32에서 raw pitch≈−84.50°, raw yaw99.78→−60.44°로 반대쪽에 재진입한다.
수직 특이점 이탈/단안 깊이 모호성을 A2가 반드시 다뤄야 한다.

합성 민감도: Camera ΔZ에 전완 길이 ±2%를 가하고 angle만 재계산(관측 noise 측정 아님).
최대 yaw 변화 37.64°, pitch 1.14°. 이 DEMO의 table Z는 Camera Y이므로
camera depth가 곧 table height는 아니다. 보정 방향에 따라 영향이 달라진다.
측면 proxy `abs(shoulder Camera-X span)/3D span<0.35`: 91 frames,
같은 perturbation 최대 yaw1.18°/pitch0.05°. 몸의 실제 yaw 측정/측면 정확도 보장은 아니다.
기존 reconstruction side-view 6 fixture도 회귀 통과했지만 실측 depth 검증은 아니다.
입력 시간간격 min/median/max≈0.033416/0.033421/0.066840초. 파일명만으로 고정 dt를 가정하지 않는다.

## 10. README

[README_AGENT1_TEST.md](../README_AGENT1_TEST.md): 실제 실행한 빌드/CTest/CSV/통계/preview 명령.
UART는 옵션 확인만 했고 물리 실행은 NOT VERIFIED로 구분했다.

## 11. Remaining Risks

보정된 TableFrame 없음, 단안 깊이/어깨 occlusion에 의한 재구성 모호성,
pole 이탈의 큰 yaw 변화, 손 normal/라벨 모호성, 장기 손 dropout,
카메라-실물 축 정합 미검증. 기존 35° spike 완화와 EMA를 모터 속도제한으로 쓰면 안 된다.
현재 결과를 PWM에 직결하지 말 것. 이번 변경은 인간 자세 의미 계층이며 로봇 안전 판정이 아니다.

## 12. Agent2 Handoff Summary

새 typed API로 명시적으로 연결하고 old HumanJointTarget을 reinterpret하지 않는다.
zero/direction/offset/scale/minmax/rate/shortest-angle/FK/workspace/table collision은 A2 책임.
**vertical installation assumptions must be reviewed.** 기존 FLOOR_COLLISION 우회 금지.
서보 PWM 실측값을 버리거나 임의로 표준값으로 초기화하지 않는다.

현재 `SERVO_COUNT`는 실제 6채널이다. `ROBOT_MOTION_JOINT_COUNT=5`는
기존 base/shoulder/elbow/wrist_pitch/wrist_roll **gripper 제외** 개수이므로
새 5서보와 같은 뜻이 아니다. 새 비-gripper 관절은 4개다.
JointCommand/PwmCommand named fields, servo_config/servo_control/servo_hal,
PL channel wiring/startup, trace/record/playback 자료형과 채널 순서를 A3에 인계한다.
기존 record 관련 CMake 항목은 실제 소스가 없으므로 존재한다고 가정하지 않는다.

## 13. Agent2 Codex Prompt

복사 가능한 최종본: [agent2_forearm_handoff_prompt.md](agent2_forearm_handoff_prompt.md).
