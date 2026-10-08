# 영상 + 실제 UART 좌표·각도 뷰어

다운로드 폴더의 `stereo_xyz_viewer.py` / `.md`는 원본 그대로 보존했다.
그 원본은 좌우 영상과 별도 pose CSV를 입력받아 MP4를 만드는 도구다.
여기 있는 버전은 현재 보드의 `combined_uart.log`와 OBS 녹화를 직접 읽는
**독립적인 오프라인 HTML 뷰어**다. 느린 프레임별 Matplotlib/MP4 렌더링 대신
브라우저에서 시점 회전, 시간 이동, 좌표표와 각도 추이를 제공한다.

기존 `stereo_uart_monitor_guide.md`와 UART 명령 문자열은 변경하지 않는다.
`monitor_stereo_uart.py`는 기존 실행 명령으로 OBS 녹화도 자동 시작하고,
`q`/Ctrl+C 종료 후 영상 저장이 끝나면 통합 리뷰를 생성해 브라우저로 연다.
영상·3D·A1/A2 각도를 한 HTML 화면에서 함께 보며 별도 플레이어는 필요 없다.
화면 폭은 1430px, 패널은 690px로 고정한다. 작은 창에서는 가로 스크롤하며
영상은 664×373.5px 영역에서 원본 종횡비를 유지한다(필요시 검은 여백).
OBS 원본 MP4에 각도를 구워 넣는 방식이나 실시간 UART 뷰어는 아니다.
녹화기와 뷰어 자체는 COM 포트를 열거나 보드에 명령을 전송하지 않는다.
운영 펌웨어·필터·SD도 변경하지 않는다.

## 이번 PC 준비 상태

OBS 32.2.2 / WebSocket 5.7.4의 인증 연결 확인.
현재 OBS 장면에 `왼쪽`, `오른쪽` 두 캡처 소스가 함께 보이도록 구성돼 있다.
`websocket-client`, `imageio-ffmpeg`를 설치했고 호스트 GCC도 확인했다.
다른 PC에서 녹화를 쓰려면 다음을 먼저 실행한다.

```powershell
python -m pip install -r tools/stereo_vision/requirements_xyz_viewer.txt
```

뷰어는 Python 표준 라이브러리만 필요하다. 보드와 같은 C 삼각측량을
비교하려면 호스트 GCC가 필요하다. Windows에서는 PATH의 GCC 또는
`C:/msys64/ucrt64/bin/gcc.exe`를 찾는다. 다른 위치면 `--cc`를 사용한다.
GCC가 없으면 `--no-geometry`로 기록된 P3와 A1/A2만 볼 수 있다.

## 촬영 순서 — 기존 명령 그대로

작업 디렉터리는 `D:\Working\zynq-cnn-motion-robot-soc\robot_arm`이다.
OBS가 실행 중이고 인증된 WebSocket 서버가 켜져 있어야 한다.
비밀번호는 로컬 OBS 설정에서 읽으며 화면·기록 파일에 저장하지 않는다.

1. UART 터미널에서 **평소 쓰던 명령 하나만** 실행한다.

```powershell
python tools/stereo_vision/monitor_stereo_uart.py --left COM3 --right COM4 --interactive --allow-motion-commands --filter commands --format plain
```

2. 양쪽 UART 연결 후 OBS 녹화 시작을 확인하고 명령 입력 처리를 시작한다.
별도 녹화 터미널은 필요 없다. **`[OBS] recording:` 출력**을 확인한 뒤
실험을 시작한다. OBS 연결·녹화에 실패하면 경고를 출력하고 UART는 계속 실행한다.
그 경우 `session.json`의 `obs.status`가 `error`이며 영상 저장을 보장하지 않는다.
녹화 없는 실행은 위 명령 끝에 `--no-video`를 추가한다.

녹화 도구는 `r A`, `r S`, PWM 활성화,
색상 변경 등 어떤 UART 명령도 보내지 않는다. 모니터의 `q`로 세션이
종료되거나 Ctrl+C를 누르면 영상 저장·해시 확인과 OBS 녹화 경로 복원까지
기다린 뒤 종료한다. **`r S`는 로봇 추종 명령이지 모니터 종료가
아니므로 영상은 계속 저장된다.**
`q`도 로봇/PWM을 끄는 명령이 아니다. 실물 종료 절차는 기존 UART 안내를 따른다.

### 별도 녹화기 — 구형/외부 G 모니터용

통합되지 않은 외부 G 모니터를 쓸 때만, 별도 터미널에서 다음 대기를 먼저 실행한다.
자동 녹화가 켜진 운영 모니터와 중복 실행하지 않는다.

```powershell
python tools/stereo_vision/obs_session_recorder.py --watch captures
```

이미 실행 중이며 녹화기가 붙어 있지 않은 모니터에 붙이려면 세션 경로를 지정한다.

```powershell
python tools/stereo_vision/obs_session_recorder.py --session "captures/monitor_YYYYMMDD_HHMMSS_microseconds"
```

별도 녹화기의 자동 대기는 **한 세션**을 녹화한 뒤 종료한다. 다음 실험 때 다시 실행한다.
기본 녹화 상한은 통합/별도 모두 3,600초다. 상한에 도달하면 경고를 출력하고
녹화만 종료하며 UART는 계속 실행한다. 더 긴 녹화는 운영 모니터에 `--no-video`를
추가하고 별도 녹화기를 `--max-seconds 7200`처럼 실행한다.
기존 OBS 녹화가 진행 중이면 거부하고, 그 녹화는 중지하거나 덮어쓰지 않는다.
강제 프로세스 종료/PC 종료는 정상적인 `q`/Ctrl+C와 다르다. 해당 경우
OBS 녹화 상태와 녹화 디렉터리를 직접 확인한다.

## 저장 위치

```text
captures/monitor_.../
  combined_uart.log           기존 모니터가 저장하는 전체 로그
  host_commands.jsonl         기존 전송 명령 기록
  obs/
    YYYY-MM-DD HH-MM-SS.mp4    OBS 원본 녹화 (기존 OBS 형식 유지)
    recording.json            UTC/영상 시간, 장면 배치, 크기·SHA256, 복구 상태
    *.browser.mp4             원본이 MKV 등일 때 브라우저용 stream-copy MP4
  xyz_review/
    review.html               영상·2D·3D·각도 화면
    review.json               실제 연결된 프레임쌍과 측정값
    summary.json              집계, C 소스 해시, 계산 경계
    geometry_build.log        호스트 C 빌드 기록
```

영상은 UART와 **같은 세션 폴더**에 저장한다. 이 실행 위치에서는 D:다.
OBS의 평소 녹화 디렉터리는 녹화 중에만 바꾸고 정상 종료 후 복원한다.
사용자가 도중에 다른 디렉터리로 바꾸면 그 변경을 덮어쓰지 않는다.
기존 OBS 인코더, 프레임률, 장면과 오디오 설정은 변경하지 않는다.
따라서 현재 장면에 포함된 오디오는 기존 OBS 설정대로 녹화될 수 있다.
브라우저용 remux가 실패해도 원본은 보존하고 경고를 기록한다.

## 녹화 종료 후 뷰어 생성

기존 모니터 명령만 실행하면 종료 후 `xyz_review/review.html`이 자동 생성·열린다.
자동 리뷰는 실제 기록된 P3·A1/A2를 사용하며 GCC나 보정 소스 추정을 요구하지 않는다.
PAIR 로그가 없으면 생성을 건너뛴다. 각도 로그가 없으면 `--`로 표시하며
없는 측정값을 추정하거나 이전 값을 새 값으로 채우지 않는다.
OBS 영상이 없어도 좌표·각도 리뷰는 생성한다.
`--no-open-review`는 자동 열기만, `--no-review`는 생성도 끈다.
리뷰 오류는 `session.json`의 `xyz_review`와 `xyz_review_build.log`에 남으며
이미 저장한 UART/OBS 파일은 보존한다.

과거 세션을 수동 재구성하거나 C 삼각측량 후보도 비교할 때만 다음을 실행한다.
자동 생성 폴더와 겹치지 않도록 다른 출력 폴더를 사용한다.

```powershell
python tools/stereo_vision/stereo_xyz_viewer.py --session "captures/monitor_YYYYMMDD_HHMMSS_microseconds" --output-dir "captures/monitor_YYYYMMDD_HHMMSS_microseconds/xyz_geometry_review" --last-run
Invoke-Item "captures/monitor_YYYYMMDD_HHMMSS_microseconds/xyz_geometry_review/review.html"
```

`--last-run`은 마지막 **전송된 `r A`부터 그 뒤 첫 `r S`까지** 선택한다.
보드가 실제 승인했다는 뜻은 아니다. 전체 세션을 보려면 옵션을 뺀다.
출력 폴더가 이미 있으면 덮어쓰지 않는다. 새 `--output-dir`을 지정한다.
`--max-frames 30`으로 짧은 구간부터 확인할 수도 있다.
HTML 파일을 더블클릭하면 서버나 인터넷 없이 열린다.
`review.html#20`처럼 프레임 인덱스를 붙여 특정 지점에서 시작할 수 있다.

G 스냅샷의 보정값으로 재구성하려면 다음 옵션을 추가할 수 있다.

```powershell
--firmware-root "captures/g_uart_tuning_20261004/G/snapshot"
```

## 화면에서 구분하는 단계

1. **RAW:** CNN/색상 검출 원본 2D. 회색 점.
2. **PIX:** 보드가 실제 삼각측량에 사용한 2D. 관절별 색 점.
   뷰어는 PIX에 EMA/One Euro/칼만을 다시 적용하지 않는다.
3. **삼각측량 후보:** 선택한 펌웨어 소스의 C geometry와 calibration을
   직접 빌드·호출한 좌표와 재투영 오차. 주황색. 입력 승인과 별개다.
4. **P3:** 실제 보드가 기록한 3D 필터/매핑 결과. PG 승인 + age=0 +
   해당 관절의 fresh 플래그를 만족할 때만 초록색 새 좌표로 그린다.
   유효 마스크가 남아 있어도 손가락 fresh가 없으면 `held / not fresh`다.
   어깨는 현재 양쪽 PIX가 유효하고 재구성 geometry가 OK일 때만 신규로 표시한다.
5. **A1:** 실제 로그의 사람 기준 elbow/wrist 각도·gripper. 재계산하지 않는다.
6. **A2:** 실제 로그의 보정된 명령 후보·안전 거부 플래그.
   N/S와 R/V를 구분한다. 거부 후보는 적용된 서보 명령이 아니다.
   어느 값도 엔코더/실제 로봇 위치 피드백이 아니다.

세션·순번·프레임 번호가 모두 같은 RAW/PIX를 PAIR에 연결한다.
좌우 fid가 달라도 문제없으며 fid가 같다는 이유로 새로 매칭하지 않는다.
A1/P3/A2의 fid는 PAIR의 **왼쪽 fid**와 연결한다.
누락·충돌·세션 변경을 구분하고 이전 값을 새 입력으로 채우지 않는다.
삼각측량 후보를 제어 승인으로, 필터 출력을 물리 정답으로 간주하지 않는다.

P3/A1/A2가 없으면 기존 UART trace 설정이 켜져 있는지 확인한다.
화면 출력 필터가 `commands`여도 디스크의 전체 로그 저장은 유지된다.
보드 로그 자체에 값이 없으면 뷰어가 임의로 만들지 않는다.

## 영상 대응의 한계

OBS 출력 경과시간과 PC UTC를 0.5초마다 기록하고 UART **줄 수신 UTC**와
대응시킨다. HDMI/USB/인코딩/보드 처리 지연은 남아 있으며 하드웨어 노출
동기화나 CNN이 실제 처리한 동일 이미지임을 보장하지 않는다.
영상 시각 보정 입력은 대응 영상에 더하는 초 단위 값이다. 눈으로 같은
동작을 확인해 조정할 수 있지만 정확한 지연 추정에는 별도 표식 실험이 필요하다.
녹화 이전/이후, 녹화 pause 구간은 임의로 다른 영상 프레임에 매칭하지 않는다.

기본 소스 이름은 `왼쪽`/`오른쪽` 또는 Left/Right다. 장면 배치에서
회전·좌우 반전·crop·bounds 변환이 없고 카메라 전체 영상이 유지돼야 한다.
1280×720과 같은 종횡비의 캡처 크기는 전체 화면 리사이즈라는 가정하에
표시만 스케일한다. 이 가정은 실제 카메라 영상으로 확인해야 한다.
녹화 중 장면/배치 변화가 감지되거나 영상 크기가 다르면 잘못된 점을
덧씌우는 대신 좌표만 보여준다. 원본 합성 영상은 접이식 영역에서 따로 볼 수 있다.
장면 변경 검사는 2초 주기이며 매우 짧은 변경까지 검출한다고 보장하지 않는다.
3D 기본 표시 범위는 신규 P3를 기준으로 고정한다. P3가 전혀 없으면 후보
좌표 범위를 사용한다. 큰 거부 후보는 화면 밖일 수 있으나 좌표표에는 남는다.
카메라/광선 체크 시 카메라 원점까지 포함해 범위를 넓힌다.

## 검증 기록 (2026-10-05)

- 회귀 테스트 18개 통과: 프레임 ID 차이, 세션 변경, 충돌/누락,
  손가락 held, native C 투영·복원/Y 부호, OBS 상태 전이·기존 녹화 보호.
- 기존 실제 r A~r S 로그: 1,705쌍 / PG 승인 960 / 신규 P3 960 / A1 960으로
  기존 집계와 일치. 과거 구간의 대응 영상은 없어 좌표 전용 검증이다.
- 기존 픽셀 로그 분석 테스트 12개, 브라우저 시간 경계/pause/프레임 이동
  로직 검증 및 Chrome headless 화면 렌더링도 통과했다.
- 현재 OBS 3초 녹화: D: 저장, 92프레임/30fps/1280×720 디코딩,
  종료 후 녹화 OFF·기존 C: 녹화 디렉터리 복원 확인.
- 녹화 테스트는 가상 세션 manifest만 사용했고 COM/보드 명령은 전송하지 않았다.
  **실제 손 영상과 현재 보드 로그의 동시 정합 검증은 다음 촬영에서 필요하다.**
- 현재 OBS 로그에는 양쪽 캡처의 JPEG 디코딩 오류가 반복된다. 보드/HDMI 입력을
  켠 후 실제 움직이는 영상이 들어오는지 먼저 확인해야 한다. 녹화 파일 저장
  성공만으로 카메라 영상이 정상이라고 판단하지 않는다.
- MKV → 브라우저용 MP4 stream-copy도 별도 파일로 검증했다.

```powershell
python -m unittest discover -s tools/stereo_vision -p test_stereo_xyz_viewer.py -v
python tools/stereo_vision/obs_session_recorder.py --check
```

OBS 녹화 API는 [공식 obs-websocket 프로토콜](https://github.com/obsproject/obs-websocket/blob/master/docs/generated/protocol.md)을 따른다.
MP4 remux는 [FFmpeg streamcopy](https://ffmpeg.org/ffmpeg.html#Streamcopy)를 사용하며 재학습·좌표 계산이나 프레임 필터를 추가하지 않는다.
