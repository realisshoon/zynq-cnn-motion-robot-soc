# CNN 기반 5축 로봇팔 펌웨어

Zybo Z7-20의 카메라/CNN 결과를 사람 전완 목표로 바꾸고, 보정·안전검사·50 Hz
궤적 제어를 거쳐 서보 PWM으로 출력한다. 로봇 동작의 SD 녹화·반복 재생과
Unity UART 연동을 지원한다.

## 현재 구성

| 빌드 | 제어하는 로봇 | 출력 | 입력 |
|---|---|---|---|
| RIGHT | 로봇0 | JB, CH0~4 | 스테레오로 복원한 사람 오른팔 |
| LEFT | 로봇1 | JC, CH0~4 | RIGHT에서 받은 동일 사람 오른팔 목표 |

- 각 보드는 자기 로봇만 제어한다. 두 로봇의 재생/실시간 추종 모드는 독립적이다.
- 로봇 PWM은 부팅 시 OFF다. `E`로 허용하고 `X`로 해제한다. 위치 피드백은 없다.
- 콘솔은 **921600 baud, 8N1**, 보드 간 UART0은 **115200 baud**다.
- 프레임 도착과 **20 ms 제어 틱**은 분리한다. 관측이 끊겨도 마지막 승인 목표까지 이동 후 유지한다.
- 최종 필터는 2D EMA + 3D Kalman이다. 과거 One Euro/Mono/동일 보드 양팔 분기는 제거했다.
- 현재 소스에는 기존 미배포 Unity Virtual Compute·SD 작업도 보존되어 있다.
  마지막 설치 BOOT와 동일한 소스라고 단정하지 않는다. 아래 빌드는 새 소스 후보의 검증이다.

## 먼저 읽을 핵심 8개 파일

| 파일 | 역할 | 대표 함수 |
|---|---|---|
| `src/app/main.c` | 공통 초기화·서비스·20 ms 제어 순서 | `main` |
| `src/app/uart/control_commands.c` | UART 명령·모드 전환·가상 세션 서비스 | `control_commands_handle` |
| `src/app/robot/vision/human_target.c` | 사람 관측 → 관절 목표 | `human_target_update_stereo` |
| `src/app/robot/control/robot_motion.c` | 초기화·보정·FK·복귀·속도/가속도 제한 | `robot_motion_set_target`, `robot_motion_step` |
| `src/app/robot/control/servo_output.c` | 출력 허가·PWM 변환·HAL 적용 | `servo_output_set_enabled`, `servo_output_apply` |
| `src/app/robot/control/gripper_control.c` | 독립 집게 판정·확인·유지 | `gripper_control_update` |
| `src/app/board/right_input.c` | 스테레오 입력 처리·사람 목표 송신 | `board_input_update` |
| `src/app/board/left_input.c` | 수신한 사람 목표·로봇1 목표 승인 | `board_input_update` |

좌우 입력 파일은 같은 API를 구현하며 **빌드에서 하나만 선택**한다. main·명령·
두 입력 파일에는 역할별 조건부 컴파일을 두지 않는다. 상수 프로파일로 보고 문자열과
역할별 수치를 제공한다. 공통 상태/API는 `include/app/robot/control/robot_pipeline.h`에서 확인한다.

새 관측에서는 `human_target_update_stereo` → `robot_motion_set_target`,
20 ms 틱에서는 `robot_motion_step` → `servo_output_apply` 순서다.
LEFT는 RIGHT에서 받은 사람 목표부터 자기 보정에 연결한다. PLAY는 기존 별도
샘플 검증·출력 경로를 유지하며 사람 각도 보정을 다시 적용하지 않는다.

하위 구현은 아래 기능별 폴더에 둔다. 앱의 계산·정책과 하드웨어 접근을 구분하고,
수학·하드웨어·저장 구현을 한 파일에 합치지 않는다.
UART `A1`, `A2`, `P3`, `TK` 태그와 명령 바이트는 그대로다.

이 README와 [수정 지침](AGENTS.md)이 Git 배포본의 기본 안내다.

## 코드 폴더 안내

```text
src/
  app/
    main.c
    cnn/                  CNN·카메라·HDMI·프레임 SD 캡처
    uart/                 사용자 명령·메뉴·설정·진단·telemetry
    board/                LEFT/RIGHT 입력·보드 간 통신 정책
    robot/
      vision/             stereo·필터·사람 관절 목표
      control/            로봇 보정·FK·속도/가속도·그리퍼·서보 변환
      record_replay/      녹화·재생·SD 동작 목록
  drivers/
    pwm/                  로봇/카메라 PWM IP·로봇 출력 HAL
    uart/                 콘솔 UART1·보드 간 UART0·입력 프로토콜
    system/               공통 초기화·GIC·20 ms 타이머
```

`include/`는 같은 기능별 이름으로 공개 헤더를 제공한다. 구현 전용 헤더는 해당
소스 옆에 둔다. `include/common/`은 공통 타입, `config/`는 공통 설정,
`docs/`, `tests/`, `tools/`, `etc/`는 로컬 전용 자료이며 Git 배포본에 포함하지 않는다.
로컬 문서·시험·도구·데이터 없이도 아래 Vitis 빌드와 기본 CMake 빌드가 가능하다.
기존 `integration/`, `cnn_firmware/`, `robot_calibration/`, `output_controller/` 소스
폴더 또는 10월 16일 배치를 사용하는 Vitis 워크스페이스는 재생성해야 한다.
테스트 폴더 이름과 UART 명령은 유지한다.

## Vitis 빌드

저장소 루트의 PowerShell에서 실행한다. Vitis 2020.2와 짧고 비어 있는 외부
워크스페이스가 필요하다. XSA는 `robot_arm/vitis/xsa/final_uart0.xsa`다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File robot_arm/vitis/setup_vitis.ps1 -Workspace D:\robot_right -StereoRole Right
powershell -NoProfile -ExecutionPolicy Bypass -File robot_arm/vitis/setup_vitis.ps1 -Workspace D:\robot_left -StereoRole Left
```

앱 심볼은 `SERVO_PWM_DRIVER_USE_XILINX`와 역할 하나(`ROBOT_STEREO_RIGHT` 또는
`ROBOT_STEREO_LEFT`)뿐이다. `ROBOT_TRACE` 옵션 없이 진단을 항상 포함한다.
링커는 앱 DDR을 `0x00100000`부터 `0x09F00000` 바이트로 제한하여 CNN 프레임
버퍼와 분리한다. 스크립트는 플랫폼/BSP/ELF를 빌드하며 **SD를 쓰거나 로봇을 구동하지 않는다**.
BOOT 패키징·설치·실물 검증은 별도 단계다. 이전 워크스페이스를 그대로 재사용하지 않는다.

## 호스트 검증

CMake 기본 빌드는 테스트 없이 이식 가능한 CPU 계산·제어 정적 라이브러리를 만든다.
카메라·보드 입력·UART·PWM 하드웨어를 포함하는 전체 펌웨어 ELF는 위 Vitis 절차로 만든다.
호스트 정적 라이브러리 빌드는 실제 보드 구동이나 펌웨어 전체 링크 검증이 아니다.

```powershell
cmake -S robot_arm -B D:\robot_core_right -DROBOT_BOARD_ROLE=RIGHT
cmake --build D:\robot_core_right --config Debug
cmake -S robot_arm -B D:\robot_core_left -DROBOT_BOARD_ROLE=LEFT
cmake --build D:\robot_core_left --config Debug
```

`BUILD_TESTING` 기본값은 OFF다. 로컬 검증 묶음의 `tests/`, `tools/`, `etc/`를 별도로
복원한 환경에서만 `-DBUILD_TESTING=ON`으로 구성한다. 검증 묶음은 Git에서 제공하지 않는다.
ON 상태에서 필수 자료가 없으면 구성 단계에서 명시적으로 실패하며 시험을 조용히 생략하지 않는다.
호스트 시험은 mock·Xilinx 정수 타입 stub을 사용하고 Vitis는 실제 구현/BSP를 사용한다.

2026-10-19 소스 재배치 검증에서는 LEFT/RIGHT ARM ELF 빌드, 역할당 6,000틱 출력 동일성,
32개 녹화·재생 경계 사례와 Virtual Compute 시험을 확인했다. 넓은 호스트 회귀는
25개 중 14개 통과·기존 실패 11개 유지였다. LEFT CMake FK 시험의 RIGHT 기구 치수
기대값 문제, Virtual/Real 손목 복귀 계산 차이, Release 시험 assert 비활성화는 미해결이다.
이 결과는 새 BOOT 설치·실물 시험이나 모든 기능의 검증 완료를 의미하지 않는다.

네 로컬 전용 폴더를 제외한 별도 패키지에서도 LEFT/RIGHT 기본 CMake 빌드와 기존
역할별 BSP·링커를 사용한 ARM 전체 컴파일·링크를 확인했다. 로컬 검증 묶음이 있는
환경에서는 `BUILD_TESTING=ON` 전체 빌드와 역할당 선택 시험 6개를 통과했고,
묶음이 없는 환경에서 ON을 지정하면 구성 단계에서 거부되는 것도 확인했다.

## 주요 설정

- `config/firmware_config.h`: 콘솔·보드 UART 속도, 제어 주기, 필터 식별명.
- `config/robot_board_config.h`: 두 기구 길이와 LEFT 그리퍼 PWM 제한.
- `include/app/robot/vision/stereo/kalman_config.h`: Kalman 초기 계수.
- `src/app/robot/control/forearm_calibration_config.c`: 관절 방향·오프셋·범위·운동 제한.
- LEFT elbow pitch는 30°/s·60°/s², LEFT 집게는 1722~2200 µs를 유지한다.
  RIGHT elbow roll 거울 방향, 손목 제한, 홈 자세도 이번 정리로 바꾸지 않는다.

## 배포 범위

Git에는 소스·헤더·설정·빌드 파일과 이 README 및 AGENTS.md를 유지한다.
`docs/`, `tests/`, `tools/`, `etc/`의 로컬 파일은 보존하지만 추적하지 않는다.
상세 UART 명세와 운영 도구·시험 자료가 필요한 팀에는 별도로 전달해야 한다.
과거 Agent 테스트 README는 로컬 검증 묶음을 전제로 한 이력이며 새 환경의 빌드 안내가 아니다.

`captures/`, 과거 BOOT 및 외부 하네스는 복구·분석 자료이지 런타임 소스가 아니다.
서보 과열·기구 걸림·물리적 떨림은 코드 정리로 해결됐다고 주장하지 않는다.
