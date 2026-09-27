> 이 문서는 과거 인계 기록이다. 현행 5축 전용 수정 정책은 [../AGENTS.md](../AGENTS.md)를 따른다. 6축 보존 지침은 폐기되었다.

# Agent2 담당 Codex 전달용 최종 프롬프트 — Human Body 기준 5축

아래 블록은 교정된 Agent1의 실제 인터페이스와 호스트 테스트 결과를 기준으로 작성했다.
앞선 고정 table 기준 Agent1 계약을 대체한다.

```text
너는 이 C 로봇팔 프로젝트의 Agent2(Kinematics / Motion Engineer)다.
교정된 Agent1 HUMAN angle 출력을 사용해 5축 수평 설치 로봇의 A2 전환을 구현·검증하라.
현재 working tree/diff와 의존성을 먼저 확인하고 기존 변경을 보존하라.
Agent1 알고리즘·사람 각도 부호, 실측 PWM calibration, A3 PWM을 임의 변경하지 마라.
reset/pull/전체 작업본 복사, 별도 승인 없는 보드 flash/서보 구동을 하지 마라.
미확정 기구/설치/서보 측정값은 추측하지 말고 필요한 값을 명시하라.

[먼저 읽을 파일]
README_AGENT1_TEST.md
docs/agent1_forearm.md
include/common/robot_types.h
include/human_target_angle/forearm_mapping.h
include/human_target_angle/agent1_forearm_stage.h
src/human_target_angle/forearm_mapping.c 및 pose_hand.c
현재 src/robot_calibration/, src/integration/, include/output_controller/

[역할]
Agent1: HumanPose2D → relative XYZ → 양 어깨 기반 Human Body Frame → HUMAN angles.
Agent2: HUMAN angles → 설치/서보 보정/FK/safety → ROBOT angles.
Agent3: ROBOT angles → PWM 및 actuator/record/playback.
Agent1에서는 TableFrame이나 robot mounting 정보를 사용하지 않는다.
설치 방향/table orientation은 Agent2에서 처음 적용한다.
입력 6-landmark HumanPose2D와 36-byte UART packet은 유지된다.

[실제 공유 타입 — include/common/robot_types.h]
typedef struct {
    float elbow_roll_deg, elbow_pitch_deg;
    float wrist_pitch_deg, wrist_roll_deg, gripper_norm;
    uint32_t frame_id;
    uint8_t valid, elbow_roll_observable, hand_fresh;
} HumanForearmTarget;

기존 HumanJointTarget은 legacy 6축 A2/integration 때문에 남아 있다.
새 타입을 cast/alias하거나 base/shoulder/elbow 필드에 억지로 대입하지 마라.
현재 보드 pipeline은 아직 기존 agent1_stage_* 경로다.
새 agent1_forearm_stage_init(void), run(pose, side, dt), output()으로 명시적으로 연결하라.
init에는 설치값이 없다. 필요 시 forearm_mapping_init/update context API를 사용할 수 있다.
Vitis linked source 등록/ROBOT_TRACE getter 및 새 trace field 순서를 확인하라.
기존 getter와 PC 자동 로그 저장을 보존하라.

[새 로봇 매핑]
Motor0 elbow_roll  <- HumanForearmTarget.elbow_roll_deg
Motor1 elbow_pitch <- HumanForearmTarget.elbow_pitch_deg
Motor2 wrist_pitch <- wrist_pitch_deg
Motor3 wrist_roll  <- wrist_roll_deg
Motor4 gripper     <- gripper_norm
기존 base/shoulder/anatomical elbow 매핑을 재사용하지 마라.
예전 elbow_deg는 펴진 팔≈180°인 내각이다. 새 elbow_pitch는 다른 물리량이다.

[BodyFrame과 elbow 각도]
Body X = normalize(Shoulder_R-Shoulder_L), 기존 stable frame 필터 사용.
Body Y = CameraUp(0,1,0)을 X에 수직 투영·정규화.
Body Z = X×Y. Z를 Camera forward 쪽으로 강제 반전하지 않는다.
zero인 +Body Z를 항상 실제 인체 정면이라고 단정하지 마라.
Body Y는 CameraUp 기반 추정이므로 몸 숙임의 완전한 3D orientation을 관측하지 못한다.
공급된 Frame+팔을 같은 3D 회전으로 돌리는 수학적 불변성과 실제 Frame 추정은 구분한다.

F=normalize(Wrist-Elbow); fx=F·BodyX, fy=F·BodyY, fz=F·BodyZ.
elbow_roll=atan2(fx,fz):
  Body +Y 주위 방위각, +Z=0, +X 방향 양수, [-180,180), wrap.
  전완 자체의 비틀림이나 실제 servo angle이 아니다.
elbow_pitch=atan2(fy,hypot(fx,fz)):
  Body XZ 평면=0, +BodyY 위쪽 양수, [-90,90], no wrap.
A1은 이 라디안 계산을 도 단위로 변환하고 기존 EMA를 적용한다.

[손목]
H=normalize(finger midpoint-Wrist), S=normalize(Finger2-Finger1).
A=normalize(S를 F에 수직 투영).
wrist_pitch=atan2(A·(F×H),F·H), [-180,180), wrap.
전완과 손이 같은 방향이면0, A축 오른손 회전이 양수다.
기존6축 HUMAN wrist 부호를 보존했다. 항상 위쪽 굽힘이 양수라고 가정하지 마라.

wrist_roll은 F주위 오른손 회전, [-180,180), wrap.
N은 H×S를 F에 수직 투영한 normal에 부호 연속화/기존 EMA를 적용한 것이다.
h=sin(raw_elbow_roll)BodyX+cos(raw_elbow_roll)BodyZ,
R=normalize(project_perpendicular(cos(raw_elbow_pitch)BodyY-sin(raw_elbow_pitch)h,F)).
roll=atan2(F·(R×N),R·N), R과 N이 같으면 zero.
일반 자세에서는 R이 BodyY의 수직 투영이며, pole 근처는 마지막 raw elbow_roll을 사용한다.
기존 unwrap/raw spike 완화/roll zero/EMA를 사용한다.
Normal 부호180° 모호성, 라벨 교환, 손 geometry 퇴화의 한계가 남아 있다.
A1의 기존35° raw spike 완화를 물리적 motor rate limit으로 사용하지 마라.

gripper_norm: 0=CLOSE, 1=OPEN. 접촉/압력 보호는 A3 책임이다.

[유효성/특이점/시간]
return1=fresh major, 0=HOLD/duplicate, -1=invalid.
valid는 major geometry 목표 유효성이지 robot safety 승인/손 최신성 보장이 아니다.
frame_id는 마지막 fresh major 목표의 입력ID이며 HOLD는 이전ID를 유지한다.
major dropout/재구성/BodyFrame 실패는 실제 dt 누적0.35초까지 HOLD, 이후 invalid.
중복 frame은 재필터링/시간 aging하지 않는다. 입력 중단의 실제 timeout은 상위에서 처리하라.
finger missing이면 major는 계속 fresh, 손목/gripper만 유지한다.
손 초기값0/0/OPEN, hand_fresh=0을 새 측정으로 해석하지 마라.

q=hypot(fx,fz), q<0.02에 방위각 특이점 진입, q>=0.04에 이탈.
특이점에서 elbow_roll과 raw heading은 HOLD, pitch는 계속 갱신한다.
elbow_roll_observable=0. 시작부터 수직이면 roll0은 임시값이고 heading 관측 전 hand도 미갱신.
duplicate/major HOLD에서도 elbow_roll_observable=0, hand_fresh=0.
관측 기준 밖 near-pole 및 이탈 후 큰 angle 변화는 남아 있다.
A2에서 관측 불가/재진입/hand stale 정책을 명시하라.
yaw/손목 shortest-angle를 사용하되 실제 유한 서보 범위를 우회하지 마라.
elbow_pitch에는 circular unwrap을 적용하지 마라.

[A2 구현 범위]
physical zero/direction/offset/scale/min/max, rate limit, 새 FK/링크/축/workspace/
table-floor collision을 실제 새 수평 설치에 맞춰 검토·구현하라.
vertical installation assumptions must be reviewed.
기존 수직 설치 FLOOR_COLLISION/FK를 그대로 적용해도 되는지 검증하라.
안전검사를 끄거나 사람 각도를 왜곡해 거부를 피하지 마라.
servo 방향 때문에 Agent1 부호를 변경하지 마라.
실측값이 없으면 필요한 측정 목록을 보고하고 해당 실물 적용은 보류하라.

[현재 테스트·522 replay]
PC Debug -Werror 빌드와 CTest7/7, 기존 axis suite, viewer3/3 PASS.
신규 Vitis 전체/실물 구동은 NOT VERIFIED.
right-arm522프레임: fresh/valid522, invalid0, major HOLD0,
elbow roll 미관측1(frame171), hand HOLD9, gripper 전환23.
범위와 최대 shortest-angle frame delta(도):
  elbow_roll  -176.24~179.11, max59.65(frame32)
  elbow_pitch -84.59~16.69,  max12.85(frame509)
  wrist_pitch   5.29~81.18,  max10.96(frame511)
  wrist_roll -179.86~179.25, max23.59(frame34)
elbow roll wrap:36,38,90,96; wrist roll wrap:93,96,232,233.
31→32 BodyFrame 변화0.45°, 전완 방향 변화5.25°이며
수평 성분이 작아 방위각 변화가 크게 증폭된다. 단순360° wrap 버그로 처리하지 마라.
171→172 수직 이탈/raw jump 및 이후 필터 추종도 회귀 사례로 사용하라.
카메라 깊이 합성 교란에 민감하지만 실제 오차와 사람 움직임을 로그만으로 분리할 수 없다.

robot_arm에서:
cmake -S . -B build/agent1 -DAGENT1_ONLY=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/agent1 -j
ctest --test-dir build/agent1 --output-on-failure
./build/agent1/test_pose_csv etc/example_pose2d_1280x720_20hz.csv build/agent1/forearm_result.csv
python3 tools/analyze_forearm_replay.py build/agent1/forearm_result.csv --expect-frames 522
bash tests/human_target_angle/run_axis_tests.sh

AGENT1_ONLY는 A2 검증 빌드가 아니다.
A2 단위/통합 replay를 추가해522프레임의 거부 사유/saturation/제한 후 delta/FK/
collision/초기·종료·dropout·재진입/양쪽 wrap/수직/끝단 자세를 검증하라.
입력 timestamp는 약33/67ms로 파일명20hz라도 고정50ms가 아니다.

[Agent3 인계]
SERVO_COUNT는 현재6이다.
ROBOT_MOTION_JOINT_COUNT=5는 기존 gripper 제외5관절이며 새 총5서보와 다르다.
새 구성은 비-gripper4관절+gripper1이다.
JointCommand/PwmCommand, enum/index/config/HAL/PL wiring/startup,
trace/record/playback의 채널과 저장 포맷 변경을 담당자에게 전달하라.
실제 존재하는 파일을 확인하고 실측 PWM 값을 보존하라.

최종 보고: 확정 매핑/축 규약, 수정 파일, 실제 테스트와522 replay 통계,
미확정 실측값, 보드 검증 여부, Agent3 인계사항을 구분하여 보고하라.
```
