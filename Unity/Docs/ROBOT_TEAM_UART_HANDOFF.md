# 로봇팀 UART 상세 인계

2026-10-08 / 소스 감사 및 인계 준비. 실제 UART·실물 시험은 NOT TESTED. Git 업로드는 수행하지 않았다.

## 1. 근거와 버전 경계

Unity 전달 SHA는 **업로드 후 확정 필요**이다. 비교 기준 dev/unity=`30b060feb8ec3ef33e0d3d2432b465270a2f120f`, 최신 UART 참고 dev/integration=`473e8fe1d95d53dd4aaa16db9e137a3e615462a6`이다. 두 브랜치를 합치지 않았다.

펌웨어 문서 근거(모두 위 integration SHA):

- [uart_protocol_unity.md](https://github.com/realisshoon/zynq-cnn-motion-robot-soc/blob/473e8fe1d95d53dd4aaa16db9e137a3e615462a6/robot_arm/docs/uart_protocol_unity.md)
- [robot0_robot1_commands.md](https://github.com/realisshoon/zynq-cnn-motion-robot-soc/blob/473e8fe1d95d53dd4aaa16db9e137a3e615462a6/robot_arm/docs/robot0_robot1_commands.md)
- [stereo_uart_monitor_guide.md](https://github.com/realisshoon/zynq-cnn-motion-robot-soc/blob/473e8fe1d95d53dd4aaa16db9e137a3e615462a6/robot_arm/docs/stereo_uart_monitor_guide.md)

코드 근거는 `robot_arm/src/integration/{cnn_app.c,cnn_console.c,trace.c,agent_pipeline.c,main_integration.c,platform_vitis.c}`, `src/robot_calibration/forearm_calibration_config.c`, `src/output_controller/servo_config.c`, `src/cnn_firmware/cnn/cnn_hw.h`이다.

문서 이력 주의: monitor guide의 이전 절은 외부 `captures/g_uart_tuning_20261004/G/snapshot`을 지칭하지만, 같은 SHA의 `robot_arm/docs/seungyeol3_release.md`는 정상 저장소 루트를 canonical 소스로 명시하고 `final_uart0.xsa`를 사용한다. 최신 추적 소스를 감사 기준으로 삼았다. 릴리스 문서도 SD 복사·실제 부팅·물리 시험을 NOT TESTED로 남겼으므로 설치 BOOT와 동일하다고 단정하지 않는다.

## 2. 시스템 경계

목표 보드 경로:
`Stereo Camera → FPGA CNN → Agent1 → Agent2 승인·제한 → Agent3 PWM → Physical Robot`

목표 표시 경로:
`보드의 실제 출력 처리 후 UART 상태·관절 기록 → PC 수신 → Unity 시각화`

현재 Unity 경로는 다음과 다르다.

- Manual → C 출력 policy → `SingleArmCommandRouter` → `ForearmArmController.ApplyCommand()` → 기존 G51 visual adapter.
- Recorded CSV Human 각도 → ABI v2 calibration/승인/HOLD/출력 → 같은 router.
- XYZ A/B 또는 임시 POSE3D RX → 별도 XYZ C solver → Human target → ABI v2 → 같은 router.
- Robot Control E/A/S/X, RGB, Camera Preview는 Mock UI이다. 최신 보드의 TK는 현재 위 경로로 들어오지 않는다.

**보드 TK는 이미 명령 단계이므로 Human 각도로 취급하여 calibration을 다시 적용하면 안 된다.** 추후 연결에서는 출력 소유권과 보드 시각화용 경로를 명시적으로 설계해야 한다. 이번 작업은 새 메시지/제어기를 구현하지 않는다.

## 3. Unity 코드 지도와 구현 상태

아래 경로는 전달 Unity 프로젝트 루트 기준이다. ControlStudio 파일은 `Assets/Scripts/ControlStudio/` 아래에 있다.

| 기능 | 실제 파일·함수 | 상태/제한 |
|---|---|---|
| Scene | `Assets/Scenes/Demo_07_SingleArmControl.unity` | VERIFIED IN CLEAN CLONE. Demo07을 첫 활성 Build Scene으로 설정; 원본 Build Settings는 보존 |
| Startup | `ControlStudioStartupMenu.Cycle/StartStudio/ShowMenu` | IMPLEMENTED. Robot/Input/Tool 선택, MENU 복귀 |
| 제어 UI | `ControlStudioRuntimeUI`, `ControlStudioHeaderLayout` | IMPLEMENTED. 모드별 패널·Servo 접기 |
| Mock 명령 | `RobotControlMockPanel`, `RobotControlMockState.EnablePwm/StartFollow/StopFollow/DisablePwm/Apply` | MOCK ONLY. 실제 송신/상태조회 없음 |
| COM RX | `UartPose3DSource.ConnectReal/ConsumeLine`, `UartPose3DProtocol.TryParse`, `UartPoseLineFramer.Feed` | PARTIAL. 실제 SerialPort 수신 코드 + 임시 RIGHT XYZ 파서. 보드 실수신 NOT VERIFIED |
| COM UI | `ControlStudioUartPanel` | PARTIAL. Mock/Real COM RX, baud/port. V/T/t/TK 표시 미구현 |
| TX | `ControlStudioUartOutput.Connect/SetTxEnabled/QueueApplied/FormatApplied` | PARTIAL. 실제 SerialPort write 구현은 존재; 기본 OFF, 레거시 UI 숨김, 최신 보드와 비호환 |
| 단일 제어 소유자 | `SingleArmCommandRouter.Submit/SubmitHuman/AdvanceVirtual/OutputTick` | IMPLEMENTED. 20ms 가상 출력, epoch/owner/safety |
| Native | `ControlStudioOutputPolicy` ABI2, `ControlStudioXyzPolicy` ABI1 | IMPLEMENTED. 고정 구버전 C 기반; 최신 보드와 수치 동일 보장 아님 |
| 논리 조인트 | `Assets/Scripts/Validation/ForearmArmController.cs` `ApplyCommand()` | IMPLEMENTED. M0~M4 clamp와 rest 회전 적용 |
| realistic G51 | `Demo06RealisticVisualAdapter.SyncVisuals`, `G51IndustrialVisual`, `RobotVisualProfiles.Update/Sync` | IMPLEMENTED. 원본 logical pivot과 visual 분리 |
| Humanoid | `HumanoidVisualRig.ApplyArmTarget`, `HumanoidJointAdapter.FromSingleArmRight` | PARTIAL. 오른팔 Applied visual 호환. shoulder 3축 입력=0. 왼팔 정적 preview |
| CSV | `RecordedHumanCsv`, `CsvRecordedHumanSource`, `CsvReplayTimeline`, `Pose3DFrame` | IMPLEMENTED. Recorded/XYZ A/B, 저장 BodyFrame 사용 |
| Camera | `RobotControlMockPanel` Preview | MOCK ONLY. HDMI/USB 캡처 미구현 |
| Presentation | `ControlStudioPresentationUI.SetPresentationMode`, `ControlStudioOrbitCamera.SetPresentationMode` | IMPLEMENTED. UI만 숨기도록 최근 수정, 이전 Player 검증 기록 존재 |
| 배경 | `SimulationEnvironment`, `ControlStudioBackgroundView` | Grid IMPLEMENTED. Factory NOT IMPLEMENTED; HDRP 패키지 캐시만 존재 |

Humanoid 양팔 rig API가 존재해도 왼팔 Human Motion 입력 연결을 의미하지 않는다. `RobotVisualProfiles.Sync`는 오른팔만 `router.Applied`로 갱신한다. Robot 1 LEFT Mock 선택도 이 사실을 바꾸지 않는다.

## 4. 포트·바이트·상태 계약

| 역할 | PC 연결 | 로봇 출력 | 데이터 역할 |
|---|---|---|---|
| RIGHT Robot 0 | 해당 보드 USB PS UART1 | JB CH0~CH4 | stereo Agent1 생성, RIGHT 로컬 출력 |
| LEFT Robot 1 | 해당 보드 USB PS UART1 | JC CH0~CH4 | LEFT CNN/UART0 좌표 송신, RIGHT의 신선한 결과 수신 |
| 보드 간 연결 | PS UART0 | PC 콘솔 아님 | stereo/보드 간 데이터, 기본 115200 |

TRACE 콘솔 921600 baud, 비TRACE 115200 baud, 8N1. actual BOOT 기준 확인 필수. COM은 Windows에서 각각 식별한다. 포트당 하나의 프로세스만 연다.

단일 키는 대소문자 구분, CR/LF 없이 전송한다. `r E`, `l E`, `both E`는 Python 모니터 라우팅이며 wire 메시지가 아니다. 숫자/프레임 메뉴는 별도 CR 종료 계약이다.

최신 권위 상태:
`[REC_STATE] schema=1 side=l|r ui=... mode=... pwm=... ram_samples=... entries=... selected=... playing=... delete=... unsaved=...`

상태 변경 또는 V 강제 조회 시 나온다. heartbeat/명령별 ACK 번호가 아니다. 분할 read를 누적하고 혼합 TRACE/콘솔 로그에서 해당 줄을 식별해야 한다. 없는 응답은 UNKNOWN/TIMEOUT이지 OFF가 아니다. 현재 Unity는 이 파서를 구현하지 않았다.

| 명령/hex | 의미·사전조건 | 코드/문서상 응답 및 향후 UI | 거부·안전 |
|---|---|---|---|
| V / 56 | 해당 로봇 상태 조회, 연결·입력 경계 확인 | PWM/역할 보고와 강제 REC_STATE; 확인된 값만 표시 | timeout이면 UNKNOWN. write 성공은 적용 증거 아님 |
| T / 54 | local follow 상태 조회 | RIGHT async 또는 LEFT remote follow 상태; PWM과 별개 표시 | stereo 노출 동기화·실측 정지 증거 아님 |
| E / 45 | PWM 활성화 요청. 최신 ui=IDLE, mode=LIVE; 실제 자세 확인 | `[PWM] enabled=... result=... mode=... arm=...` 및 상태 재조회 | 목표 catch-up/safety/상태 검사로 거절 가능. 자동 추종 시작 아님 |
| A / 41 | 실시간 추종 요청. IDLE/LIVE, PWM ON 및 보드 승인 | RIGHT `[ST] async_test=... pwm=... result=...`; LEFT remote follow 보고. 성공 후 FOLLOW ON | 메뉴·녹화·재생·정착 등 허용 안 되는 상태 차단. 노출 동기화 검증 아님 |
| S / 53 | 새로운 CNN 추종 목표 차단 | follow OFF 확인, PWM 기존 상태 유지, 마지막 승인 목표로 이동 완료 뒤 HOLD | 즉시 물리 정지 아님. CNN 추론 중단 아님. 재생 정지 명령과도 구분 |
| X / 58 | PWM OFF + 추종 차단 | PWM OFF 응답/조회 확인, 토크 해제 상태 표시 | 팔 처짐·낙하 가능. 하드웨어 비상정지 아님 |
| t / 74 | CNN 상태 및 지원 기능 조회 | auto/running/single/stopping, 기능 지원 | 소문자. T/로봇 PWM 조회와 다름 |
| m / 6D | RGBY 13항목 메뉴 | 각 숫자 prompt, 마지막 적용 결과 | CNN 연속 추론 OFF 요청 후 진입; Unity Mock Apply와 다름 |

정확한 응답 전체 줄과 승인 문자열은 실제 BOOT 로그로 추가 확정한다. `cnn_app_report_pwm_result/async_result`의 형식은 compile-time 역할 분기가 있으므로 특정 보드 라벨을 무조건 고정하지 않는다.

불완전 프레임/숫자 메뉴에서 최신 BOOT의 `ESC+S`=`1B 53`, `ESC+X`=`1B 58`은 입력을 취소하고 해당 동작을 실행한다. 녹화 NAME/MENU 취소와는 다르다. 구형 BOOT 지원을 먼저 확인한다. SD/foreground 처리 지연이 가능하며 즉시 처리 보장은 없다. timeout/짧은 write 후 자동 재시도하지 않고 상태를 확인한다. 연결 시 E/A 자동 송신 금지.

## 5. TK 명령 스트림: 확인된 것과 미연결 부분

`trace.c:trace_tick` 스키마:
`TK,tick,t_ms,dur_us,er,ep,wp,wr,g,per,pep,pwp,pwr,pg,rem,w,err`

| 필드 | 코드 의미 |
|---|---|
| tick, t_ms, dur_us | 제어 tick 번호, trace 시간 ms, 표시 구간 경과 µs |
| er,ep,wp,wr | 이번 tick Agent3 command가 current+valid일 때 명령 각도 deg, 소수 1자리. 아니면 빈 4필드 |
| g | 같은 command의 gripper_norm 소수 2자리; 무효면 빈 필드 |
| per,pep,pwp,pwr,pg | ctx->pwm의 5축 pulse µs |
| rem | motion target이 있으면 ramp remaining, 소수1자리; 없으면 빈 필드. 엔코더 오차 아님 |
| w | servo_writes 카운터가 지난 TK와 달라졌으면 1: 해당 구간 HAL write 성공 |
| err | servo_errors가 달라졌으면 1 |

`main_integration.c`의 출력 처리 뒤 `TRACE_TK`, `platform_vitis.c`의 20ms=50Hz tick을 확인했다. TRACE 비활성 빌드에서는 매크로가 제거되고 출력 mute/버퍼/통신 조건에 따라 매50Hz 수신을 보장하지 않는다. 실제 BOOT의 출력 여부·주기·유실률은 NOT TESTED.

`agent3_apply_command`는 PWM OFF에서도 PWM **변환값**을 보존한다. PWM ON에서 HAL 실패하면 마지막 적용 PWM을 유지하고 오류를 센다. 따라서 TK 각도/PWM 숫자가 있다는 사실만으로 실제 서보에 적용됐다고 판정하면 안 된다. w/err, PWM 상태, current tick, 연결 신선도를 같이 확인해야 한다. TK는 encoder/전류/힘 피드백이 아니다.

현재 Unity에는 TK 파서·보드별 상태 모델·왼팔 telemetry 연결이 없다. 정식 telemetry 전달 계약(포트별 식별, reconnect/session, 늦은 tick, 상태 freshness, trace drop/mute, 적용 성공 판정)은 로봇팀과 확정해야 한다. 기존 TK 형식을 새로운 확정 Unity 프로토콜로 임의 선언하지 않는다.

## 6. M0~M4와 보정 단계

실제 Demo07 직렬화 및 `ForearmArmController.ApplyCommand`를 확인했다. 아래 Unity 회전은 저장 restRotation에 추가되는 회전이다.

| 순서 | TK | Unity 적용 | 기존 Unity clamp |
|---|---|---|---|
| M0 Elbow Roll | er / per | local Y, offset=-90, sign=-1 → `-90-(M0-90)` | 20..160 deg |
| M1 Elbow Pitch | ep / pep | local X, offset=0, sign=+1 → `M1-90` | 20..180 deg |
| M2 Wrist Pitch | wp / pwp | local X, offset=0, sign=+1 → `M2-90` | 10..160 deg |
| M3 Wrist Roll | wr / pwr | local Y, `-(M3-90)` | 10..170 deg |
| M4 Gripper | g / pg | `G51GripperVisual.Apply(clamp01(M4))` | 0=CLOSE, 1=OPEN |

논리 체인: ElbowRoll → ElbowPitch → WristRoll → WristPitch. visual adapter의 bind/rest와 실제 servo 방향은 별도 층이다. M2=90 code-neutral이 실물 중립임을 뜻하지 않는다.

참고 integration의 **추적 C** Human→Servo 보정은 scale=1, M0 direction=-1/offset90/range10..170, M1 -1/120/20..180, M2 +1/80/10..160, M3 -1/87/10..170이다. 코드 상단 주석에는 과거 wrist roll +1 설명이 남아 있어 현재 initializer를 기준으로 읽었다. 이 값은 위 Unity visual sign과 같은 개념이 아니다. Unity DLL은 이전 SHA이며 **M0 허용범위부터 다르므로** 새 펌웨어 TK를 현재 clamp에 넣으면 극단값이 달라질 수 있다. 이번에 clamp/DLL을 고치지 않았다.

servo_config의 0/90/180deg→500/1500/2500µs는 코드값이며 실측 안전범위 검증을 대신하지 않는다. M4는 `servo_control.c:gripper_to_pwm_us`의 별도 변환 `center_us + (1-gripper_norm)*(max_us-center_us)`를 사용하므로 이 설정에서 1=OPEN1500µs, 0=CLOSE2500µs이다. LEFT gripper 2400µs cap 시험판 등 BOOT별 제한이 문서에 존재하므로 실제 BOOT 설정을 받아야 한다. 물리 방향·offset·S 최종 자세는 각각 실물에서 확인한다.

## 7. 현재 임시 XYZ RX와 끊김 정책

`POSE3D_V1,RIGHT,frame_id,target_frame_id,time_sec,18 XYZ,9 BodyBasis,10 source flags,gripper_norm\n` (43필드).

이것은 `Pose3DFrame.Profile=CSV_RELATIVE_CAMERA_STORED_BODY_RIGHT`, 단위 relative shoulder-width이다. 원시 stereo mm/m 좌표나 최종 명령이 아니다. XYZ·BodyFrame·source flags가 필요하고 per-point confidence/age는 UNKNOWN이다. flags[7] BodyFrame 무효, NaN/Inf, 필드 수/side 오류를 거부한다.

RX는 LF 완성 줄을 누적하며 CR은 제거한다. 프레임/시간 단조 검사, 중복/역순 거부, gap 카운트가 있다. stale 기본0.5s이면 `STALE / HOLD` 및 새 target 중단이다. 기존 승인 궤적이 즉시 멈춘다는 보장은 없고 S/X를 보드에 송신하지 않는다. COM disconnect를 PWM OFF로 해석할 수 없다.

현재 실제 TX의 `M0=...,M1=...,M2=...,M3=...,M4=...\n`은 보드의 E/A/S/X 또는 TK와 다르다. GUI의 레거시 TX 구역은 숨겨져 있으며 Connect 시 TX=false, focus/pause/epoch 변경 때 disarm한다. 이 코드를 실물용이라고 안내하지 않는다. router의 `HardwareTxCount=0` 상수만으로 전체 백엔드 TX 안전성을 증명하지 않는다.

## 8. RGB Margin

`cnn_hw.h` 기본 Red=40, Green=30, Blue=50. `cnn_console.c:color_fields` 순서:

| 번호 | 항목 | 범위 |
|---|---|---|
| 1~3 | red / green / blue margin | 각0..255 |
| 4~5 | yellow R-G max difference / R,G above B min gap | 각0..255 |
| 6~8 | red / green / blue minimum brightness | 각0..255 |
| 9~11 | yellow minimum red / minimum green / maximum blue | 각0..255 |
| 12 | minimum detected taps | 1..262143 |
| 13 | color enable mask R=1,B=2,G=4,Y=8 | 0..15 |

m 진입 시 연속 CNN OFF 요청. 개행 없는 prompt도 올 수 있다. prompt마다 `숫자`+CR(0D) 또는 CR만 보내 기존값을 유지한다. 마지막 필드 뒤 적용한다. 중간에 V/R/P 같은 일반 명령을 섞지 않는다. 완료 뒤 t로 확인하고 CNN이 OFF이면 담당자가 소문자 a로 재개한다. 대문자 A는 로봇 추종이며 다르다. 자동 재개/자동 재전송을 가정하지 않는다. 녹화·재생·정착·저장 중 설정 변경은 금지한다.

현 Unity RGB Slider는 0..100의 Mock 세 값만 저장하며 13필드 전송·응답·설정 영속화가 없다. UI Apply 성공은 보드 적용 성공이 아니다.

## 9. Camera·배경·의존성

향후 영상 경로는 `FPGA HDMI → USB Capture Device → Windows Video Input → Unity Preview`이다. UART 영상 전송이 아니다. 현재는 Mock placeholder뿐이며 실제 장치 선택/프레임 캡처는 미구현이다.

Simulation Grid는 현재 지원한다. Unity Factory는 외부 Asset Store 캐시에만 있는 약882MiB HDRP 패키지로, URP17.3 프로젝트에 import하지 않았다. 기본 실행 의존성이 아니며 재배포 라이선스와 선별 URP 변환은 별도 승인 대상이다.

Native DLL hashes:

- ABI2 `control_studio_v2.dll`: `236E05DB7E386AFDB79279DF0D73C8ABC41B1ECE17873EDDDB3EC794D9CA9783`
- XYZ ABI1 `control_studio_xyz.dll`: `8D6AF5F835D3BE4513AD0E3F5A22A073B855901770594DBCD2FBA85B578A789A`

두 파일은 manifests와 일치한다. Windows UCRT/KERNEL32 의존, x64. `Tools/ControlStudioNative`에 wrapper/build script/source hash manifest가 있다. source_head=`4acc02f3eda814bfbe07d7d121215e49cfe80e27`. 인계 manifest는 `robot_arm/` 기준 상대경로로 정리했고 기존 source/DLL 해시는 유지했다. 빌드 스크립트는 `-RobotRoot` 필수·`-Gcc` 선택 인자를 받는다. Editor 검증 스크립트도 현재 프로젝트를 기준으로 출력하고 `--isolated-validation`을 요구한다. Windows Clean Clone에서 두 DLL의 실제 계산과 Player 로드를 확인했다. 별도 새 PC는 미검증이다. System.IO.Ports의 공식 파일 대조와 라이선스는 아래 12절에 기록했다.

## 10. 실제 질문과 후속 연결 조건

| 로봇팀 확인 항목 | 필요한 증거/결정 |
|---|---|
| LEFT/RIGHT BOOT 식별 | BOOT.BIN SHA256, build define, 대응 소스/snapshot·배포 버전 |
| 실제 포트·baud | 장치별 COM, UART1 연결, 부팅 로그, 8N1/baud |
| V/T/t/REC_STATE | 양 보드 원시 응답 로그, 지원 명령/상태·timeout 동작 |
| telemetry 채택 | 실제 TK 샘플과 출력 주기/유실/mute, PWM OFF·HAL 실패 사례, session 식별 방침 |
| 관절 대응 | 5축 명령·PWM·실물 각도 표, 방향/offset/가동범위, LEFT cap |
| S 종료 | 신규 목표 차단 뒤 최종 승인 목표 도달/유지의 실물 로그 |
| RGBY | m 13개 prompt/적용 로그, CNN 재개 및 CFG 지원 여부 |
| 실물 안전 | 시험 담당자 승인, 하중/지지, 접근 금지 구역, 실제 전원 차단 경로 |

Unity 쪽 후속 개발은 TK/상태 RX, 응답 기반 command UI, 단일 출력 소유권, telemetry stale 정책, 좌우 profile/범위 호환성을 별도 승인받아 진행한다. 파일 공유와 Mock 시험을 위해 UART 설계 확정까지 기다릴 필요는 없지만, 실물 동기화 GO로 표시할 수는 없다.

## 11. Pcam 영상 입력부터 Unity Digital Twin까지의 데이터 흐름

추가 감사는 동일 integration SHA의 실제 함수와 `robot_arm/vitis/xsa/final_uart0.xsa` 내부 `design_1.hwh`를 근거로 한다. HWH는 합성된 하드웨어 연결 메타데이터이며 RTL 내부 연산 정확도/보드 실행 시험은 아니다. 해당 branch에는 CNN/RTL 전체 소스 디렉터리가 없으므로 내부 CNN·색상 검출 RTL의 연산을 C 함수 구현이라고 쓰지 않는다.

```text
Pcam 5C / OV5640
  → PL MIPI D-PHY → CSI-2 → BayerToRGB → Gamma → VDMA S2MM → DDR
  → image DMA / CNN Accelerator → keypoint + RGBY marker 결과 레지스터
  → PS 펌웨어 → 좌우 좌표 pairing / Stereo 3D (UART0 보드 간 데이터)
  → Agent1 사람 목표 → Agent2 승인·제한 → Agent3 → PL Servo PWM

USB UART1: 보드 상태·TK → PC/Unity (최종 연결 미구현)
USB UART1: PC/Unity → V/T/E/A/S/X/m (현 GUI는 Mock, 최종 연결 미구현)

DDR → VDMA MM2S → Keypoint Overlay → Video Out → HDMI
     → USB HDMI Capture → Windows Video Input → Unity Preview (현재 Mock)
```

### 영상 입력·CNN: PL 처리와 PS 설정 분리

| 단계 | 실제 파일·함수/IP | 확인 내용·한계 |
|---|---|---|
| 앱 초기화 | `robot_arm/src/integration/cnn_app.c:cnn_app_init()` | `initialize_camera_capture()` → `initialize_display()` → `initialize_cnn()` 호출 |
| Pcam 센서 | `src/cnn_firmware/ov5640/OV5640.c:OV5640_Init/OV5640_PowerCycle/OV5640_InitSensor/OV5640_SetMode720p` | SCCB 설정·sensor ID 및 1280×720 RAW10 모드 설정. 모드 테이블은60fps, 실측 FPS 아님 |
| MIPI 제어 | `src/cnn_firmware/mipi_rx/mipi_rx.c:mipi_rx_reset/mipi_rx_enable` | PS가 D-PHY/CSI-2 AXI-Lite 제어 레지스터를 설정. 픽셀 스트림 처리 자체는 PL |
| PL 영상 경로 | XSA HWH `MIPI_D_PHY_RX_0` → `MIPI_CSI_2_RX_0` → `AXI_BayerToRGB_0` → `AXI_GammaCorrection_0` → `axi_vdma_0/S_AXIS_S2MM` | HWH의 BUSNAME으로 연결 확인. C가 원본 픽셀을 UART로 보내는 경로 아님 |
| VDMA·DDR | `src/cnn_firmware/vdma_api/vdma_api.c:run_vdma_frame_buffer/ReadSetup/WriteSetup/StartTransfer` | XAxiVdma_DmaConfig/SetBufferAddr/Start로 write/read 경로 구성; BOTH 모드. 실제 DDR 주소는 build define |
| 완료 프레임 선택 | `src/cnn_firmware/cnn/cnn_bringup.c:cnn_bringup_prepare_frame_internal` | 현재 write frame `%3`, `(current+2)%3` 완료 frame 선택, SG 구성 전후 write index 재검사 |
| CNN DMA 데이터 | `src/cnn_firmware/cnn/cnn_frame_sg.h`, `cnn_frame_sg.c:cnn_sg_build` | 1280×720, RGB3bytes, stride3840, frame2764800bytes, SG144개/ROW_STEP5. 전체 원본 영상 UART 전송 아님 |
| 추론 시작 | `cnn_app.c:cnn_continuous_step` → `cnn_bringup_start` → `cnn_hw_configure/cnn_hw_start` | 가중치 SD load 상태 검사, weight/feature/SG/frame 주소·frame ID 설정. PS가 추론을 지시하고 PL CNN이 실행 |
| CNN 하드웨어 | HWH `cnn_accelerator_top_0` v4.1 | `s_image_axis`←axi_dma_image, `s_weight_axis`←axi_dma_weight, feature DMA 입출력, AXI-Lite control 확인 |
| 완료·결과 | `cnn_bringup_service` → `src/cnn_firmware/cnn/cnn_hw.c:cnn_hw_read_result` | IRQ/DONE/error/timeout 검사 후 joint x/y/score/valid 및 red/blue/green/yellow 결과 레지스터 읽음 |
| 결과 표시 | `cnn_bringup.c:cnn_overlay_publish` | keypoint overlay register 갱신. 추론 결과와 HDMI 표시용 데이터는 구분 |

`cnn_app.c:print_video_status`는 RAW10 2-lane MIPI를 표시한다. XSA/펌웨어 설정 존재를 실제 카메라 모델 식별·프레임 완전성·새 BOOT 작동 검증으로 확대하지 않는다. 가중치 파일·CNN pack(문서상0x77D4E3BB)과 설치 bitstream 일치는 로봇팀 확인 대상이다.

### Stereo·Agent·PWM

| 단계 | 실제 호출 | 처리·유효성 |
|---|---|---|
| CNN 완료 분기 | `cnn_continuous_step()` → `stereo_board_on_result(&last_result,NULL)` | stereo 빌드에서는 board 경로. non-stereo에서는 `input_pose_cnn_publish()` |
| Mono 2D 경로 | `input_pose_cnn.c:input_pose_cnn_publish/joint_point/marker_point` | 좌우 shoulder·오른 elbow/wrist, finger1=red/finger2=green, pixel 범위/valid 검사. 색상 marker와 CNN joint를 구분 |
| UART0 역할 | `stereo_board.c:stereo_board_init/receive_isr/service_tx/stereo_board_service` | UART0 base0xE0000000, USB STDIN/OUT UART1 base0xE0001000 검사. binary parser/CRC/session/order 등 별도 계약 |
| 좌표 전달 | `stereo_board_on_result` | LEFT는 `ROBOT_FOLLOW_COORDINATES` queue 전송, RIGHT는 local `stereo_link_push(...,1,...)`. 좌표·score·valid·RGBY·frame/session 전달; 이미지 픽셀 아님 |
| Stereo 계산 | `src/stereo_vision/stereo_link.c:stereo_link_take_at` → `stereo_geometry.c:stereo_reconstruct_point`, `stereo_pose.c:stereo_pose_reconstruct/stereo_pose_reconstruct_async_test` | RIGHT에서 calibrated geometry로 재구성. camera mm 좌표와 image 2D 동시 사용; CSV 상대좌표와 같지 않음 |
| 입력 gate | `input_pose_cnn_publish_stereo` → `input_pose_cnn_take_stereo` | 같은 frame, image/measured valid, metadata·age·시간/epoch 검사; 비검증 timing은 명시 async 시험 분기 |
| Agent1 | `main_integration.c` → `agent_pipeline.c:agent1_run_stereo` | image_pose + measured_pose + dt로 Human target 계산. 2D gripper 별도 경로 존재 |
| RIGHT Agent2 | `main_integration.c` → `agent2_run` | LIVE/record 허용·follow gate 조건에서 calibration/승인·제한. `agent_pipeline_sync_arm_motion` 상태 반영 |
| LEFT 출력 | `stereo_board_send_target/take_target`, `send_gripper/take_gripper` | RIGHT Human target과 별도 gripper 자료를 UART0로 전달. LEFT 로컬 Agent2/PWM gate 적용; 두 팔 PWM 위상 동기 보장 아님 |
| 출력 틱 | `platform_tick_due` → `motion_record_replay_control_tick` |20ms. LIVE는 agent2_tick→검증→agent3_apply_command. main에 agent3_run 추가 호출하면 중복 출력이므로 금지 |
| Agent3/PWM | `agent_pipeline.c:agent3_apply_command` → `output_control_update` → `servo_hal_apply_joint_command` → `src/drivers/servo_pwm_driver.c`의 bank/channel write/update | PS의 command→pulse/안전 검사 후 PL `servo_pwm_0`/`servo_pwm_arm2_0` IP 레지스터. 실측 각도 feedback 아님 |
| TRACE | `main_integration.c:TRACE_TK` → `trace.c:trace_tick` → `platform_vitis.c:platform_trace_tx/platform_uart_service` | 출력 처리 뒤 ASCII TK·다른 상태로그를 USB UART1에 전송. PC 수신 완성/유실 처리는 별도 |

**중요한 실제 경계:** 현재 `stereo_board_on_result` 호출의 metadata는 NULL이다. strict admission은 exposure_time_verified/fixed_geometry/localization_quality/shared clock epoch를 요구한다. `publish_async_test`/RECEIPT_LATEST 경로가 존재한다고 노출 동기화가 완료된 것은 아니다. 실제 보드 receipt 시간과 exposure 시간의 차이, 재획득·필터 상태는 별도 시험해야 한다.

### UART는 양방향 제어 통신

- FPGA/Zynq → Unity 목표: 로봇 상태, M0~M4 출력 명령, 유효성·적용 결과·타이밍. 현 TK 형식은 5절과 같이 코드에서 확인했지만 안정적인 Unity telemetry 채택 계약은 로봇팀 확인 필요.
- Unity → FPGA/Zynq 목표: V/T 상태 조회, E/A/S/X 명시 제어, m RGBY 설정. 새 wire protocol을 만들지 않고 기존 보드 계약을 따른다.
- 사용자 표시 명칭은 **UART**로 통일했다. 통신 패널 제목은 **UART / COMMUNICATION**이다. 임시 XYZ source와 RX/TX 내부 구현은 유지하며, 명칭 변경이 현재 Mock 버튼을 실제 송신으로 바꾸었다는 뜻은 아니다.

### HDMI 영상 Preview

`cnn_app.c:initialize_display` → `DisplayInitialize/DisplaySetMode/DisplayStart`로 display timing을 설정한다. XSA에서 `axi_vdma_0/M_AXIS_MM2S` → `keypoint_overlay_0` 및 `rgb2dvi_0`의 Video Out 입력/TMDS 출력이 확인된다. PS가 UART에 원본 프레임을 넣어 Unity로 보내는 구조가 아니다.

최종 PC 경로는 **FPGA HDMI Output → USB HDMI Capture Device → Windows Video Input → Unity Camera Preview**. 현재 Unity는 Mock이며 capture device enumerate/open/frame upload를 구현하지 않았다. HDMI 영상과 UART TK의 frame/time 동기 비교 역시 미구현이다.

### 추가 미검증 및 로봇팀 요청

1. 설치된 좌우 BOOT/bitstream/XSA/weight pack SHA 및 Pcam 배선·센서 실제 프레임률.
2. MIPI/VDMA 에러·DDR frame 선택 안정성, CNN IRQ/timeout·keypoint/marker 출력 원시 로그.
3. stereo calibration 단위/기준축, 실제 노출 동기 여부, strict/async gate·source epoch·재획득 결과.
4. UART0 binary 통신과 USB UART1 TK 스트림의 실측 주기·drop·접속/timeout 정책; Unity 수신 계약 승인.
5. capture device 모델/Windows driver/해상도/FPS/지연, 영상과 telemetry 시간 비교 방식.
6. 실제 Servo PWM 출력과 보드/Unity 시각화 비교. 이 문서에서 PL 구현 존재와 end-to-end 실물 PASS를 구분한다.


## 12. Clean Clone 재현성 및 UART UI 명칭 정리 — 2026-10-08

- 전달 대상은 저장소의 `Unity/`이다. 원격 `dev/unity` 기준 SHA `30b060feb8ec3ef33e0d3d2432b465270a2f120f`를 별도 clone하고, 감사의 424개 후보를 실제 파일·크기·SHA256으로 재검토해 선별 복사했다. 원격 전용 11개 파일은 바이트 단위로 보존했다.
- Startup INPUT의 최종 선택지는 **Manual / CSV·XYZ / UART**이다. 통신 화면 제목은 **UART / COMMUNICATION**이다. RX/TX 클래스·파서·상태 방향·프로토콜·연결 정책은 변경하지 않았다. 헤더의 RX OFF / TX OFF / REAL TX=0은 실제 연결 상태를 계속 구분한다.
- 원본 프로젝트에는 표시 문자열 3개 파일 수정 후, 사용자 추가 승인에 따라 13절의 UI·Humanoid visual 확장을 반영했다. `ControlStudioStartupMenu.cs`, `ControlStudioRuntimeUI.cs`, `ControlStudioUartPanel.cs`가 원본과 Clean Clone에서 SHA256 동일하다. 원본 메뉴의 UART 선택·START는 사용자 확인, Clean Clone Player는 EventSystem 입력으로 별도 검증했다.
- URP 17.3.0을 유지했다. DefaultVolumeProfile의 누락/테스트 subasset 9개와 obsolete Probe debug 리소스 참조 7개를 Clean Clone에서 정리했다. 정상 Volume 효과와 SSAO를 보존하고 전체 GUID 미해결 참조 0을 확인했다. Render Pipeline 전환이나 로봇 리모델링은 하지 않았다.
- 실제 Editor Play 및 Windows x64 Player에서 Manual/Mock UI와 두 CSV의 XYZ A/B를 실행했다. 하드웨어 포트 연결과 TX는 0이다. [상세 결과](CLEAN_CLONE_REPRODUCIBILITY.md)에는 시도 중 시험 fixture 오류와 최종 통과를 구분했다.
- `System.IO.Ports.dll`의 SHA256은 `c83bb693f3ac49285d5d53d406fadc2a960398c60a4927f6bd1d4a34cf78e554`이며 [Microsoft 공식 NuGet System.IO.Ports 6.0.0](https://www.nuget.org/packages/System.IO.Ports/6.0.0)의 `lib/netstandard2.0/System.IO.Ports.dll`과 일치한다. [MIT LICENSE](ThirdParty/System.IO.Ports-LICENSE.TXT)와 [원본 third-party notices](ThirdParty/System.IO.Ports-THIRD-PARTY-NOTICES.TXT)를 포함했다. 실제 Windows COM 연결과 이 라이브러리의 플랫폼 실행 경로는 아직 시험하지 않았고 DLL을 교체하지 않았다.

새 PC Import, 실제 UART 양방향 명령, Pcam/HDMI 캡처, 물리 PWM·각도·LEFT/RIGHT 동기화는 이번 가상 재현성 PASS에 포함하지 않는다. 사용자가 업로드를 승인했다. 전달 SHA는 이 문서를 포함한 Git 커밋과 완료 보고에서 확인한다.


## 13. Humanoid Shoulder / RIGHT·LEFT·BOTH 확장

### 실제 축과 5축 경계

`Assets/Resources/VisualProfiles/HumanoidRobot.prefab`의 좌우 계층은 각각 ShoulderRoot → ArmSourceFrame_Demo01 → ShoulderYaw → ShoulderPitch → ShoulderRoll → UpperArm → ElbowPitch → ElbowRoll → Forearm → WristPitch → WristRoll → ToolMount → Gripper다.

`HumanoidVisualRig.ApplyArmTarget()`은 저장 rest rotation에 local Y yaw, X pitch, Z roll을 곱한다. 기존 pivot, rest rotation, 부모, mesh는 수정하지 않았다. `HumanoidJointAdapter.FromSingleArmRight()`는 기존 Applied M0~M4를 팔/손목/그리퍼에만 변환하며 이전에는 shoulder=0이었다.

`robot_arm/include/output_controller/servo_config.h`의 enum은 ELBOW_ROLL / ELBOW_PITCH / WRIST_PITCH / WRIST_ROLL / GRIPPER 뒤 SERVO_COUNT=5다. Unity 어깨 UI는 펌웨어 6·7번째 채널을 추가하지 않는다.

### visual writer와 입력 소유권

- `HumanoidPreviewControl`: 좌우 어깨 목표와 왼팔 프리뷰 목표를 보관한다. 위치를 바꾸지 않고, frame dt 상한 50ms 및 초당 30°의 시각 보간으로 기존 rig target을 만든다. gripper 시각 보간은 초당 normalized 2다. Pause와 Startup에서는 시각 보간을 중단한다.
- `RobotVisualProfiles.Sync()`: 기존 단일 visual 적용 지점에서 RIGHT Applied + 어깨 프리뷰, LEFT 프리뷰 target을 `HumanoidVisualRig.ApplyArmTarget()`에 전달한다. 다른 LateUpdate Transform writer를 추가하지 않는다.
- `HumanoidServoPreviewPanel`: RIGHT의 다섯 팔 관절은 기존 `ManualServoSource.Set()`을 호출한다. LEFT는 visual state만, BOTH는 각각 명시 편집을 처리하며 독립 결과를 표시한다. CSV/XYZ/UART 소유권을 뺏지 않는다.
- G51 UI와 기존 controller/adapter/C/DLL/router는 변경하지 않았다. Camera/Presentation/Startup/ToolSocket의 기존 경로도 유지한다.

### 범위의 근거와 제한

| 항목 | 이번 가상 UI 범위 | 근거 / 한계 |
|---|---:|---|
| 양측 Shoulder Pitch | 0~10° | 기존 `FinalPresentationDirector.HumanAt()`의 시연 pitch 범위 재사용 |
| 양측 Shoulder Roll | -8~8° | 기존 Z 축의 작은 프리뷰 sweep; 원본에 물리 제한 없음, 별도 실물 계약 아님 |
| Yaw | UI 미노출, 0 delta | 기존 Y 축/rest 유지 |
| LEFT M0/M1/M2/M3/M4 표기 | 84~96 / 90~118 / 74~90 / 74~106 / 0~1 | 기존 시연 elbow/wrist delta를 호환 서보 표기로 표시한 프리뷰 범위. 실제 LEFT 서보 승인값 아님 |
| RIGHT M0~M4 | 기존 router 범위 | 기존 Agent2 승인/HOLD/보간 유지 |

BOTH arm UI는 왼팔 시연 범위에서 양측을 편집한다. 숫자/드래그 조작 전에는 서로 다른 좌우 값을 보존한다. 기존 preset은 RIGHT 5축만 저장하며 좌우 프리뷰는 별도 세션 상태다. 특정 시험 자세에서 구조 유지·말단 간격을 확인했지만, 전 관절 조합의 충돌 검증/충돌 회피/IK는 구현하지 않았다. 이 UI를 실물 안전 제어기로 사용하지 않는다.

### 독립 Mock 결과와 향후 UART 계약

Robot Control의 RIGHT=Robot 0, LEFT=Robot 1, BOTH=양측 대상이다. 기존 `RobotControlMockState` 두 인스턴스를 재사용한다. E/A/S/X/RGB/조회는 선택한 각 상태에 순서대로 적용된다. BOTH 선택만으로 실행되지 않는다. 한쪽 PWM OFF에서 A는 그쪽 Denied, 다른 쪽 Accepted이며 PARTIAL을 표시한다.

`MockResult`는 NotRequested/Accepted/Denied/Timeout을 구분할 자리다. 현재는 즉시 Mock 승인/거부만 생성하고 실제 timeout을 측정했다고 보고하지 않는다. 실제 보드 연동 시 COM별 session/command correlation/응답시간/deadline/재시도/부분 성공 정책을 로봇팀과 확정해야 한다. 두 COM 전송은 원자적 실행이나 PWM 위상 동기화를 보장하지 않는다. 기존 UART 프로토콜을 새로 정의하거나 송신을 연결하지 않았다.

E=PWM ON, A=허용 시 FOLLOW ON, S=새 추종 목표 차단/PWM 유지, X=PWM OFF/토크 해제라는 펌웨어 의미는 유지한다. 현재 Mock은 PWM/FOLLOW boolean만 표현하므로 실제 관성·진행 중 motion·토크 해제 결과는 미검증이다. 실제 TX=0, 보드 연결 없음이다.
