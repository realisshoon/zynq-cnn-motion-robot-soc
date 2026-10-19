# CNN 기반 5축 로봇팔 펌웨어

카메라/CNN 관측을 사람 오른팔 목표로 해석하고, 로봇별 보정·FK 안전검사·
속도/가속도 제한을 거쳐 서보를 제어한다. SD 녹화·반복 재생과 Unity UART 연동을 포함한다.
축 순서는 **elbow roll → elbow pitch → wrist pitch → wrist roll → gripper**다.

## 현재 코드 트리

Git 배포본의 주요 폴더와 진입 파일을 표시했다. 하위 구현 파일은 일부 생략했다.

```text
robot_arm/
├── src/
│   ├── app/
│   │   ├── main.c
│   │   ├── cnn/
│   │   │   ├── cnn_app.c
│   │   │   └── camera_tracking/, display_ctrl_hdmi/, vdma_api/, …
│   │   ├── uart/
│   │   │   ├── control_commands.c
│   │   │   ├── uart_settings.c
│   │   │   ├── telemetry.c
│   │   │   └── diagnostics.c
│   │   ├── board/
│   │   │   ├── right_input.c
│   │   │   ├── left_input.c
│   │   │   └── stereo_transport.c
│   │   └── robot/
│   │       ├── vision/
│   │       │   ├── human_target.c
│   │       │   ├── human_target/
│   │       │   └── stereo/
│   │       ├── control/
│   │       │   ├── robot_motion.c
│   │       │   ├── forearm_calibration_config.c
│   │       │   ├── forearm_safety_check.c
│   │       │   ├── gripper_control.c
│   │       │   └── servo_output.c
│   │       └── record_replay/
│   │           ├── motion_record_replay.c
│   │           ├── motion_library.c
│   │           └── motion_sd.c
│   └── drivers/
│       ├── pwm/
│       ├── uart/
│       └── system/
├── include/
│   ├── app/           cnn/, uart/, board/, robot/
│   ├── drivers/       pwm/, uart/, system/
│   └── common/
├── config/
├── vitis/
│   ├── setup_vitis.ps1
│   ├── xsa/
│   ├── reference/
│   └── boot/
├── pc/
├── sample/
├── scripts/
└── README.md
```

## 폴더별 역할

| 경로 | 담당 기능 |
|---|---|
| `src/app/main.c` | 공통 초기화, 서비스 호출 순서, 입력 처리와 20 ms 제어 틱 연결 |
| `src/app/cnn/` | 카메라·CNN 실행, 키포인트 관측, 카메라 추적, HDMI 오버레이·프레임 캡처 |
| `src/app/uart/` | 사용자/Unity 명령, 녹화·재생 메뉴, 설정, 진단·상태 텔레메트리 |
| `src/app/board/` | LEFT/RIGHT 입력 선택, 보드 간 사람 목표 송수신·추종 정책 |
| `src/app/robot/vision/` | 스테레오 3D 복원·필터링, 사람 전완·손목 목표 계산 |
| `src/app/robot/control/` | 로봇 관절 보정, FK 안전검사, 속도/가속도 제한, 그리퍼 판정·PWM 값 변환 |
| `src/app/robot/record_replay/` | 최종 명령 샘플 녹화·재생, 이름·선택·삭제, SD 동작 파일 입출력 |
| `src/drivers/pwm/` | 커스텀 로봇/카메라 PWM IP 접근과 로봇 출력 HAL |
| `src/drivers/uart/` | 콘솔 UART1·보드 간 UART0의 실제 입출력, IRQ·수신 버퍼·프로토콜 처리 |
| `src/drivers/system/` | 보드 초기화, GIC, 20 ms 타이머 등 공통 실행 기반 |
| `include/` | 외부에서 사용하는 공개 헤더. `src/`의 기능 구분에 대응하며 공통 타입은 `common/`에 배치 |
| `config/` | 통신 속도, 제어 주기, 로봇 기구 치수·공통 설정 |
| `vitis/` | 전체 펌웨어 빌드 환경, 하드웨어 플랫폼 XSA, 참고·과거 BOOT 자료 |

`app/`은 “무엇을 계산하고 언제 동작할지”를, `drivers/`는 “하드웨어에 어떻게 접근할지”를 담당한다.
예를 들어 `servo_output.c`는 출력 허가·PWM 변환 정책, `drivers/pwm/`은 PWM IP 접근을 맡는다.
`app/board/` 역시 보드 입력 정책이며, UART 레지스터를 다루는 드라이버 폴더와 구분한다.
구현 내부에서만 사용하는 헤더는 해당 소스 옆에 둔다.

## 실행 흐름과 좌우 보드

```text
카메라/CNN 관측
  → 스테레오 복원·필터 → 사람 전완·손목 목표
  → 로봇별 보정·FK 안전검사 → 승인 목표
  → 20 ms 속도/가속도 제한 → 서보 명령 → PWM 드라이버
```

| 역할 | 제어 대상 | 입력과 출력 |
|---|---|---|
| RIGHT | 로봇0 | 스테레오로 사람 오른팔 목표 계산 → JB CH0~4 |
| LEFT | 로봇1 | RIGHT의 사람 오른팔 목표 수신 → 자체 보정·안전검사 → JC CH0~4 |

- 역할에 따라 `right_input.c` 또는 `left_input.c` **하나만 빌드**하며, 공통 `main.c`를 사용한다.
- 새 관측 처리는 `human_target_update_stereo`·`robot_motion_set_target`, 제어 틱 진행은 `robot_motion_step`이 중심이다.
- 관측 갱신과 50 Hz 제어는 분리한다. 입력이 끊기면 마지막 승인 목표까지 진행한 뒤 유지한다.
- 녹화는 출력 경로의 최종 명령 샘플을 저장한다. 재생은 저장 샘플을 검증해 출력하며 사람 각도 보정을 다시 적용하지 않는다.
- 집게는 별도 관측 판정·유지 로직을 사용한다. 두 보드의 실시간 추종/녹화/재생 모드는 독립적이다.

## 주요 설정 위치

| 파일 | 변경하는 항목 |
|---|---|
| `config/firmware_config.h` | 콘솔 921600 baud, 보드 간 UART0 115200 baud, 제어 주기 20000 µs |
| `config/robot_board_config.h` | 로봇별 기구 치수, LEFT 그리퍼 PWM 제한 |
| `src/app/robot/control/forearm_calibration_config.c` | 관절 방향·오프셋·가동 범위·속도/가속도 제한 |
| `include/app/robot/vision/stereo/kalman_config.h` | 3D Kalman 필터 초기 계수 |

Unity와 연결되는 UART `A1`, `A2`, `P3`, `TK` 태그·필드 순서는 유지한다.
로봇 PWM은 부팅 시 OFF이며 `E`로 허용, `X`로 해제한다.
이 상태는 명령 출력 상태이지 실제 서보 위치·전류·온도 피드백이 아니다.

## Vitis 작업환경

저장소 루트의 PowerShell에서 실행한다. Windows의 Vitis 2020.2와
**짧고 비어 있는 외부 워크스페이스**가 필요하다.
기본 하드웨어 플랫폼은 `vitis/xsa/final_uart0.xsa`다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File robot_arm/vitis/setup_vitis.ps1 -Workspace D:\robot_right -StereoRole Right
powershell -NoProfile -ExecutionPolicy Bypass -File robot_arm/vitis/setup_vitis.ps1 -Workspace D:\robot_left -StereoRole Left
```

스크립트는 플랫폼/BSP를 생성하고 저장소 소스를 링크해 역할별 ELF와
`build_manifest.json`을 만든다. 앱 심볼은 `SERVO_PWM_DRIVER_USE_XILINX`와
역할 하나(`ROBOT_STEREO_RIGHT` 또는 `ROBOT_STEREO_LEFT`)다.
BOOT 패키징·SD 설치·실물 시험은 별도이며, 이 스크립트가 자동 실행하지 않는다.
폴더 재배치 전의 Vitis 워크스페이스는 그대로 재사용하지 않는다.

## 참고 자료와 배포 범위

- `pc/`: 과거 CSV 기반 UART 입력 시험 도구다. 현행 콘솔·Unity 명령이나 스테레오 추종의 실행 진입점이 아니다.
- `sample/`: 예제 영상이다. 펌웨어 빌드의 필수 입력이 아니다.
- `scripts/build.sh`, `scripts/setup_ubuntu.sh`: 과거 CMake 기반 호스트 도구다. 루트 `CMakeLists.txt` 제거 후에는 동작하지 않으며 현행 빌드에 사용하지 않는다.
- `vitis/reference/`, `vitis/boot/`: 참고 코드와 과거 빌드 자료다. 포함된 BOOT가 현재 소스로 만든 최신 배포본이라는 뜻은 아니다.
- `docs/`, `tests/`, `tools/`, `etc/`, `captures/`: 로컬 문서·검증·도구·로그 자료이며 Git 배포본에는 포함하지 않는다.

현재 소스와 마지막 SD 설치 BOOT가 동일하다고 단정하지 않는다.
코드 구조 정리는 서보 과열·기구 간섭·물리적 떨림의 해결이나 새 BOOT의 실물 검증을 의미하지 않는다.
