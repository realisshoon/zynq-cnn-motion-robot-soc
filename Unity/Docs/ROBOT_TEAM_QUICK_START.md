# 로봇팀 빠른 시작

작성·소스 확인: 2026-10-08. 이번 패키지는 **dev/unity 인계본**이다. 2026-10-08 동일 Windows PC의 별도 `dev/unity` Clean Clone에서 새 Library Import·Editor Play·Windows Player를 검증했다. 별도 팀원 PC와 실물 UART는 미검증이다.

## 1. 먼저 알아둘 상태

- Unity에서 G51/Humanoid 외관, Manual, Recorded CSV, XYZ 재계산, Mock Robot Control을 확인할 수 있다.
- E/A/S/X와 RGB Apply는 현재 **MOCK ONLY**이다. 실제 보드 상태를 표시하지 않는다.
- 실제 COM RX 코드는 있으나 임시 `POSE3D_V1`만 받는다. 로봇팀 보드의 `TK` 및 V/T 응답은 아직 연결되지 않았다.
- 실제 TX 백엔드도 있으나 숨겨진 레거시 `M0=...,M4=...` 출력이다. 최신 보드 계약과 호환되지 않으므로 실물에서 사용하지 않는다. 기본 연결 해제/TX OFF를 유지한다.

## 2. 받을 저장소와 버전

저장소: https://github.com/realisshoon/zynq-cnn-motion-robot-soc

| 항목 | 확인값 |
|---|---|
| 전달 브랜치 | `dev/unity` |
| 이번 전달 Commit SHA | 이 문서를 포함한 인계 커밋: `git rev-parse HEAD`로 확인 |
| 감사 당시 원격 dev/unity | `30b060feb8ec3ef33e0d3d2432b465270a2f120f` — 아직 최신 Demo07 전달본 아님 |
| UART 계약 참고 dev/integration | `473e8fe1d95d53dd4aaa16db9e137a3e615462a6` |
| Unity Native DLL 소스 기준 | `4acc02f3eda814bfbe07d7d121215e49cfe80e27` — 최신 펌웨어와 구분 |

아래 명령은 **팀원 PC의 새 폴더에서** 실행한다. Clean Clone 검증에서는 원격 기준 복제 후 승인된 후보를 선별 반영했다. Clone/Pull 후 전달받은 인계 SHA가 포함되어 있는지 확인한다.

```powershell
git clone --branch dev/unity --single-branch https://github.com/realisshoon/zynq-cnn-motion-robot-soc.git
cd zynq-cnn-motion-robot-soc
git rev-parse HEAD
```

기존 clone 갱신은 변경 파일이 없는 `dev/unity` 작업공간에서만 `git pull --ff-only origin dev/unity`를 사용하고, 전달 SHA와 비교한다. 다른 브랜치나 미저장 작업 위에 강제로 덮어쓰지 않는다.

## 3. 5~10분 실행 순서

1. Unity Hub에서 **Unity 6000.3.24f1** 및 **Windows Build Support (IL2CPP를 쓸 경우 해당 모듈)**을 준비한다. 현재 확인된 Editor revision은 `4e7b9b5b6244`이다.
2. Hub → Add project에서 `<clone>/Unity`를 선택한다. `Unity/HumanMotionDigitalTwin` 중첩 폴더를 만들지 않는다.
3. URP 17.3.0 등 `Packages/manifest.json`과 `packages-lock.json`의 패키지 복원을 기다린다. HDRP로 바꾸지 않는다.
4. Project 창에서 `Assets/Scenes/Demo_07_SingleArmControl.unity`를 직접 연다.
5. Console 컴파일 오류, Missing Script/Material을 확인한 뒤 Play한다.
6. Startup에서 Robot Arm 또는 Humanoid Robot, Input=Manual, Tool=Gripper를 선택하고 START한다.
7. Header의 Servo를 펼쳐 M0~M4를 작은 범위에서 바꾼다. 이는 가상 로봇 시험이다. MENU로 복귀해 다른 Robot을 선택한다.
8. 좌측 Robot Control에서 Mock PWM 활성화 → 추종 시작 → 추종 정지 → PWM 해제를 확인한다. RIGHT/LEFT/BOTH 선택은 독립 Mock 대상 선택이며 실제 포트를 바꾸지 않는다.
9. Advanced RGB 40/30/50을 조절하고 Apply한다. `MOCK 적용 / 송신 없음`을 확인한다. Camera SHOW는 `MOCK PREVIEW / 영상 입력 없음`만 표시한다.

**Build Settings 확인 완료:** 인계 Clean Clone의 첫 활성 Scene은 `Demo_07_SingleArmControl.unity`이다. `Main.unity`는 목록에 남아 있으나 비활성이다. 검증 빌드는 이 실제 활성 Scene 목록을 사용했다. 원본 프로젝트의 Build Settings는 변경하지 않았다.

## 4. DLL·데이터

| 파일 | 용도 |
|---|---|
| `Assets/Plugins/x86_64/control_studio_v2.dll` | Manual/Recorded 공통 Agent2 출력, ABI 2 |
| `Assets/Plugins/x86_64/control_studio_xyz.dll` | XYZ + 저장 BodyFrame 계산, ABI 1 |
| `Assets/Plugins/System.IO.Ports.dll` | COM RX/TX 코드의 관리 DLL; 공식 NuGet 6.0.0 파일과 SHA256 대조 완료 |
| `Assets/StreamingAssets/ControlStudioSamples/` | 두 CSV 및 임시 UART Mock 샘플 |
| `Assets/Resources/` | 런타임 로봇 프로필·도구·브랜딩 등. 폴더째 보존 |

Native DLL은 PE x86-64이며 KERNEL32와 Windows UCRT 의존성을 확인했다. x86/macOS/Linux 지원은 검증하지 않았다. DLL과 `.meta`를 같이 전달한다. DLL이 없다고 임의의 다른 버전으로 교체하지 않는다. XYZ DLL 실패는 XYZ 모드에, 출력 DLL 실패는 공통 제어 준비 상태에 영향을 준다.

DLL을 사용하는 팀원에게 GCC 설치는 필요 없다. 재빌드 시에만 MSYS2 UCRT64 GCC와 위 고정 소스가 필요하다. `Tools/ControlStudioNative/Build-ControlStudio*.ps1`에 `-RobotRoot`를 명시하고 필요하면 `-Gcc`로 UCRT64 GCC를 지정한다. 고정 revision 체크는 유지했다. Native source manifest 경로는 소스 저장소 기준 상대경로이며, DLL과 C/H 파일은 변경하지 않았다. SHA가 다르다고 자동 갱신하지 않는다.

CSV Load는 제공된 샘플을 사용한다. Recorded는 저장 Human 각도 재생, XYZ A/B는 상대 XYZ + 저장 BodyFrame 재계산이다. A는 CSV gripper 보조 입력, B는 모드 진입 Applied M4 HOLD이며 5축 모두 원시 XYZ로 계산하는 모드가 아니다. preset은 `Application.persistentDataPath/ControlStudio/manual-preset-v1.json`에 저장되어 PC마다 별도다.

## 5. Windows Player

인계 프로젝트의 재현용 기본 빌드 경로:
`Validation/HandoffRepro/Windows/SingleArmControl.exe`

`HandoffReproValidation.AssetsAndBuild`를 분리된 clone의 batchmode에서 `--isolated-validation`과 함께 실행하면 현재 Build Settings로 빌드한다. `--handoff-output`으로 출력 폴더를 바꿀 수 있다. 실제 검증은 Git 밖의 별도 evidence 폴더로 출력했으며 exe·데이터 폴더를 Git에 넣지 않았다.

이는 Git 포함 대상이 아니다. 팀 전달 다운로드 경로와 전달 빌드 해시는 **업로드/별도 배포 후 확정 필요**이다. 별도 배포할 때 exe만 복사하지 말고 `_Data`, UnityPlayer.dll 및 동반 파일을 포함한 전체 폴더를 전달한다.

이전 검증 기록은 Unity 6000.3.24f1/Windows x64, Build 오류 0, 경고 4, 1920×1080·1280×720 Player EventSystem 검증 279 PASS/0 FAIL이다. 이는 과거 이력이다. 이번에는 Git 반영 대상만 넣은 Clean Clone에서 다시 Import·Editor Play·Windows Player 실행을 확인했다. 새 PC 검증과 구분한다. 최신 상세 결과는 [Clean Clone 재현성 결과](CLEAN_CLONE_REPRODUCIBILITY.md)를 따른다.

## 6. UART 연결 전에

현 Unity의 Real COM RX는 임시 상대좌표 패킷 시험용이다. **실제 보드의 상태 조회·5축 동기화 시험은 아직 이 UI로 수행할 수 없다.**

1. 로봇팀이 RIGHT/LEFT USB UART1 COM 및 BOOT 버전을 식별한다. 보드 간 UART0와 혼동하지 않는다.
2. TRACE=921600, 비TRACE=115200, 8N1을 BOOT 기준으로 확인한다. Unity 기본 입력값 115200을 그대로 신뢰하지 않는다.
3. 실제 조회는 로봇팀의 검증된 Python 모니터/시리얼 도구 한 개로만 수행한다. 포트를 Unity와 동시에 열지 않는다. 모니터의 정확한 실행 옵션은 해당 SHA의 `robot_arm/docs/stereo_uart_monitor_guide.md`를 따른다.
4. 모니터 `r V`, `r T`, `r t`/`l V`, `l T`, `l t`는 라우팅 문법이다. 직접 COM 송신은 V/T/t 단일 바이트이며 접두어·Enter를 붙이지 않는다.
5. 연결했다고 E/A를 자동 송신하지 않는다. 실물 지지·가동 공간·물리 전원 차단 경로와 담당자 승인이 먼저다. S는 즉시 정지가 아니며 X는 토크 해제로 팔이 처질 수 있다.

상세 계약과 누락 구현: [UART 인계서](ROBOT_TEAM_UART_HANDOFF.md)

시험 기록: [체크리스트](UART_TEST_CHECKLIST.md). 미시험 항목은 NOT TESTED를 유지한다.

## 7. Pcam·UART·사용자 영상의 구분

Pcam 픽셀은 FPGA MIPI → Bayer/RGB·Gamma → VDMA/DDR → CNN에서 처리되고, 좌우 검출 좌표는 보드 간 UART0/Stereo → Agent1/2/3 → Servo PWM으로 이어진다. Unity가 원본 영상을 UART로 받는 구조가 아니다.

USB UART1의 최종 역할은 **양방향**이다. 보드→Unity는 상태·M0~M4 출력·유효성/시간, Unity→보드는 V/T/E/A/S/X와 RGBY 설정이다. Startup INPUT은 `Manual, CSV / XYZ, UART`이며, 통신 패널 제목은 `UART / COMMUNICATION`이다. 개별 연결·상태의 RX/TX 방향 표시는 그대로다. 명칭 변경은 양방향 통신의 최종 역할을 나타내며 보드 명령 연동 완료를 뜻하지 않는다. 현재 보드 TK 연결과 명령 UI는 미완성이다.

실시간 영상은 별도로 **FPGA HDMI → USB HDMI Capture → Windows Video Input → Unity Preview**를 사용해야 한다. 현재 Preview는 Mock이다. 실제 함수·XSA 연결과 미검증 구간은 [상세 인계서 11절](ROBOT_TEAM_UART_HANDOFF.md)에 기록했다.


## 8. Humanoid 어깨 / RIGHT·LEFT·BOTH 가상 조작

1. Startup에서 Humanoid Robot / Manual을 선택하고 START한다. 일시정지 상태라면 Resume virtual을 누른다.
2. 왼쪽 Robot Control에서 RIGHT / LEFT / BOTH를 고른다. 선택만으로 PWM·FOLLOW·관절값은 바뀌지 않는다.
3. Header의 Servo를 펼치고 SHOULDER 또는 ARM JOINTS를 펼친다. 두 섹션은 독립적으로 접히며 값은 유지된다.
4. 어깨 Pitch(X)는 0~10°, Roll(Z)은 -8~8°의 작은 **VISUAL ONLY** 시연 범위다. 기존 Rig에는 물리적 어깨 한계/서보 보정값이 없다. 이 범위를 실물 안전 범위로 사용하지 않는다. Yaw(Y)는 기존 축을 유지하지만 UI로 노출하지 않는다.
5. RIGHT ARM JOINTS는 기존 Manual → Agent2 → Router → ApplyCommand 경로다. LEFT는 별도 가상 프리뷰이며 실제 보드·센서 추종이 아니다. BOTH에서 드래그/숫자 편집한 축만 양측에 적용한다. R/L 값이 달라도 선택 자체로 같아지지 않는다.
6. 기존 Home/Save/Load/Default preset은 **RIGHT M0~M4만** 대상으로 한다. 어깨·왼팔은 세션 값이며 기존 preset 파일 형식에 추가하지 않았다.

Robot Control E/A/S/X와 RGB는 좌우 독립 Mock 상태에만 반영된다. BOTH에서 한쪽 PWM OFF이면 A는 그쪽에서 거부되고 `PARTIAL · R Accepted / L Denied`처럼 구분한다. G51의 Servo 패널은 여전히 한 대의 기존 M0~M4이며 Robot Control의 LEFT/BOTH 선택은 두 G51 실물 제어를 추가하지 않는다. Preview와 Mock PWM 상태는 실제 모터 구동을 의미하지 않는다.
