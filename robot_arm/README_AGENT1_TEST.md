# Agent1 5축 테스트 치트시트

`robot_arm/`에서 실행. Pose2D → 기존 relative 3D → 새 Forearm/Wrist 각도.
입력 6개 landmark/36-byte UART는 그대로다. **아직 실물 5축 통합 완료가 아니다.**
기존 `agent1_stage_*`와 보드 A1/P3 trace는 6축 경로를 유지한다.
새 경로는 `forearm_mapping_*` / `agent1_forearm_stage_*`이며 Agent2/3 연결은 후속 작업이다.

| 새 모터 | Agent1 필드 | 의미 |
|---|---|---|
| M0 elbow_roll | forearm_yaw_deg | 테이블 법선 주위 방위각; 전완 자체의 비틀림 아님 |
| M1 elbow_pitch | forearm_pitch_deg | 테이블 평면 기준 고도각; 기존 elbow 내각 아님 |
| M2 wrist_pitch | wrist_pitch_deg | 전완에 대한 손의 굽힘 |
| M3 wrist_roll | wrist_roll_deg | 전완 축 주위 손의 회전 |
| M4 gripper | gripper_norm | 0=CLOSE, 1=OPEN |

Agent1에는 서보 offset/direction/limit/PWM을 넣지 않는다.

## TableFrame

실측 camera-space 법선 `up`과 기준 정면 `forward`를 공급한다.
Z=정규화한 up, X=forward를 테이블에 투영·정규화, Y=Z×X. X×Y=Z인 오른손 기저다.
X는 실제 로봇 neutral 전완의 정면을 카메라 공간에 표현한 방향을 권장한다.
yaw: +X=0°, +Z 오른손 방향이 양수, [-180,180).
pitch: 평면=0°, 위쪽 양수, [-90,90].

`--demo-table`은 **실측 아님**: X=카메라 +Z(멀어짐), Y=카메라 +X, Z=카메라 +Y(영상 위).
실측값이 없으므로 실제 설치 기준이라고 가정하지 않는다. 자세/카메라가 바뀌면 다시 보정한다.
`--table up_x up_y up_z forward_x forward_y forward_z`로 실측값을 명시할 수 있다.
이 옵션은 사용자가 보정했다고 선언하는 것이지 실측 여부를 소프트웨어가 검증하는 것은 아니다.

## 빌드 / 회귀 테스트 — PC에서 실행 확인

```bash
cmake -S . -B build/agent1 -DAGENT1_ONLY=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/agent1 -j
ctest --test-dir build/agent1 --output-on-failure
bash tests/human_target_angle/run_axis_tests.sh
```

CMake에 정식 `AGENT1_ONLY` 모드를 추가했다. 기존 전체 빌드의 A2/A3 누락 파일 문제는
고치지 않았으며, 이 모드는 해당 타깃을 구성하지 않는다. 기존 `build/`와 별도 디렉터리다.
CTest 7개: 기존 mapping/body 회귀, 새 전완/손목/HOLD/EMA/gripper,
새 UART 522프레임, CSV replay, TRACE getter 컴파일·링크, replay 통계/범위 검사.
스크립트는 기존 mock/legacy UART 회귀도 실행한다. Debug를 사용해야 assert 검증이 살아 있다.

뷰어 신/구 CSV 로딩과 panel 렌더 테스트도 실행 확인했다(CSV 생성 후):

```bash
PYTHONDONTWRITEBYTECODE=1 MPLCONFIGDIR=/tmp/robot-arm-mpl .venv/bin/python \
  tests/human_target_angle/test_forearm_viewer.py \
  build/agent1/forearm_result.csv etc/example_agent1_result_1280x720.csv
```

## 기존 Pose2D CSV → 새 결과 — 실행 확인

```bash
./build/agent1/test_pose_csv \
  etc/example_pose2d_1280x720_20hz.csv \
  build/agent1/forearm_result.csv --demo-table
python3 tools/analyze_forearm_replay.py \
  build/agent1/forearm_result.csv --expect-frames 522
```

이 유틸리티는 오른팔 입력용이다. 왼팔 제어는 API의 `POSE_ARM_LEFT`를 사용한다.
입력 추출을 다시 할 필요 없다. 출력에는 옛 base/shoulder/elbow 열이 없고 새 5개 필드가 있다.
`target_valid`, `yaw_observable`, `hand_fresh`, `table_calibrated`, `target_frame_id`도 확인한다.
`frame_id`는 입력 프레임, `target_frame_id`는 마지막 fresh major 목표의 프레임이다.

## 뷰어 — 아래 짧은 미리보기 실행 확인

현재 `etc/example.mp4`는 없다. 동일 입력의 기존 overlay 영상을 사용한다.

```bash
MPLCONFIGDIR=/tmp/robot-arm-mpl .venv/bin/python \
  tools/agent1_video_xyz_viewer_v3.py \
  --video etc/example_pose_overlay_1280x720_20hz.mp4 \
  --result-csv build/agent1/forearm_result.csv \
  --pose2d-csv etc/example_pose2d_1280x720_20hz.csv \
  --output build/agent1/forearm_preview.mp4 \
  --output-fps 20 --max-seconds 0.3 --panel-size 720 --elev 10 --azim -75
```

전체 영상을 만들려면 `--max-seconds 0.3`을 제거한다(전체 길이 렌더는 이번에 미실행).
테이블 XYZ 화살표는 팔꿈치에서 시작하고 보라색 화살표는 팔꿈치→손목이다.
DEMO 표시를 실물 보정 완료로 읽으면 안 된다. 기존 CSV는 LEGACY로 구별한다.
CSV를 다시 만들어도 기존 mp4는 자동 갱신되지 않는다.

## UART — 옵션 확인, 실물 실행 NOT VERIFIED

Windows PowerShell에서 실제 저장소의 `robot_arm`으로 이동한 뒤 사용한다. COM9는 예시다.
새 Agent2/3를 보드에 통합하기 전에는 **기존 6축 A1/P3 로그**가 나오는 것이 정상이다.
새 5축 실물 동작 확인 명령으로 오해하지 말 것. 이번 작업에서 보드/서보는 구동하지 않았다.

```powershell
py .\pc\send_pose_uart_with_video.py --port COM9 --baud 921600 --csv .\etc\example_pose2d_1280x720_20hz.csv --video .\etc\example_pose_overlay_1280x720_20hz.mp4 --hz 20 --log-dir "C:\Users\kccistc\Downloads\로그 파일"
```

Pose2D 추출기/전송기/로그 저장 기능은 변경하지 않았다. `--help`로 옵션 존재를 확인했다.

## 빠른 이상 확인

- 시작부터 수직이면 yaw=0은 임시값이며 `yaw_observable=0`, 손도 기본값/`hand_fresh=0`이다.
- 수직 근처 yaw는 HOLD하지만, 특이점을 벗어난 뒤 큰 변화가 생길 수 있다.
- 이번 DEMO replay: invalid 0, yaw 미관측 1, hand HOLD 9; yaw 최대 변화 72.10°/frame.
  테스트 PASS는 모터 직결이 안전하다는 뜻이 아니다. A2의 속도제한/재진입 정책이 필요하다.
- 각도 차이는 yaw/손목에서 shortest-angle로 비교한다. pitch에는 wrap을 적용하지 않는다.
- 입력 파일명은 20 Hz지만 timestamp 간격은 약 33/67 ms이다. replay는 실제 timestamp를 쓴다.
- 자세한 규약/검증/한계: [agent1_forearm.md](docs/agent1_forearm.md).
- 다음 담당자 전달: [Agent2 5축 프롬프트](docs/agent2_forearm_handoff_prompt.md).
