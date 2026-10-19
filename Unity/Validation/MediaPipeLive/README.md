# MediaPipe Heavy Webcam → Unity RAW HumanArmDebug

## 실행

1. 기존 `HumanMotionDigitalTwin` Unity 프로젝트를 연다. 다른 Play Mode가 켜져 있으면 먼저 상단 Play 버튼으로 정지한다.
2. Project 창에서 `Assets/Scenes/Demo_05_MediaPipeLive.unity`를 더블클릭한다. 기존 Scene의 변경 저장 여부가 나오면 의도한 사용자 변경만 저장한다.
3. Unity 상단 **▶ Play**를 누르고 **Game** 탭을 연다. 처음에는 `UDP Connected: False`가 정상이다.
4. PowerShell에서 아래 송신기를 실행한다. 카메라는 Python 한 프로세스만 사용한다. 기존 Webcam 테스트 프로그램이 카메라를 사용 중이면 먼저 종료한다.

```powershell
& 'D:\working\final_project\Unity\HumanMotionDigitalTwin\Tools\MediaPipePoseTest\Start-MediaPipeLive.ps1'
```

직접 실행할 수도 있다. PowerShell 정책으로 `.ps1` 실행이 막히면 이 명령을 사용한다.

```powershell
& 'D:\working\final_project\Unity\HumanMotionDigitalTwin\Tools\MediaPipePoseTest\.venv\Scripts\python.exe' -B 'D:\working\final_project\Unity\HumanMotionDigitalTwin\Tools\MediaPipePoseTest\mediapipe_pose_test.py' --camera 0 --udp --udp-host 127.0.0.1 --udp-port 5055 --no-3d
```

5. Game 창에서 `UDP Connected: True`, 증가하는 Frame과 Receive fps를 확인한다. 왼쪽은 해당 inference 입력 영상의 축소 미리보기, 오른쪽은 같은 packet의 양팔 world XYZ이다. 영상과 좌표를 반전하지 않는다. 해부학적 Left는 청록색, Right는 주황색이다.
6. 양팔 벌리기 → 팔꿈치 굽히기 → 내리기를 수행하여 UpperArm과 Forearm의 독립적인 움직임, 지연을 확인한다.
7. Unity Game 창에 초점을 두고 Q/Esc 또는 Quit 버튼으로 Play Mode를 종료한다. **Python 카메라 창에서도 Q/Esc를 눌러 송신기를 별도로 종료한다.** Python Space는 추론을 일시정지하므로 Unity는 0.75초 후 disconnected로 바뀌며 자세를 지운다.

카메라 선택: 실행 스크립트에 `-Camera 1`, 또는 직접 명령에 `--camera 1`. 포트 변경은 스크립트 `-Port`와 Scene root의 `UdpMediaPipePoseSource.port`를 동일하게 지정한다. 기본 loopback 연결에는 외부 네트워크 개방이 필요 없다. `UDP bind failed`가 뜨면 다른 Live Scene/프로세스가 5055를 점유하고 있는지 확인한다.

## 구조와 보존 범위

`Webcam → 기존 MediaPipe Pose Heavy detect_for_video → PoseUdpSender → UDP → UdpMediaPipePoseSource(IPoseSource) → 기존 PoseFrame → 기존 HumanArmDebugController`

Unity에서 MediaPipe inference와 Webcam 장치 열기를 하지 않는다. 기존 Heavy 모델과 Tasks VIDEO 모드를 그대로 재사용한다. MediaPipe 내부 tracking 외에 추가 smoothing, HOLD, identity correction, IK, Robot C, M0/M1, calibration은 없다. CSV 분석용 기존 각도 계산은 기존 Python 도구에 그대로 남아 있지만 UDP packet과 Unity 경로에서 사용하지 않는다.

`Demo_05`의 편집 시 Hierarchy는 `MediaPipeLive` root 하나이며 source/controller/view가 연결되어 있다. Play 시 LeftArm/RightArm의 Shoulder/Elbow/Wrist, UpperArm/Forearm 및 표시 카메라가 생성된다.

기존 좌표 변환은 그대로 `Unity = translation + scale × (MediaPipe.x, -MediaPipe.y, -MediaPipe.z)`. 기본 translation=0, scale=1, 1 unit=1m. 기존 Controller Inspector에서 변경 가능하다.

## UDP 계약 v1

- 기본 수신: IPv4 UDP `127.0.0.1:5055`, 한 inference frame당 UTF-8 JSON datagram 하나. 기본 사용 범위는 같은 PC이다.
- `version`: 1
- `session_id`: 송신 실행마다 새로운 UUID. 송신 재시작의 frame 0 허용에 사용한다.
- `frame_id`: 0부터 증가하는 inference frame 번호. Webcam 장치의 하드웨어 frame 번호는 아니다.
- `timestamp`: `capture.read()` 반환 직후의 Unix UTC seconds(double). 카메라 노출 시각이나 MediaPipe 내부 timestamp가 아니다.
- `source_fps`: 기존 도구에서 측정한 처리 주기 FPS. 첫 frame은 0.
- `valid`: 아래 여섯 점의 유한 XYZ와 visibility/presence ≥0.5를 모두 만족하는지 표시한다. false여도 유한 좌표는 그대로 적용한다.
- `left_shoulder`, `left_elbow`, `left_wrist`, `right_shoulder`, `right_elbow`, `right_wrist`: 각각 `{ "xyz": [x,y,z], "visibility": score, "presence": score }`.
- `xyz`: MediaPipe world 원본 미터 좌표. 해당 점이 없거나 비유한 좌표이면 빈 배열 `[]` → Unity NaN → 해당 점/연결 링크 숨김. 검출 실패 frame도 전송한다.
- visibility/presence 미제공·비유한 값은 `-1`로 명시한다. 0이나 신뢰도 높은 값으로 대체하지 않는다.
- `preview_jpeg`: 같은 inference 입력 영상의 폭 320px JPEG(base64), quality 50. 크기 축소는 영상에만 적용한다. 추가 overlay를 그리기 전에 만든다. encoding 실패나 전체 packet이 60,000byte를 넘으면 빈 문자열이고 UI는 이전 영상을 표시하지 않는다.

수신은 배경 thread, PoseFrame 이벤트와 Texture 갱신은 Unity main thread에서 실행한다. 대기 packet은 최대 32개이며, 한 render frame에 새 packet이 여러 개 도착하면 가장 최근 frame만 표시한다. 오래된 frame을 차례로 재생하여 지연을 누적하지 않는다. 좌표 자체는 보간/변경하지 않는다. 같은 session의 중복·역순, 종료된 session, 잘못된 schema 및 오래된 packet은 적용하지 않고 Rejected 수에 반영한다. 신뢰된 로컬 송신기 한 개 사용을 전제로 한다.

`timeoutSeconds` 기본 0.75초. 수신 경과 시간이 이를 넘으면 영상과 자세를 지우고 disconnected로 표시한다. capture timestamp가 이미 timeout보다 오래된 packet도 제외한다. 이는 신뢰도 보정/HOLD가 아니라 오래된 통신 데이터 표시 방지이며, Inspector에서 조정 가능하다.

## UI 지표

- UDP Connected: 최근 유효한 packet 수신 여부. UDP 자체의 연결/handshake를 의미하지 않는다.
- Frame: 마지막 수락한 frame id.
- Receive fps: 최근 약 1초간 수락한 packet 수/시간. Unity render FPS와 다르다.
- Packet age(ms): 마지막 수락 packet이 로컬 socket에 도착한 뒤 경과 시간(monotonic clock).
- Capture-to-now(ms): capture.read 반환 timestamp부터 현재까지의 시간. 같은 PC wall clock 기준이며 inference/송신/대기 시간을 포함하지만 카메라 내부 버퍼나 실제 화면 scanout 지연은 포함하지 않는다.
- Left/Right S/E/W visibility와 presence, valid, Missing XYZ, LOW CONFIDENCE를 표시한다. 낮은 confidence는 기존 Controller 색상으로만 반영한다.

## 검증 결과

2026-09-29:

- Python 실제 UDP 송신 단위 테스트 2개 PASS: 여섯 점의 원본 좌표·confidence 보존, JSON 크기, 미리보기, 미검출 표현.
- Unity 6000.3.24f1 / 기존 URP·Input System 설정을 복사한 독립 검증 프로젝트에서 컴파일 및 통합 테스트 PASS. Python 송신기로 생성한 fixture를 실제 UDP socket으로 보내 여섯 관절/네 링크, low-confidence RAW 유지, 영상 디코딩, 중복·역순·잘못된 packet 거부, 검출 실패, timeout 해제, 송신 재시작, 수신기 disable/enable 포트 재연결을 확인했다.
- 실제 Webcam index 0 + 기존 Heavy 모델 + UDP loopback: 20/20 frame 추론 및 전송 성공, 20 frame 모두 pose 검출. 약 8.39fps, capture.read 반환→UDP 수신 평균 53.20ms / 최대 115.23ms. 이 테스트의 수신기는 Python 검증 socket이며 Unity 실제 화면의 종단 지연 측정은 아니다.
- 사용자가 직접 팔을 움직이며 보는 **최종 Live 시각적 Acceptance Test는 아직 미확인**. 위 실행 절차로 확인해야 한다. 20fps를 강제하거나 보장하지 않는다.
- 기존 CsvVideoPoseSource, HumanArmDebugController, PoseFrame/MediaPipeCoordinateMapping, Replay view/parser, Demo_01~04, 기존 Replay 검증 코드의 SHA256 변경 없음. `protected-before.json`과 `protected-after.json` 참고.
- Git 원본, Robot C, M0/M1, calibration은 수정/연결하지 않았다.
- Unity batch 로그에는 이전 Replay 검증에서도 존재한 Editor Search index 시작 시 예외가 포함되어 있다. 새 코드 컴파일 및 UDP 테스트는 PASS로 완료했다.

근거: `unity-udp-validation.txt`, `unity-udp.log`, `webcam-smoke.json`. 테스트 코드는 `Tools/MediaPipePoseTest/test_pose_udp.py`, `test_webcam_udp.py`, `Assets/Editor/MediaPipeLiveValidation.cs`이며 자동으로 Webcam을 열거나 사용자 Scene을 전환하지 않는다. 명시적으로 실행할 때만 테스트한다.
