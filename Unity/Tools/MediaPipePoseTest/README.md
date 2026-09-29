# MediaPipe Pose Heavy 오른팔 좌표 POC

일반 RGB webcam 또는 MP4 영상에서 MediaPipe Tasks `PoseLandmarker` Heavy가 오른쪽 어깨·팔꿈치·손목을 얼마나 안정적으로 추적하는지 관찰한다. 입력 영상과 고정 축척의 3D 사영 창을 표시하고, 매 처리 프레임의 원본 image/world landmark 및 진단 값을 CSV에 기록한다. 기존 Pose/C/Unity 파이프라인과 연결하지 않는다.

## 환경 및 준비

- 확인한 환경: Windows, Python 3.10.11, `mediapipe==1.0.1`, `opencv-contrib-python==4.14.0.94`.
- 이 폴더의 `.venv`에만 패키지를 설치했다. 기존 `LivePose` 환경은 사용하지 않는다.
- 모델: Google 공식 [Pose landmarker (Heavy)](https://developers.google.com/edge/mediapipe/solutions/vision/pose_landmarker)의 `pose_landmarker_heavy.task`.
- 모델 원본 URL: <https://storage.googleapis.com/mediapipe-models/pose_landmarker/pose_landmarker_heavy/float16/latest/pose_landmarker_heavy.task>
- 이번에 받은 모델 크기: 30,664,242 bytes. SHA256: `64437AF838A65D18E5BA7A0D39B465540069BC8AAE8308DE3E318AAD31FCBC7B`.

현재 작업 셸에서는 `python` 명령이 인식되지 않았다. 설치된 Python 실행 파일은 `C:\Users\kccistc\AppData\Local\Programs\Python\Python310\python.exe`이다. 다른 PC에서 환경을 새로 만들 때는 해당 PC의 Python 3.10 실행 파일로 다음을 실행한다.

```powershell
cd D:\working\final_project\Unity\HumanMotionDigitalTwin\Tools\MediaPipePoseTest
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r .\requirements.txt
```

모델을 다시 받아야 할 때만 다음 명령을 사용한다.

```powershell
New-Item -ItemType Directory -Path .\models -Force | Out-Null
Invoke-WebRequest -Uri 'https://storage.googleapis.com/mediapipe-models/pose_landmarker/pose_landmarker_heavy/float16/latest/pose_landmarker_heavy.task' -OutFile .\models\pose_landmarker_heavy.task
```

## 실행

```powershell
cd D:\working\final_project\Unity\HumanMotionDigitalTwin\Tools\MediaPipePoseTest
.\.venv\Scripts\python.exe .\mediapipe_pose_test.py
```

`--video`를 생략하면 webcam index 0을 사용한다. `--camera 1`, `--min-landmark-confidence 0.5`, `--no-3d`를 선택적으로 지정할 수 있다. 카메라에 1280×720을 요청하고, 실제 열린 해상도를 콘솔에 출력한다. 요청한 해상도를 지원하지 않아도 실제 프레임 크기로 계속 처리한다.

1440×720 side-by-side 시연 영상에서 왼쪽 720×720 사람 영상만 처리하는 예:

```powershell
.\.venv\Scripts\python.exe .\mediapipe_pose_test.py --video 'D:\경로\example6_thumb_agent1_xyz_20hz.mp4' --crop-left 720 --save-overlay
```

`--crop-left`를 생략하면 MP4 전체 프레임을 처리한다. `--crop-left 720`을 주면 왼쪽 720 pixel만 MediaPipe에 전달하고, 화면과 overlay MP4도 그 영역만 사용한다. 원본 오른쪽 XYZ 시각화는 처리하지 않는다. crop 너비가 입력 너비보다 크면 오류로 종료한다. 영상의 마지막 프레임 뒤에는 자동으로 CSV와 MP4를 닫는다.

`VIDEO` 모드의 `detect_for_video()`를 사용한다. 매 프레임을 동기적으로 처리하므로 한 CSV 행과 화면이 같은 입력 프레임을 가리킨다. Webcam에서는 `perf_counter()` 기준 timestamp를, MP4에서는 0부터 시작하는 `video_frame_id / source_fps` 기준 timestamp를 사용한다. MediaPipe timestamp는 이전 값보다 최소 1 ms 크게 만들어 단조 증가를 보장한다. MP4 파일의 FPS metadata가 없으면 잘못된 source time으로 분석하지 않도록 시작 전에 중단한다. 현재 방식은 고정 FPS 영상을 전제로 한다. MediaPipe의 기본 내부 tracking을 이용하며 별도 EMA, Kalman, deadband, HOLD, 각도 필터는 사용하지 않는다.

MP4는 CPU가 처리할 수 있는 속도로 순서대로 분석한다. CSV의 `fps`와 화면의 `Proc FPS`는 실제 처리 속도이고, `source_fps`와 화면의 `Source FPS`는 MP4 metadata의 원본 FPS이다. `timestamp_sec`은 프로그램 시작 후 경과한 실제 시간, `video_time_sec`은 파일 안의 영상 시간이다. `--save-overlay`의 출력 FPS는 MP4 입력 시 원본 FPS를 사용한다. Webcam에서는 장치가 보고한 FPS를 사용하고, 없으면 30 FPS를 사용한다.

화면은 좌우 반전하지 않는다. 즉 MediaPipe의 `RIGHT_*`는 사람의 해부학적 오른쪽이며, 화면의 오른쪽을 뜻하지 않는다. image `x,y`는 영상 폭·높이에 대해 정규화된 값이다. world `x,y,z`는 공식 API의 엉덩이 중앙 기준 미터 단위 추정값이다. 3D 창은 이 원본 world 좌표를 고정 축척으로 사영한 그림이며 추가 3D 복원이나 깊이 보정을 하지 않는다.

## 키 조작

| 키 | 동작 |
| --- | --- |
| `Q` 또는 `ESC` | 종료 및 CSV 닫기 |
| `1` | `NEUTRAL` (POSE A) |
| `2` | `FRONT` (POSE B) |
| `3` | `OUT` (POSE C) |
| `4` | `OUT_FOREARM_UP` (POSE D) |
| `5` | `FOREARM_DOWN` (POSE E) |
| `6` | `IN` (POSE F) |
| `0` | pose marker 지우기 |
| `R` | pose marker를 지우고 다음 프레임의 `session_event`에 `RESET` 기록 |
| `V` | 3D 사영 창 표시/숨기기 |
| `Space` | 일시정지/재개. 정지 중에는 새 프레임을 읽거나 CSV·overlay에 기록하지 않음 |

키 입력은 화면을 표시한 다음 읽으므로 marker와 `RESET`은 **다음 처리 프레임부터** CSV에 반영된다. 숫자 marker는 다른 키를 누르거나 지울 때까지 유지된다.

## CSV

실행마다 `logs/mediapipe_pose_YYYYMMDD_HHMMSS_ffffff.csv`가 생성된다. 끝의 마이크로초는 같은 초에 다시 실행해도 CSV 이름이 겹치지 않게 한다. `timestamp_sec`, `frame_id`, 처리 속도 `fps`, `source_type`, MP4의 `source_fps`·`video_frame_id`·`video_time_sec`, `pose_marker`, `session_event` 뒤에 양쪽 어깨·팔꿈치·손목과 양쪽 thumb/index/pinky의 image/world `x,y,z,visibility,presence`가 기록된다. Webcam 행에서 영상 전용 열은 빈 값이다. 그 외에 다음 진단 열이 있다.

- `upper_arm_d*` = `RIGHT_ELBOW - RIGHT_SHOULDER`의 world 좌표 차이.
- `forearm_d*` = `RIGHT_WRIST - RIGHT_ELBOW`의 world 좌표 차이.
- `upper_arm_n*`, `forearm_n*` = 각 벡터의 단위 벡터.
- `elbow_bend_deg` = `Shoulder-Elbow-Wrist` 사이의 기하학적 각도. 로봇 관절 명령이 아니다.
- `landmark_valid` = 오른쪽 어깨·팔꿈치·손목의 image와 world 좌표가 모두 유한하고, 제공된 `visibility` 및 `presence`가 각각 지정한 임계값 이상이면 1, 아니면 0.

모델이 점을 반환했지만 confidence가 낮은 경우에도 **원본 좌표와 파생 벡터를 그대로 기록**하고 `landmark_valid=0`으로 구분한다. 점이 아예 없으면 좌표/벡터 칸은 빈 값이며 `landmark_valid=0`이다. 제공되지 않는 `visibility`나 `presence`도 빈 값이다. 이전 프레임 값을 복사하지 않는다. `fps`는 연속으로 처리된 프레임의 읽기 시각 간격으로 계산한다.

`--save-overlay`를 사용하면 같은 `logs/`에 `mediapipe_overlay_YYYYMMDD_HHMMSS_ffffff.mp4`가 생성된다. 이 MP4에는 처리 영역의 skeleton과 수치 panel이 포함되며, 별도 3D 창은 포함되지 않는다.

## 사람 동작 시험

카메라 정면에서 상체와 오른팔 전체가 프레임에 들어오게 한다. 각 자세를 잠시 유지하면서 `1`~`6` marker를 찍고, 천천히 `A → B → C → D → E → F`를 수행한다. `G`는 같은 동작을 천천히 연속 반복하여 좌표의 연속성과 복귀를 본다. 특히 `3`(C)에서 `4`(D)로 넘어갈 때 어깨·팔꿈치 world XYZ 및 `upper_arm_n*`가 대체로 유지되고, 손목 및 `forearm_n*`와 `elbow_bend_deg`가 실제 전완 변화에 맞게 움직이는지 본다. 다시 C로 돌아왔을 때 좌표가 근처로 복귀하는지도 확인한다. 정지 구간에서는 drift와 `landmark_valid=0` 구간을 확인한다.

MP4 분석에서도 C→D 구간의 `video_time_sec`를 기준으로 어깨·팔꿈치 world XYZ와 상완 벡터가 유지되는지, 손목·전완 벡터가 동작에 맞게 변하는지 확인한다. 정지 구간의 drift와 `landmark_valid=0` 구간을 함께 본다. 이 단계의 품질 판정은 실제 시연 영상의 overlay와 CSV를 검토한 뒤 내린다.

## 공식 자료

- [Pose Landmarker Python 가이드](https://developers.google.com/edge/mediapipe/solutions/vision/pose_landmarker/python)
- [모델 및 33개 landmark 번호](https://developers.google.com/edge/mediapipe/solutions/vision/pose_landmarker)
