# Agent1 Human Body 기준 5축 테스트

Agent1 outputs HUMAN angles, not ROBOT servo angles.

robot_arm/에서 실행한다. 입력 → relative XYZ → 양 어깨 기반 Human Body Frame
→ 사람 elbow/wrist/gripper 각도 순서다. 로봇 설치 방향과 서보 보정은 Agent2 책임이다.

입력 6개 landmark: Finger1, Finger2, Elbow, Wrist, Shoulder_L, Shoulder_R.
HumanPose2D와 기존 36-byte UART packet, 추출기, 로그 저장 기능을 유지한다.

## 좌표·각도 규약

- Body X: 해부학적 Shoulder_L → Shoulder_R(기존 시간 필터 적용).
- Body Y: Camera up=(0,1,0)을 X에 수직 투영·정규화한 방향.
- Body Z: X×Y. 카메라 forward 쪽으로 강제 반전하지 않는다.
- Body Y는 몸 위쪽의 추정값이다. 몸 숙임을 포함한 임의의 3D 몸 회전을 완벽히 추정하지 못한다.

| 사람 출력 | 의미 |
|---|---|
| elbow_roll_deg | Body +Y 주위 방위각; +Z=0°, +X 쪽 양수, [-180,180), wrap |
| elbow_pitch_deg | Body XZ 평면 기준 고도각; +Y 위쪽 양수, [-90,90], no wrap |
| wrist_pitch_deg | 손·전완 일직선=0°, 전완에 수직 투영한 Finger1→Finger2 축 주위 양수, [-180,180) |
| wrist_roll_deg | 전완 축 주위 오른손 회전; Body/전완 기준 normal과 손 normal 일치=0°, [-180,180) |
| gripper_norm | 0=CLOSE, 1=OPEN |

elbow_roll은 전완 자체의 비틀림이 아닌 방위각이다.
elbow_pitch는 기존 팔꿈치 내각(펴진 팔≈180°)과 다르다.
손목 pitch의 부호는 기존 6축 사람 각도 규약을 유지한다. 항상 '화면 위쪽=양수'인 값은 아니다.
손목 reference와 특이점 상세는 [규약/검증 기록](docs/agent1_forearm.md)을 참조한다.

## 빌드·회귀 — 실행 확인

    cmake -S . -B build/agent1 -DAGENT1_ONLY=ON -DCMAKE_BUILD_TYPE=Debug
    cmake --build build/agent1 -j
    ctest --test-dir build/agent1 --output-on-failure
    bash tests/human_target_angle/run_axis_tests.sh

CTest: 기존 mapping/body, 새 human forearm 기하·손목·회전 불변성·HOLD,
UART 522프레임, CSV replay, TRACE getter, 통계/범위 검사.
Debug로 실행해야 assert 검증이 활성화된다.
AGENT1_ONLY는 기존 정식 Agent1 전용 빌드 모드다. 전체 빌드의 A2/A3 누락 소스 문제는 별도다.

## CSV replay — 실행 확인

    ./build/agent1/test_pose_csv etc/example_pose2d_1280x720_20hz.csv build/agent1/forearm_result.csv
    python3 tools/analyze_forearm_replay.py build/agent1/forearm_result.csv --expect-frames 522

인수: input.csv output.csv [right|left], 기본 right.
출력은 새 5개 사람 각도/의도와 Body 축을 기록한다.
target_valid는 major 목표 유효성, elbow_roll_observable은 방위각의 현재 관측 여부,
hand_fresh는 손의 현재 갱신 여부다. 몸 축만 과거 값으로 남을 수 있으므로 body_frame_valid만으로
전체 목표를 유효하다고 판단하지 않는다.
frame_id는 입력 ID, target_frame_id는 마지막 fresh major 목표 ID이며 HOLD 때 이전 ID를 유지한다.

기존 잘못된 고정축 CSV는 다시 생성해야 한다. 이전 기준값 설정 옵션은 제거했다.
BodyFrame은 입력 어깨에서 자동 계산하며 사용자 설치 방향 입력을 요구하지 않는다.

## 뷰어 — 아래 미리보기 실행 확인

    PYTHONDONTWRITEBYTECODE=1 MPLCONFIGDIR=/tmp/robot-arm-mpl .venv/bin/python tools/agent1_video_xyz_viewer_v3.py --video etc/example_pose_overlay_1280x720_20hz.mp4 --result-csv build/agent1/forearm_result.csv --pose2d-csv etc/example_pose2d_1280x720_20hz.csv --output build/agent1/human_forearm_preview.mp4 --output-fps 20 --max-seconds 0.3 --panel-size 720 --elev 10 --azim -75

0.3초 제한을 제거하면 전체 길이를 렌더한다(이번 전체 길이 렌더는 NOT VERIFIED).
실제로 존재하는 동일 입력의 overlay 영상을 사용했다. etc/example.mp4는 현재 없다.
Body X/Y/Z 화살표와 Elbow→Wrist 벡터를 표시한다.
기존 6축 CSV는 LEGACY 표시로 읽고, 폐기된 고정축 5축 CSV는 재생성을 요청하며 거부한다.

    PYTHONDONTWRITEBYTECODE=1 MPLCONFIGDIR=/tmp/robot-arm-mpl .venv/bin/python tests/human_target_angle/test_forearm_viewer.py build/agent1/forearm_result.csv etc/example_agent1_result_1280x720.csv

위 3개 뷰어 회귀 테스트 실행 확인. CSV를 다시 만들어도 과거 mp4는 자동 갱신되지 않는다.

## UART — 옵션 확인, 실물 실행 NOT VERIFIED

Windows PowerShell에서 실제 저장소 robot_arm 폴더로 이동한 뒤 사용한다. COM9는 예시다.

    py .\pc\send_pose_uart_with_video.py --port COM9 --baud 921600 --csv .\etc\example_pose2d_1280x720_20hz.csv --video .\etc\example_pose_overlay_1280x720_20hz.mp4 --hz 20 --log-dir "C:\Users\kccistc\Downloads\로그 파일"

현재 보드 pipeline은 기존 agent1_stage_* / HumanJointTarget 경로다.
새 agent1_forearm_stage_* / HumanForearmTarget 소비부 연결은 Agent2 후속 작업이다.
따라서 UART 실행만으로 새 5축 보드 제어가 활성화되지는 않는다.

## 이상 확인 / 알려진 문제

- 전완이 Body Y에 가까우면 elbow_roll을 HOLD한다. 처음부터 수직이면 임시 0°와 관측 불가 flag.
- 수직에서 벗어난 후 큰 방위각 변화는 남는다. 522 replay 최대 59.65°/frame.
- 이번 결과: fresh/valid 522, invalid 0, major HOLD 0, 방위각 미관측 1, hand HOLD 9.
- angle wrap은 최단 각도 차로 비교한다. elbow_pitch에는 wrap을 적용하지 않는다.
- 입력 파일명은 20hz지만 timestamp 간격은 약 33/67ms이다. replay는 실제 timestamp를 사용한다.
- 몸 좌표 추정은 Camera up을 사용한다. 몸을 앞으로 숙이는 동작까지 회전 불변이라고 해석하지 않는다.
- 새 공유 타입은 include/common/robot_types.h의 HumanForearmTarget.
- [Agent2 전달용 최종본](docs/agent2_forearm_handoff_prompt.md).
