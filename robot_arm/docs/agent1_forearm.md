# Agent1 Human Body 기준 전완 해석 — 교정 기록

## 1. Previous Wrong Change Analysis

시작 시 working tree는 clean이었다. 앞선 5축 구현은 커밋된 상태였다.
새로운 전면 구현이나 전체 revert 없이 해당 구현을 교정했다.
잘못 추가됐던 것은 Agent1의 고정 TableFrame, up/forward 설치 보정 API,
table 축 투영 yaw/pitch, table 기반 wrist reference, CSV/뷰어 축·보정 열이었다.
Agent1이 사람 각도를 계산하고 Agent2에서 로봇 설치 정보를 적용한다는 최종 역할에 맞지 않았다.

## 2. What Was Kept

HumanPose2D, 6-landmark UART, 카메라 내부 파라미터, relative 3D reconstruction,
2D/3D EMA, frame_id/중복 처리, valid mask, major HOLD, hand dropout,
gripper hysteresis, hand normal EMA/unwrap/roll zero, 기존 Body Y 반전 수정 모두 보존했다.
앞선 5축의 context/stage, 명시적 관측 flag, 전완 특이점 HOLD, CSV/뷰어 인프라,
AGENT1_ONLY, README 및 회귀 테스트 구조도 재사용했다. 새 motion rate limiter는 없다.

## 3. What Was Corrected

REMOVE: TableFrame, forearm_mapping_set_table, table calibration flag/인수,
CSV의 table 축/보정 열, 뷰어의 table 축, 관련 설치 기준 설명/테스트.
FIX: 기존 pm_update_stable_body_frame을 새 전완 계산 앞에서 호출하고 같은 몸 축을 손목에서도 사용.
공개 필드 forearm_yaw/pitch → elbow_roll/pitch. 단순 이름 변경이 아니라 투영 축과 식도 교정.
손목 pitch는 직전 새 경로에서 추가한 부호 반전을 제거하여 기존 HUMAN 규약과 일치시켰다.
HumanForearmTarget의 정의를 공용 robot_types.h로 이동했다.
기존 잘못된 CSV는 뷰어가 명시적으로 거부한다. 과거 결과를 새 사람 각도로 잘못 표시하지 않는다.

## 4. Final Agent1 Data Flow

HumanPose2D → 중복/유효성/기존 2D EMA → 기존 relative XYZ 복원/필터
→ 양 어깨 기반 stable BodyFrame → Elbow→Wrist의 몸 기준 방위각/고도각
→ Body/전완 기준 손목과 기존 gripper → HUMAN target.

새 공개 API는 forearm_mapping_init/update 및 agent1_forearm_stage_init/run/output이다.
init에는 설치값 인수가 없다. 어깨는 재구성·BodyFrame 생성·valid 판정에 계속 필요하다.
기존 pose_mapping_* / agent1_stage_*는 6축 legacy 통합 경로로 유지한다.
A2/integration은 아직 legacy 자료형을 소비하므로 후속 단계에서 새 typed API를 연결해야 한다.

## 5. Final Human Body Frame

X=normalize(Shoulder_R−Shoulder_L), 이후 기존 time-based stable X 사용.
Y=normalize(CameraUp−dot(CameraUp,X)X), CameraUp=(0,1,0).
Z=normalize(X×Y), 마지막 Y=normalize(Z×X).
X×Y=Z인 수학적 기저를 유지한다. Z가 Camera +Z를 향하도록 강제 반전하지 않는다.
+Z를 항상 해부학적 정면이라고 단정하지 않는다. 최종 zero는 이 계산된 +Z다.
어깨선이 CameraUp과 거의 평행하면 축 생성을 실패시켜 기존 bounded HOLD/invalid로 간다.

Camera relative 좌표는 X 영상 오른쪽, Y 영상 위, Z 카메라에서 멀어짐.
Body Y는 실제 척추/중력 센서에서 얻은 위쪽이 아니라 CameraUp을 이용한 추정이다.

회전 불변성 검증을 구분했다:
- 이미 주어진 BodyFrame 전체와 모든 3D 점에 같은 3D 회전을 적용:
  각도 및 wrist reference가 같은 Body-local 의미를 유지함을 검증.
- 실제 두 어깨로 Frame 재생성: CameraUp을 보존하는 Y축 회전은 초기/정적 기준에서 검증.
- 어깨선을 축으로 몸을 앞으로 기울여도 양 어깨 위치는 같을 수 있어 몸 숙임을 관측할 수 없음.
  +Z 전완을 X축 주위 30° 회전하는 반례에서 pitch 0→−30°를 확인.
시간 필터의 추종 지연과 단안 복원 오차까지 없어졌다는 뜻은 아니다.

## 6. Final Human Angle Convention

F=normalize(Wrist−Elbow), fx=F·X, fy=F·Y, fz=F·Z.
각도 계산식은 도 단위로 변환한다.

| HUMAN 출력 | reference / axis | zero / positive | range / wrap |
|---|---|---|---|
| elbow_roll_deg | BodyFrame / +Body Y | +Body Z=0, +Body X 쪽 양수 | [-180,180), wrap |
| elbow_pitch_deg | Body XZ 평면 / 현재 방위각의 elevation | 평면=0, +Body Y 쪽 양수 | [-90,90], no wrap |
| wrist_pitch_deg | 전완과 손 / 전완에 수직 투영한 Finger1→Finger2 | 손 방향=F가 0, 해당 축 오른손 회전 양수 | [-180,180), wrap |
| wrist_roll_deg | Body/전완 reference / F | reference normal=hand normal이 0, F 오른손 회전 양수 | [-180,180), wrap |
| gripper_norm | finger pixel 간격÷어깨 간격 / 회전축 없음 | CLOSE=0, OPEN=1 | 이산값, wrap 없음 |

elbow_roll=atan2(fx,fz), elbow_pitch=atan2(fy,hypot(fx,fz)).
roll 이름이지만 첫 값은 전완 자체의 비틀림이 아니라 몸 위쪽 축 주위 방위각이다.
기존 base와 zero/positive 규약이 같고, 사용하는 벡터가 위팔에서 전완으로 바뀌었다.
기존 anatomical elbow(펴진 팔 180°)와 새 elbow_pitch를 연결하지 않는다.

### 전완 특이점과 상태

q=hypot(fx,fz) < 0.02일 때 진입, q>=0.04일 때 이탈한다.
이전 5축 경로의 관측 hysteresis를 재사용했으며 필터 강도는 바꾸지 않았다.
특이점 안에서는 출력 방위각/마지막 raw 방위각을 유지하고 pitch는 계속 갱신한다.
처음부터 수직이면 방위각 0°는 임시값, elbow_roll_observable=0.
관측 가능한 방위각을 한 번도 얻지 못한 상태에서는 wrist reference zero도 임의이므로
hand를 갱신하지 않는다. pole 이탈에는 큰 변화가 남을 수 있다.

major dropout/재구성 실패/BodyFrame 실패: 실제 dt 누적 0.35초까지 마지막 target HOLD,
그 이후 invalid. 중복 frame은 재필터링·시간 누적 없음. 새 프레임이 오지 않는 시간의
실제 timeout은 상위 통합 계층에서 관리해야 한다.
손만 missing/geometry 실패이면 major는 fresh, wrist/gripper만 유지한다.
손 초기값은 0/0/OPEN이며 hand_fresh=0이다.
활성 팔 전환은 같은 frame_id여도 history와 roll zero 초기화.
비유한 좌표는 새 경로에서 missing 취급해 EMA state 오염을 방지한다.

### 손목 재사용과 Body/Forearm reference

H=normalize(Finger midpoint−Wrist), S=normalize(Finger2−Finger1),
A=normalize(project_perpendicular(S,F)).
기존 wrist pitch = atan2(A·(F×H),F·H).
항상 '위로 굽힘=양수'라고 해석하지 않는다. 중립 F=Body Z, S=Body X에서는
Body +Y 쪽 굽힘이 음수다. 기존 사람 각도 부호를 보존했고 실제 서보 방향은 A2에서 정한다.
A가 퇴화하면 reference×F fallback을 시도한다. 손 평면 품질까지 실패하면 hand HOLD.

hand normal N은 H×S를 전완에 수직 투영, 이전 normal과 부호 정렬, 기존 EMA 적용.
손목 roll의 reference R은:
h=sin(raw_elbow_roll)BodyX+cos(raw_elbow_roll)BodyZ,
R=normalize(project_perpendicular(cos(raw_elbow_pitch)BodyY−sin(raw_elbow_pitch)h,F)).
일반 자세에서는 Body Y 투영과 같고 pole에서는 held Body-local 방위각으로 연결한다.
raw roll=atan2(F·(R×N),R·N), 기존 unwrap/35° raw spike 완화,
사용자 roll-zero offset, normal/angle EMA를 재사용한다.
이것은 새 로봇 rate limit이 아니다. legacy의 Body X/Z fallback은 구 경로에서 유지되며,
새 경로는 특이점에서 위의 연속 reference를 사용한다.

N의 180° 부호 모호성, 손가락 라벨 교환, 급격한 큰 회전은 완전히 관측할 수 없다.
손 방향/span 평행이나 전완 수직 투영 normal이 너무 작은 자세는 hand HOLD.
roll zero만의 관측 불확실성 때문에 현재는 손 전체(그리퍼 포함)를 HOLD할 수 있다.
기존 gripper 계산의 결합 구조를 바꾸지 않았으므로 A2가 hand_fresh를 확인해야 한다.

## 7. Interface Before / After

HumanForearmTarget 이름을 유지하여 불필요한 새 타입/대규모 rename을 피했다.
기존 HumanJointTarget은 A2/통합/legacy 테스트의 광범위한 의존성이 있어 필드를 바꾸지 않았다.

Before: forearm_mapping.h에 forearm_yaw_deg/forearm_pitch_deg,
yaw_observable/calibrated 및 고정 기준 설정 API.
After: include/common/robot_types.h에 다음 공용 HUMAN 타입:

    typedef struct {
        float elbow_roll_deg, elbow_pitch_deg;
        float wrist_pitch_deg, wrist_roll_deg, gripper_norm;
        uint32_t frame_id;
        uint8_t valid, elbow_roll_observable, hand_fresh;
    } HumanForearmTarget;

valid=major geometry 유효이며 robot safety 승인이 아니다.
frame_id=마지막 fresh major ID, HOLD는 이전 ID.
elbow_roll_observable=이번 방위각 관측 성공, duplicate/major HOLD/특이점에서는 0.
hand_fresh=이번 손 갱신 성공, duplicate/HOLD/손 missing에서는 0.
return fresh=1, HOLD/duplicate=0, invalid=-1. 자료형 간 cast/alias 없음.

## 8. Modified Files

- include/common/robot_types.h: 새 HUMAN 계약 공용화, legacy 타입 구분.
- include/human_target_angle/forearm_mapping.h: 고정 설치 기준 제거, 상태 필드 교정.
- src/human_target_angle/forearm_mapping.c 및 내부 헤더: stable BodyFrame 투영/손목 reference 교정.
- agent1_forearm_stage.h/.c: 인수 없는 초기화, 기존 stage 구조 유지.
- tests/human_target_angle/test_forearm.c: Body 기하·회전 불변성·추정 한계·wrist 부호/연속성 검증.
- test_agent1_trace.c: 새 초기화/BodyFrame 상태 검증; legacy getter 유지.
- test_pose_csv.c: HUMAN 필드/Body 축, 설치 인수 제거, 선택적 left/right replay.
- CMakeLists.txt / run_axis_tests.sh: 제거한 CSV 옵션 교정.
- tools/agent1_video_xyz_viewer_v3.py / test_forearm_viewer.py:
  Body 축 표시, HUMAN 명칭, 활성 팔 연결, 폐기된 CSV 거부.
- tools/analyze_forearm_replay.py: 새 열/Body 기준 민감도/큰 jump의 축·전완 변화 분해.
- README_AGENT1_TEST.md, 본 문서, A2 prompt, README/coordinate 안내: 교정된 계약 반영.

입력/복원/BodyFrame 생성 함수/공통 hand kernel/서보 설정/A2/A3/integration 코드 미변경.
기존 getter와 PC 로그 기능도 보존했다.

## 9. Python/Test Asset Status

| 자산 | 상태 | 설명 |
|---|---|---|
| extract_real_person_pose_v3.py | UNCHANGED | Pose2D 생성 |
| send_pose_uart.py / send_pose_uart_with_video.py | UNCHANGED | 입력 packet/자동 로그 |
| agent1_video_xyz_viewer_v3.py | MODIFIED | HUMAN 각도/Body 축/구 CSV 구별 |
| test_pose_csv | MODIFIED | 기존 입력 → 교정된 출력 |
| test_pose_mapping / test_body_frame / test_pose_visual | UNCHANGED | legacy·공통 기능 회귀 |
| test_uart_agent1_pc | UNCHANGED | 기존 UART/6축 회귀 |
| test_forearm / test_agent1_trace / test_forearm_viewer | MODIFIED | 잘못된 기준 기대값을 교정 |
| 고정 기준 API/축/설치 테스트 | REMOVED/REPLACED | BodyFrame와 회전 테스트로 대체 |
| 과거 고정축 5축 CSV 계약 | DEPRECATED | 재생성 필요 |
| A2/A3 테스트·코드 | UNCHANGED | 담당자 후속 전환 |

기존 정상 회귀 테스트를 삭제하지 않았다.

## 10. Test Results

- Debug GCC -Wall -Wextra -Wpedantic -Werror 빌드 성공, CTest 7/7 PASS.
- 기존 axis suite PASS: 과거 첫 프레임 shoulder +76.9878→−76.9878 회귀,
  view/tilt/degeneracy/wrap, reconstruction side-view fixture, mock 및 legacy UART 포함.
- 새 ±Body X/Z 방위각, ±Body Y/상하 pitch, noisy pole HOLD/hysteresis, ±179 wrap/EMA PASS.
- 임의 축 회전에서 supplied BodyFrame+팔의 각도 불변성, reference 회전 공변성 PASS.
- camera-up 보존 회전의 frame 재생성, 카메라 up 기반 몸 숙임 반례 PASS.
- 기존 P3 fixture의 synthetic parallel forearm pitch≈−76.99° 회귀 PASS.
- wrist neutral/±pitch/±roll/조합/wrap/normal 부호 연속화/roll zero/gripper PASS.
- 회전된 몸·손에서도 wrist 각도 동일, 일반 자세에서 legacy wrist 부호/zero 일치 PASS.
- frame_id/duplicate/major HOLD/장기 hand dropout/NaN 회복/side 변경/새 UART522 PASS.
- Python viewer 3/3 PASS: 새 CSV, legacy CSV, 폐기된 고정축 CSV 거부.
- 0.3초 20fps preview 생성 성공. 전체 길이/새 Vitis 전체 빌드/보드 동작 NOT VERIFIED.
- TRACE getter는 PC에서 선언·링크·실행 확인했다.
- ASan/UBSan 빌드의 CTest 7/7 PASS. 실행환경의 기존 ptrace 제약 때문에
  ASAN_OPTIONS=detect_leaks=0으로 실행했으며 누수 검사는 포함하지 않았다.

## 11. 522-frame Replay

입력 etc/example_pose2d_1280x720_20hz.csv, right, 실제 timestamp dt 사용.
출력 build/agent1/forearm_result.csv. 아래 각도 변화는 wrap을 고려한 최단 각도 차다.

| HUMAN 각도 (°) | min | max | 최대 frame delta | 발생 frame |
|---|---:|---:|---:|---:|
| elbow_roll | -176.2356 | 179.1084 | 59.6451 | 32 |
| elbow_pitch | -84.5886 | 16.6858 | 12.8533 | 509 |
| wrist_pitch | 5.2894 | 81.1785 | 10.9596 | 511 |
| wrist_roll | -179.8623 | 179.2471 | 23.5921 | 34 |

fresh/valid522, invalid0, major HOLD0, 방위각 미관측1(frame171),
hand HOLD9, gripper 전환23.
elbow roll wrap frames: 36,38,90,96; wrist roll:93,96,232,233.
출력의 ±180 표시 전환과 실제 최단각 jump를 구분했다.

원인 분해(추정 좌표 기준, 실제 센서 ground truth 아님):
- 최대 elbow roll 변화31→32: BodyFrame 회전0.45°, 전완 카메라 방향 회전5.25°,
  수평 길이 q0.0260→0.0914. fixed previous Body에서 방위각 변화−84.03°,
  Body basis 변화 기여−2.44°. 큰 변화의 주원인은 Body 축 반전이 아닌
  작은 수평 성분에서의 방위각 민감도다. q0.026은 현재 진입 기준0.02보다 커 관측 가능으로 표시된다.
- frame171 q≈수직으로 HOLD, 172 raw 방위각 변화175.02°.
  이후173의 출력 변화−56.70°는 재진입 이후 EMA 추종 과정도 포함한다.
- 최대 wrist roll 변화33→34: BodyFrame 회전0.53°, 전완 회전2.61°.
  직전 pole 근처에서 reference/손 normal/기존 raw spike·EMA의 추종 영향도 가능한 구간이다.
  몸 축만의 문제라고 단정하지 않으며 단안 오차와 실제 손 움직임 기여는 이 로그만으로 분리 못한다.
- 단안 깊이 민감도 확인: Camera ΔZ를 전완 길이±2%만큼 합성 교란하면
  최대 elbow roll 변화49.39°, pitch1.14°. 측정된 noise 분포는 아니다.
  shoulder Camera-X span/3D span<0.35인 측면 proxy91 frames에서는 각각1.18°/0.09°.
- raw elbow pitch RMS delta3.59° → 출력2.89°. 움직임도 포함하므로 순수 noise 표준편차가 아니다.
  임의로 smoothing/rate limit을 추가하지 않았다.

파일명은20hz지만 timestamp min/median/max≈0.033416/0.033421/0.066840초.
이번 결과가 모터 직결의 안전성을 뜻하지 않는다. A2의 rate/FK/collision 검증이 필요하다.

## 12. README

[README_AGENT1_TEST.md](../README_AGENT1_TEST.md)에 실제 build/CTest/replay/stats/viewer 명령을 교정했다.
설치 방향 인수 없이 실행한다. UART는 옵션 확인과 실물 실행 미검증을 구분했다.

## 13. Remaining Risks

CameraUp 기반 Body Y의 몸 숙임 한계, 시간 필터 지연, 단안 depth 모호성,
관측 hysteresis 밖 near-pole jump, pole 이탈 큰 각도 변화, hand normal/라벨 모호성,
장기 손 missing이 남는다. 서보·로봇 설치 문제와 이 인간 자세 추정 한계를 구분해야 한다.

## 14. Agent2 Handoff Summary

A1 HUMAN angles → A2 ROBOT angles → A3 PWM.
A2에서 처음 robot mounting/table orientation/zero/direction/offset/scale/limit/
shortest-angle/rate/FK/workspace/collision을 적용한다.
새 HumanForearmTarget으로 소비부를 명시적으로 연결하고 기존 anatomical elbow 매핑은 재사용하지 않는다.
기존 수직 설치 FLOOR_COLLISION/FK는 수평 설치 기구에 맞는지 재검토한다.
A1 semantic sign을 실제 서보 방향 때문에 변경하지 않는다.

현재 SERVO_COUNT=6, ROBOT_MOTION_JOINT_COUNT=5는 gripper 제외 기존5관절이다.
새 비-gripper4관절+gripper1=총5와 혼동하지 않는다.
A3의 enum/PWM/HAL/PL/startup/record-playback/trace 채널 전환은 별도 인계한다.
사용자 실측 PWM calibration을 임의 기본값으로 덮어쓰지 않는다.

## 15. Agent2 Codex Prompt

[agent2_forearm_handoff_prompt.md](agent2_forearm_handoff_prompt.md)가 이번 교정 후 최종본이다.
이전 table 기준 전달 내용은 이 파일에서 교체했다.
