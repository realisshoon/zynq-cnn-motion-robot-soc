# MediaPipe HumanArmDebug 재생 검증

## 실행

1. Unity Hub에서 `D:\working\final_project\Unity\HumanMotionDigitalTwin`을 연다.
2. 스크립트 컴파일 완료 후 Project 창에서 `Assets/Scenes/Demo_04_MediaPipeHumanArmDebug.unity`를 더블 클릭한다. 또는 `Tools > Human Motion > MediaPipe Debug > Open Replay Scene`을 선택한다.
3. 상단 ▶ Play 버튼을 누르고 Game 탭을 클릭한다. 영상과 skeleton이 자동으로 재생된다.
4. Game 창을 16:10 비율 또는 충분한 크기로 보면 상태와 모든 버튼을 동시에 확인할 수 있다.
5. Hierarchy의 `MediaPipeHumanArmDebug`를 선택하면 CSV/video 경로와 좌표 변환 설정을 Inspector에서 확인할 수 있다. Play 중 생성된 LeftArm/RightArm의 Shoulder/Elbow/Wrist를 펼치면 XYZ를 볼 수 있다.

## 조작

- Space 또는 Play / Pause: 영상과 pose를 함께 정지/재개.
- R 또는 Replay: 처음부터 실제 시간 1배속 재생.
- ← / → 또는 frame 버튼: pause 후 이전/다음 한 프레임. seek 완료 시에만 pose 적용.
- 12.00s / 14.65s / 28.70s 버튼: 해당 CSV/영상 frame으로 이동 후 pause.
- Front / side: skeleton을 보는 카메라만 변경. 입력 XYZ는 불변.
- Q / Esc / Quit: Editor Play Mode 종료. 빌드에서는 애플리케이션 종료.

## 입력과 좌표

- 영상: 프로젝트 기준 `../../src/동영상/example6_thumb_agent1_xyz_20hz.mp4`.
- CSV: `Tools/MediaPipePoseTest/logs/mediapipe_pose_20260929_123649_970082.csv`.
- source_fps=20, video_frame_id=0..852, video_time_sec=0..42.60. 마지막 frame의 표시 시간을 포함한 영상 길이는 약 42.65초.
- 기본 변환: `Unity=(MediaPipe.x, -MediaPipe.y, -MediaPipe.z)`, scale=1, translation=(0,0,0). 기본 1 unit=1m.
- 축 선택, 축별 sign, scale, translation은 `HumanArmDebugController.coordinateMapping` 한 곳에서 변경한다. scale 이외의 개별 링크 길이 보정은 없다.
- 영상 자체에는 기존 Agent1 그림과 frame 번호가 구워져 있다. 이것은 영상 픽셀이며 이번 Scene의 계산 결과가 아니다. 영상에 인쇄된 1-based frame 번호와 UI의 0-based VideoPlayer/CSV 번호를 혼동하지 않는다.

## RAW 정책

visibility/presence가 0.5 미만이면 관절과 연결선을 노란색으로 표시한다. `landmark_valid=0`도 유한한 좌표이면 그대로 적용한다. identity 재정렬, smoothing, HOLD, angle 계산, retargeting은 없다.

NaN/빈 XYZ는 해당 관절과 연결선만 숨기고 Missing XYZ 개수를 표시한다. 이전 정상 좌표를 대신 보여주지 않는다. frame 번호가 유효하지만 CSV 행이 없으면 오류를 표시하고 재생을 멈춘다. frame 번호를 얻을 수 없을 때에만 video time의 nearest row를 사용하며 UI에 fallback 상태를 표시한다.

## 구조

`PoseFrame / IPoseSource → CsvVideoPoseSource → HumanArmDebugController`.

CSV 파서는 `PoseCsvTable`, 좌표 변환은 `MediaPipeCoordinateMapping`, 영상/비교 화면/조작은 `MediaPipeReplayView`이다. 이후 UDP source는 IPoseSource를 구현하면 되지만 이번에는 구현하지 않았다.

Play 시 hierarchy:

```text
MediaPipeHumanArmDebug
├─ LeftArm
│  ├─ Shoulder / Elbow / Wrist
│  └─ UpperArm / Forearm
└─ RightArm
   ├─ Shoulder / Elbow / Wrist
   └─ UpperArm / Forearm
DisplayCamera
HumanArmDebugCamera
```

관절 위치는 공통 좌표 변환 후 world position에 직접 넣는다. 링크는 LineRenderer의 두 endpoint로 정의한다. 부모 회전으로 손목이 이중 이동하지 않도록 각 관절은 팔 root의 형제로 배치한다.

## 검증 실행

`Tools > Human Motion > MediaPipe Debug > Run Replay Validation` 메뉴는 새 Scene에서 자동 검증 후 Play Mode를 종료한다. 기존 Scene에 저장되지 않은 변경이 있으면 Unity의 저장 확인 창이 먼저 표시된다. 테스트 결과는 `Validation/MediaPipeHumanArm/runtime-validation.txt`에 기록한다.

검증 범위: 853개 CSV 행의 6관절 RAW XYZ와 네 링크 endpoint, frame/time 검색, time fallback, 20fps 영상 metadata, 첫 frame, pause/resume, frame stepping, frame 240/293/294의 source 이상 구간, frame 574/721의 low-confidence 상태, 전체 실제 시간 재생.

별도 테스트 프로젝트는 `Tools/MediaPipeHumanArmValidation`에 있으며, 원본 Scene을 열어 바꾸지 않고 새 코드와 동일 Unity 버전에서 실행하기 위한 용도다. 실제 사용 프로젝트는 위의 HumanMotionDigitalTwin이다.

## 보호 대상

기존 `Demo_03_LivePose.unity`, RobotArm_L/R, ForearmArmController, Robot C DLL, M0/M1, calibration, Git 저장소는 변경하거나 연결하지 않았다. MediaPipe inference를 실행하지 않는다.

## 이번 실행 결과 (2026-09-29)

- Unity 6000.3.24f1의 별도 검증 프로젝트에서 기본 렌더링과 원본 프로젝트의 URP/Input System 설정으로 각각 실행했다.
- 최종 검사 PASS: 모든 853행의 RAW 위치/링크 endpoint, frame/time lookup 및 frame unavailable fallback, pause/resume, exact seek, 단일 frame 이동.
- 전체 재생은 42.600초였다. 처음 frame 0과 이후 852개 적용 이벤트에서 순서대로 마지막 frame 852까지 진행했다. 최종 검증은 재생 이벤트마다 실제 `VideoPlayer.frame`과 `PoseFrame.frameId`의 일치도 검사했다.
- 알려진 anomaly frame 240/293/294를 정확히 선택했고 CSV geometry를 그대로 적용했다. source anomaly를 보정하지 않았다.
- low-confidence frame 574/721에서 valid=0, 낮은 confidence 표시와 RAW geometry 유지를 확인했다. 저장된 렌더링 이미지에서 노란색 관절/링크를 확인했다.
- `runtime-validation.txt`는 최종 자동 검증 결과, `skeleton-240.png`/`skeleton-574.png`와 대응 video 이미지는 렌더링 확인 자료다. 로그에는 검증 프로젝트 초기화 중 발생한 Unity Search index 예외와 사용하지 않는 terrain shader 경고가 포함되어 있으나, 새 코드의 컴파일/재생 검증은 통과했다.
- 보호 Scene SHA256은 작업 전후 동일: `E9316D7B6FE0822881AF0185E51CFD6E1CC24BAE4007DAC815699102C89BF371`.
- 이 검사는 CSV→Unity 재현 정확도를 확인한다. MediaPipe의 실제 3D 추정 정확도를 인증하는 검사는 아니다. 키보드 단축키는 컴파일과 연결을 확인했으며 사용자 Editor에서 실제 키를 누르는 수동 조작 검사는 별도다.
