# HumanMotionDigitalTwin — Unity Control Studio

현재 공유 기준은 2026-10-14 Camera Overlay Toggle R1 Windows Player를 만든 전체 Unity 소스다. CSV Unified UART R1의 CSV/역할 수정과 기존 ControlUI KO / Quick HUD / G51 source switch 기능을 포함한다. 이전 STEP 6 실험 작업 트리 전체를 덮어씌운 버전이 아니다.

## 열기 및 실행

1. 이 저장소의 `dev/unity` 브랜치를 받은 뒤 Unity Hub에서 이 `Unity` 폴더를 추가한다.
2. **Unity 6000.3.24f1**, Windows x64 Build Support를 사용한다. 최초 Import에는 패키지 다운로드를 위한 인터넷과 Git 실행 파일이 필요하다.
3. `Assets/Scenes/Demo_07_SingleArmControl.unity`를 열고 Play한다. 기존 Startup Menu에서 Robot과 Input을 선택하고 START한다.
4. Native DLL은 `Assets/Plugins/x86_64`에 포함되어 있다. 별도로 로봇팀 C 코드를 빌드하거나 FPGA에 배포할 필요가 없다.

`Library`, `Temp`, `Logs`, `UserSettings`, Windows EXE 및 녹화 영상은 Git 공유 대상이 아니다. 이 저장소 전체를 받아도 실제 FPGA에 자동 배포되지 않는다.

## 포함 기능

- 기존 Startup Menu, G51/Humanoid, Dark Control Studio Dock, Ports/Control, Camera Size 1–4, Presentation, Studio/Factory.
- 기존 UART TK Parser, RIGHT/LEFT/BOTH 데이터 선택, G51 시각화, Quick HUD 및 보드 SD Records 흐름.
- CSV 불러오기와 PLAY/PAUSE/RESUME/STOP. 역할 없는 단일 CSV는 LEFT 또는 RIGHT 대상을 명시적으로 확인하며 BOTH에 자동 복제하지 않는다.
- CSV 입력 중 포트를 연결해도 입력 소스를 UART로 자동 변경하지 않는다.
- `SERVO_INVERSE_COMPATIBILITY` CSV는 기존 Native 변환/검증 경로를 사용한다. `source_servo_M*`를 다시 사람 각도로 보정하지 않는다. PC→FPGA CSV 관절값 실시간 스트리밍은 구현하지 않았다.
- Control의 **6점 / 전체 관절점 토글 (b)**. 기존 RIGHT/LEFT/BOTH 선택과 승인 조건을 사용하며 해당 연결에 소문자 `b` 한 바이트만 요청한다. 실제 `HDMI overlay: ...` 수신 응답으로 상태를 확정하며 연결/클릭만으로 성공 처리하지 않는다. BOTH는 두 보드의 개별 응답을 표시한다.

## 실제 보드 사용 주의

기본 실행, 화면 선택, 포트 검색은 실제 PWM/FOLLOW 명령을 자동 송신하지 않는다. 기존 송신 승인 조건과 출력 보호 정책을 유지한다. Virtual 표시는 보드의 물리 PWM 차단을 보장하지 않는다. 실제 서보 사용과 명령 송신은 별도의 현장 승인 및 설치 BOOT/전원/기구 상태 확인 후 수행한다.

TK는 실제 엔코더 위치가 아닌 서보 명령 Telemetry다. LEFT Native 기구학과 기존 G51 표시 모델의 물리 링크 길이는 동일하다고 보증하지 않는다. 현재 Native DLL과 설치 펌웨어의 LEFT 그리퍼 제한(2400/2200us 등) 일치는 현장 확인 항목이다.

UART RX 파일은 Windows의 `%USERPROFILE%\AppData\LocalLow\DefaultCompany\HumanMotionDigitalTwin\BoardUartLogs`에 저장된다. RIGHT diagnostics를 연결 전에 명시적으로 활성화하면 같은 폴더에 `.diagnostic.jsonl`이 생성된다. 파일명 시각은 UTC다.

## Windows 빌드

Build Settings의 유일한 enabled Scene은 Demo_07이며 Main Scene은 비활성으로 유지한다. Windows x64, Development Build OFF를 사용한다. `CameraPreviewSizeBatch.BuildLatest`는 기존 폴더를 덮어쓰지 않는 명시적 CLI 빌드를 지원한다.

```powershell
$unityExe = 'C:\Program Files\Unity\Hub\Editor\6000.3.24f1\Editor\Unity.exe'
$projectDir = (Resolve-Path '.\Unity').Path # 저장소 루트에서 실행
$playerPath = 'D:\UnityBuilds\NewBuild\HumanMotionDigitalTwin.exe' # 새 빈 출력 폴더
& $unityExe -batchmode -nographics -quit -projectPath $projectDir -executeMethod CameraPreviewSizeBatch.BuildLatest --camera-player-output $playerPath -logFile "$env:TEMP\HumanMotionDigitalTwin-build.log"
```

설치 위치에 맞춰 `$unityExe`와 `$playerPath`를 지정한다. 같은 프로젝트의 Editor가 열려 있으면 닫고 실행한다. EXE를 전달할 때 `_Data`, `UnityPlayer.dll`, `MonoBleedingEdge`, Plugins 등 빌드 폴더 전체를 함께 전달한다.

## 검증과 출처

[공유 검증 기록](Docs/LATEST_SOURCE_HANDOFF.md)과 [기준 소스 SHA256 목록](Docs/LATEST_SOURCE_MANIFEST.json)을 참고한다. 실제 COM/서보 시험과 오프라인 Fake UART/Transform 시험을 구분한다. 이전 단계별 문서는 당시 구현 기록이며 현재 버전의 기능 계약은 본 문서와 실제 소스를 우선한다.
