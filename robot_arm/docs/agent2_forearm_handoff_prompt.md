# Agent2 담당 Codex 전달용 최종 프롬프트

아래 블록 전체를 전달하세요. Agent1 실제 구현·호스트 검증 결과에 맞춘 계약입니다.

```text
너는 이 C 로봇팔 프로젝트의 Agent2(Kinematics / Motion Engineer)다.
Agent1에 추가된 전완 기반 5축 출력을 조사하고 새 수평 설치 구조의 A2 전환을 구현·검증하라.
기존 미커밋 변경을 보존하고 reset/pull/작업본 전체 복사를 하지 말라.
실측되지 않은 기구값/서보값은 추측하지 말고, 그 부분의 연결·실물 구동은 보류하라.
보드 flash/실제 서보 구동은 별도 승인을 받아라.

먼저 읽을 파일:
- README_AGENT1_TEST.md
- docs/agent1_forearm.md
- include/human_target_angle/forearm_mapping.h
- include/human_target_angle/agent1_forearm_stage.h
- src/human_target_angle/forearm_mapping.c, agent1_forearm_stage.c, pose_hand.c
- include/common/robot_types.h 및 현재 src/robot_calibration/, src/integration/
- include/output_controller/servo_config.h, servo_control.h 및 src/output_controller/
옛 docs/agent2_handoff_prompt.md는 6축 BodyFrame 분석용이므로 새 계약으로 사용하지 마라.

[현재 경계]
HumanPose2D 6개 landmark와 36-byte UART packet은 유지된다.
새 HumanForearmTarget은 별도 자료형이다. 기존 HumanJointTarget을 cast/alias하지 말라.
기존 agent1_stage_* 및 현재 보드 pipeline은 여전히 legacy 6축이다.
새 agent1_forearm_stage_init(up, forward, calibrated), run(pose, side, dt), output()으로
A2 소비부를 명시적으로 연결하라. 새 소스의 Vitis source 등록/ROBOT_TRACE 링크도 확인하라.
Agent1 구 API/getter와 PC 자동 로그 저장 복구는 보존하라.

실제 출력 선언:
typedef struct {
    float forearm_yaw_deg, forearm_pitch_deg;
    float wrist_pitch_deg, wrist_roll_deg, gripper_norm;
    uint32_t frame_id;
    uint8_t valid, yaw_observable, hand_fresh, calibrated;
} HumanForearmTarget;

[새 모터 대응]
M0 elbow_roll  <- forearm_yaw_deg (table normal 주위 azimuth; 전완 자체 roll 아님)
M1 elbow_pitch <- forearm_pitch_deg (table plane elevation)
M2 wrist_pitch <- wrist_pitch_deg
M3 wrist_roll  <- wrist_roll_deg
M4 gripper     <- gripper_norm
기존 base/shoulder/anatomical elbow 매핑을 M0/M1에 재사용하지 마라.
기존 elbow_deg는 펴진 팔 약 180도인 내각이다. 새 forearm_pitch와 다른 물리량이다.

[TableFrame / 전완]
Camera-space에서 실측한 table up과 neutral forearm forward 방향을 공급한다.
Z=normalize(up), X=normalize(forward를 Z 수직 평면에 투영), Y=Z×X; X×Y=Z.
TableFrame은 몸 자세에 따라 돌아가지 않는다. 카메라 이동 시 재보정한다.
카메라 좌표는 X 영상 오른쪽, Y 영상 위, Z 카메라에서 멀어짐이다.
Agent1 DEMO 설정은 X=(0,0,1), Y=(1,0,0), Z=(0,1,0), calibrated=0.
이것을 실제 테이블/로봇 정렬이라고 가정하지 마라. calibrated=1도 호출자의 선언이다.
F=normalize(Wrist-Elbow), fx=F·X, fy=F·Y, fz=F·Z.
yaw=atan2(fy,fx): +X=0, +Z 오른손 방향 양수, [-180,180), wrap.
pitch=atan2(fz,hypot(fx,fy)): 수평=0, 위쪽 양수, [-90,90], no wrap.

[Wrist / gripper]
H=normalize(finger midpoint-Wrist), S=normalize(Finger2-Finger1).
A=normalize(S에서 F 성분 제거).
wrist_pitch=-atan2(A·(F×H),F·H), [-180,180).
손과 전완이 일직선이면 0, 손 normal 쪽으로 굽히면 양수이다.
중립 roll에서는 테이블 위쪽 굽힘이 양수이며, roll되면 굽힘 평면도 같이 회전한다.
legacy wrist_pitch와 새 API의 부호 규약이 다름을 확인하라.
wrist_roll은 F 주위 오른손 회전, [-180,180), wrap.
N=전완에 수직 투영한 H×S의 normal(시간 부호 정렬/기존 EMA 적용).
R=normalize(project(cos(pitch_raw)Z-sin(pitch_raw)h, F 수직 평면)),
h=cos(yaw_raw)X+sin(yaw_raw)Y.
roll=atan2(F·(R×N),R·N), R=N이면 zero; 사용자 roll-zero offset도 적용될 수 있다.
평상시 R은 table up 투영, 수직 근처에는 이전 raw yaw를 사용한다.
기존 normal 부호 연속화/unwrap/raw spike 완화/EMA를 재사용한다.
손 normal 180도 모호성/손가락 라벨 뒤바뀜/퇴화 geometry는 남아 있다.
gripper_norm: 0=CLOSE, 1=OPEN. 압력/접촉 보호는 Agent3 책임이다.

[valid / 시간 / 특이점 계약]
return 1=fresh major, 0=HOLD/duplicate, -1=invalid.
frame_id는 마지막 fresh major target ID이며 HOLD 때 이전 ID를 유지한다.
valid는 major 목표의 유효성이지 실물 안전 승인/hand 관측 성공이 아니다.
major dropout은 dt 누적 0.35초까지 HOLD, 이후 invalid.
finger dropout은 wrist/gripper만 유지하고 major는 계속 갱신한다.
초기 손값은 0/0/OPEN, hand_fresh=0; 이를 새 관측으로 취급하지 마라.
yaw 수평 길이 q<0.02 진입, q>=0.04 이탈의 hysteresis.
특이점 내부 yaw는 HOLD, pitch는 계속 갱신, yaw_observable=0.
시작부터 수직이면 yaw=0은 임시값이고, heading 관측 전에는 hand도 갱신하지 않는다.
duplicate/major HOLD에서도 yaw_observable=0, hand_fresh=0이다.
극점을 지나 반대 방향으로 이탈할 때는 큰 yaw 및 roll reference 변화가 가능하다.
재관측 시 재진입 정책과 속도 제한을 검증하라. wrap 처리는 raw 숫자 차가 아니라
shortest-angle를 사용하되 물리적 유한 서보 범위를 우회하지 마라.
forearm_pitch에 circular unwrap을 적용하지 마라.

[A2 작업]
physical zero/direction/offset/scale/min/max/rate limit을 실제 새 장착 상태에 맞게 정한다.
기존 실측 PWM calibration을 임의 표준값으로 덮어쓰지 마라.
Agent1 semantic 부호를 서보 방향 때문에 다시 뒤집지 마라.
새 기구의 FK, 링크/축 정의, workspace, table collision을 검토한다.
vertical installation assumptions must be reviewed.
기존 수직 설치 FLOOR_COLLISION/FK/threshold가 새 수평 설치에도 맞는지 재검증하라.
안전검사를 끄거나 A1 각도를 왜곡해 충돌 거부를 피하지 마라.
calibrated=0/invalid/HOLD/hand stale/yaw singular의 소비 정책을 명시하고 테스트하라.
설치 치수나 실측 방향이 없으면 필요한 측정 목록을 보고하고 production enable을 보류하라.

[현재 검증과 재현]
GCC Debug -Werror clean build, CTest 7/7, 기존 axis suite, viewer 2/2 PASS.
ASan/UBSan 7/7 PASS(실행환경 ptrace 때문에 leak detection 제외).
신규 5축 Vitis 전체 빌드/실물 구동은 아직 검증하지 않았다.
DEMO 522프레임: fresh/valid522, invalid0, yaw 미관측1, hand HOLD9, gripper 전환23.
최대 shortest-angle frame delta: yaw72.10도(frame32), pitch12.97도,
wrist pitch10.96도, wrist roll32.45도. 안전한 servo 명령이라는 뜻이 아니다.
frame31 수직 진입/32 이탈과 단안 depth 민감도를 반드시 회귀 사례로 사용하라.
입력 timestamp 간격은 약33/67ms이며 파일명이20hz라도 항상50ms가 아니다.

robot_arm에서:
cmake -S . -B build/agent1 -DAGENT1_ONLY=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/agent1 -j
ctest --test-dir build/agent1 --output-on-failure
./build/agent1/test_pose_csv etc/example_pose2d_1280x720_20hz.csv build/agent1/forearm_result.csv --demo-table
python3 tools/analyze_forearm_replay.py build/agent1/forearm_result.csv --expect-frames 522
bash tests/human_target_angle/run_axis_tests.sh
AGENT1_ONLY는 A2 검증 빌드가 아니다. A2 수정 후 별도 A2 단위/통합 replay를 추가하여
522프레임의 acceptance/rejection 사유, saturation, 제한 후 frame delta, FK/table collision,
dropout/재진입, yaw/roll wrap, +/-수직, neutral/끝단 자세를 검사하라.

[Agent3 handoff]
A3 PWM/실측 calibration은 이번 A2 작업에서 임의 변경하지 말고 담당자에게 인계하라.
SERVO_COUNT는 현재6이다. ROBOT_MOTION_JOINT_COUNT=5는 기존 gripper 제외5관절을 뜻한다.
새 구성은 비-gripper4관절+gripper1=총5이며 두 숫자를 혼동하지 마라.
JointCommand/PwmCommand 필드, servo enum/index/config/HAL/PL 배선/startup,
debug trace/record-playback 채널/배열 길이와 저장 포맷 전환을 점검하도록 전달하라.
존재하지 않는 record 소스는 추측하지 말고 실제 파일과 의존성을 확인하라.

최종 보고: 확정한 A2 매핑/축 규약, 수정 파일, 실제 테스트 결과, 522 replay 통계,
실측이 필요한 미확정값, 보드 검증 여부, Agent3 전달사항을 구분해서 보고하라.
```
