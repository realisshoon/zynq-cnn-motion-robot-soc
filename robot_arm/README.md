# robot_arm

Zybo Z7-20(Zynq-7020)의 PS(ARM Cortex-A9, 베어메탈)에서 동작하는 **6축 서보 로봇팔 제어 소프트웨어**입니다.
사람 팔의 2D 관절 좌표를 받아 로봇팔 관절 각도로 바꾸고, 안전검사와 속도제한을 거쳐 서보 PWM으로 출력합니다.
사람 자세를 만드는 CNN 가속기(PL)는 별도로 개발 중이며, 입력은 현재 PC에서 UART로 보내는 pose 프레임으로 대신합니다.

## 목차

- [개요](#개요)
- [통합 구조 (main_integration)](#통합-구조-main_integration)
  - [파일](#파일)
  - [main의 흐름](#main의-흐름)
  - [함수 5개](#함수-5개)
  - [상태: AgentPipelineContext](#상태-agentpipelinecontext)
  - [하드웨어 경계 5개](#하드웨어-경계-5개)
  - [지켜야 할 호출 규약](#지켜야-할-호출-규약)
  - [코드 읽는 순서](#코드-읽는-순서)
- [프로젝트 구조](#프로젝트-구조)
- [호스트(PC) 빌드와 테스트](#호스트pc-빌드와-테스트)
- [Vitis 작업환경 (Zybo Z7-20)](#vitis-작업환경-zybo-z7-20)
  - [준비물](#준비물)
  - [빠른 시작 (저장소 루트에서)](#빠른-시작-저장소-루트에서)
  - [스크립트가 하는 일](#스크립트가-하는-일)
  - [주의](#주의)
- [Vitis 개발 워크플로](#vitis-개발-워크플로)
  - [일상 작업 순서](#일상-작업-순서)
  - [코드를 추가할 때](#코드를-추가할-때)
  - [디버깅 요령](#디버깅-요령)
  - [PC에서 pose 보내기](#pc에서-pose-보내기)
- [UART 로그 (trace)](#uart-로그-trace)
  - [켜는 방법](#켜는-방법)
  - [로그 받기](#로그-받기)
  - [줄 형식](#줄-형식)
  - [값 읽는 법](#값-읽는-법)
  - [영향과 한계](#영향과-한계)
  - [변경 지점](#변경-지점)
- [하드웨어(XSA)가 바뀌었을 때](#하드웨어xsa가-바뀌었을-때)
- [문제 해결](#문제-해결)

## 개요

```text
PC UART (현재) / CNN 가속기 (추후)
      │  HumanPose2D      사람 관절 2D 좌표 (1280x720)
      ▼
Agent1  human_target_angle  → HumanJointTarget  사람 관절 각도
      ▼
Agent2  robot_calibration   → JointCommand      검증, 매핑, 안전검사, 속도제한/스무딩 (20 ms 틱)
      ▼
Agent3  output_controller   → ServoPwmCommand   서보 PWM(µs) 변환과 범위 검사
      ▼
servo_pwm AXI IP (PL) → MG996R 서보 6채널 (5관절 + 그리퍼, 50 Hz)
```

모듈은 `include/common/robot_types.h`의 구조체로만 서로 통신합니다.

| 모듈 | 폴더 | 역할 |
|---|---|---|
| 입력 | `uart_pose/` | PC가 UART로 보낸 pose 프레임(36바이트, CRC16) 수신과 파싱 |
| Agent1 | `human_target_angle/` | HumanPose2D → 사람 관절 각도 |
| Agent2 | `robot_calibration/` | 검증, 각도 unwrap, 서보 각도 매핑, 관절 한계, 안전검사, 속도제한과 스무딩 |
| Agent3 | `output_controller/`, `drivers/` | JointCommand → 서보 PWM(µs) 변환과 검증, servo_pwm 레지스터 쓰기 |
| 통합 | `integration/` | 세 Agent를 잇는 얇은 wrapper와 진입점, 하드웨어 경계(`platform.h`, `input_pose.h`) |

실행파일의 `main`은 `src/integration/main_integration.c`이고, 루프 하나에 시계가 둘입니다.

- **프레임 경로(가변 주기)**: pose가 도착하면 Agent1 → Agent2로 목표를 갱신합니다.
- **제어 틱 경로(고정 20 ms, AXI Timer 인터럽트)**: Agent2가 램프를 한 틱 진행하고 Agent3가 서보 레지스터에 씁니다.

코드 수준의 구조는 [통합 구조](#통합-구조-main_integration)를 보세요.

## 통합 구조 (main_integration)

세 Agent(Agent1, 2, 3)는 각자 독립적으로 만든 모듈이고, `integration/`은 이 셋을 **얇은 wrapper로 이어서** 하나의 프로그램으로 만듭니다.
Agent 원본 소스는 수정하지 않고 공개 API만 호출합니다. 처음 코드를 읽는다면 이 절의 순서대로 보세요.

### 파일

| 파일 | 역할 |
|---|---|
| `src/integration/main_integration.c` | 진입점 `main`. 초기화 3줄과 무한 루프(프레임 경로, 틱 경로) |
| `include/integration/agent_pipeline.h`, `src/integration/agent_pipeline.c` | Agent를 잇는 함수 5개와 파이프라인 상태(`AgentPipelineContext`) |
| `include/integration/platform.h` | 하드웨어 경계 선언: `platform_init()`, `platform_tick_due()` |
| `include/integration/input_pose.h` | 입력 경계 선언: `input_pose_init()`, `input_pose_ready()`, `input_pose_take()` |
| `src/integration/platform_vitis.c` | 위 두 경계의 Vitis 구현(AXI Timer 인터럽트 틱, PS UART 수신) |
| `include/integration/trace.h`, `src/integration/trace.c` | UART 디버그 로그(trace). `ROBOT_TRACE`를 정의한 빌드에서만 켜진다([UART 로그](#uart-로그-trace)) |
| `tests/integration/test_integration_smoke.c` | 같은 `main`을 가짜 경계(테스트 코드)로 호스트에서 실행하는 스모크 테스트 |
| `tests/integration/test_trace.c` | trace의 호스트 테스트(`-DROBOT_TRACE`로 빌드) |

### main의 흐름

```c
int main(void)
{
    AgentPipelineContext pipeline;   /* 파이프라인 상태(프레임 사이에 유지) */
    HumanPose2D pose;
    float dt_sec;

    if (platform_init() != 0) return -1;                  /* UART, 타이머, 서보 HAL 초기화 */
    input_pose_init();                                    /* 입력 어댑터 초기화 */
    if (agent_pipeline_init(&pipeline) != 0) return -1;   /* Agent 초기화, 홈 자세, 서보 enable */

    for (;;) {
        /* 프레임 경로(가변 주기): 새 pose가 오면 목표를 갱신한다. */
        if (input_pose_ready() && input_pose_take(&pose, &dt_sec)) {
            agent1_run(&pipeline, &pose, dt_sec);
            agent2_run(&pipeline);
        }

        /* 제어 틱 경로(고정 20 ms): 램프를 한 틱 진행해서 서보에 적용한다. */
        if (platform_tick_due()) {
            agent2_tick(&pipeline);
            agent3_run(&pipeline);
        }
    }
}
```

시계가 둘인 이유: Agent2의 속도제한(`max_delta_deg`)이 20 ms 틱 기준이라서, pose 도착(가변)과 서보 갱신(고정)을 분리합니다.
내비게이션과 같습니다. 목적지 갱신(프레임 경로)은 가끔 하고, 운전(틱 경로)은 계속합니다.
프레임 사이에도 틱은 계속 돌아서 서보는 항상 부드럽게 목표를 향해 움직입니다.

실제 `main`에는 UART 로그용 `TRACE_*` 호출 7줄이 더 있습니다(`[TRACE]` 태그). `ROBOT_TRACE`를 정의하지 않으면 아무것도 하지 않아서
위 발췌에서는 뺐습니다. [UART 로그](#uart-로그-trace)를 보세요.

### 함수 5개

| 함수 | 데이터 흐름 | 하는 일 |
|---|---|---|
| `agent_pipeline_init` | (부팅) | Agent 3개 초기화 → **홈 자세로 Agent2 부트스트랩** → PWM 변환 → shadow 6개 쓰기 + UPDATE → **그 다음 서보 enable**. 서보가 켜지자마자 튀지 않게 하는 순서입니다 |
| `agent1_run` | `HumanPose2D` → `HumanJointTarget` | Agent1 실행. 다음 단계 진행 여부는 반환값이 아니라 **출력의 `valid`**로 판단합니다(짧은 끊김 동안에도 마지막 정상값을 valid로 유지하기 때문). 결과는 복사해 둡니다 |
| `agent2_run` | `HumanJointTarget` → 목표 갱신 | 검증 → unwrap(±180° 경계 처리) → 서보 각도 매핑, 관절 한계, 안전검사(`apply`) → 승인되면 `set_target`. 거부되면 **마지막으로 승인된 목표를 유지**합니다. 직전과 같은 명령이면 재계획하지 않습니다 |
| `agent2_tick` | → `JointCommand` | 램프를 한 틱(20 ms) 진행합니다. 관절마다 틱당 이동 상한이 있습니다 |
| `agent3_run` | `JointCommand` → 서보 레지스터 | PWM(µs) 변환과 범위 검사 → shadow 6개 쓰기 → UPDATE. 변환이나 검사에 실패하면 레지스터에 쓰지 않습니다 |

### 상태: AgentPipelineContext

`main`의 지역 변수 하나(`pipeline`)를 모든 함수에 넘깁니다. C에는 객체가 없어서 상태를 한 구조체에 모았습니다.

- Agent 상태: `unwrap`(각도 이어 붙이기 기억), `motion`(Agent2의 현재 위치와 목적지)
- 프레임 경로 값: `pose`, `target`(Agent1 출력의 복사본), `command`(마지막으로 승인한 명령)
- 틱 경로 값: `output`(이번 틱의 Agent2 출력), `pwm`(마지막 PWM 명령)
- 디버깅용 카운터: `frames_in`, `targets_valid`, `commands_accepted`, `commands_rejected`, `retargets`, `ticks`, `servo_writes`, `servo_errors`

### 하드웨어 경계 5개

`main`은 시계와 입력을 이 5개 함수로만 봅니다. 선언은 헤더에만 있고 구현은 환경마다 다릅니다.

| 함수 | 하는 일 |
|---|---|
| `platform_init()` | UART, 타이머, 서보 HAL 초기화. 성공 0, 실패 -1(실패하면 서보를 켜기 전에 종료) |
| `platform_tick_due()` | 20 ms 틱이 왔으면 1. 인터럽트가 올리는 카운터를 읽어 한 틱씩 소비합니다(밀린 틱은 버림) |
| `input_pose_init()` | 입력 어댑터 초기화 |
| `input_pose_ready()` | 새 `HumanPose2D`가 준비됐으면 1 |
| `input_pose_take()` | pose와 직전 프레임과의 간격 `dt_sec`을 넘깁니다(첫 프레임은 0.05초) |

- **보드(Vitis)**: `src/integration/platform_vitis.c`가 구현합니다. AXI Timer 인터럽트(GIC ID 61)로 20 ms 틱을 만들고,
  PS UART1(115200)에서 Agent1의 `uart_pose_rx_*`로 pose를 받습니다.
- **호스트(PC) 테스트**: 테스트 코드가 가짜 구현을 제공해서 보드 없이 전체 흐름을 실행합니다.
- **새 입력 방식(CNN 등)**은 `input_pose_*` 3개만 새로 구현하면 됩니다. Agent들은 입력 방식을 모릅니다.

### 지켜야 할 호출 규약

- Agent1 다음 단계 진행은 반환값(1/0/-1)이 아니라 출력 `valid`로 판단한다.
- Agent2는 항상 검증 → unwrap → apply → set_target 순서로 부른다. 검증되지 않은 타겟을 unwrap에 넣지 않는다.
- Agent2는 첫 `set_target`을 램프 없이 바로 적용하므로, 홈 자세로 먼저 부트스트랩한다.
- 서보는 shadow 6개 → UPDATE → enable 순서로 켠다.
- 인터럽트(ISR) 안에서는 Agent를 실행하지 않는다. ISR은 카운터만 올린다.
- 사용하는 팔(`AGENT_PIPELINE_ACTIVE_ARM`, 현재 `POSE_ARM_RIGHT`)은 입력 좌표의 팔과 반드시 같아야 한다.
- 홈 자세(`agent_pipeline.c`의 `k_home_pose`: 5관절 90°, 그리퍼 0.5 = 1500 µs)는 **자리표시자**다.
  실측 후 RTL의 서보 reset 값과 함께 교체한다.

### 코드 읽는 순서

1. `main_integration.c` (33줄)
2. `agent_pipeline.h`의 주석
3. `agent_pipeline.c`를 위에서 아래로
4. `platform.h`, `input_pose.h`
5. `platform_vitis.c`

## 프로젝트 구조

```text
robot_arm/
├── include/          # 모듈별 헤더 (모듈 경로로 include)
│   ├── common/       # 공유 자료형 (HumanPose2D, HumanJointTarget, JointCommand ...)
│   ├── human_target_angle/  # Agent1
│   ├── robot_calibration/   # Agent2
│   ├── output_controller/   # Agent3
│   ├── drivers/      # servo_pwm 레지스터 드라이버
│   ├── uart_pose/    # UART pose 프로토콜과 수신
│   └── integration/  # Agent 연결, 플랫폼 경계, UART 로그(trace)
├── src/              # 구현 (모듈별 폴더, src/main.c 는 Agent3의 HAL 데모)
├── tests/            # 모듈별 호스트 테스트와 통합 스모크 테스트
├── config/           # 로봇, 카메라 공통 설정 (robot_config.h)
├── docs/             # 인터페이스, 좌표계, 설계 로그, CNN 인터페이스, 다이어그램
├── pc/               # PC 쪽 도구 (send_pose_uart.py: pose 프레임 UART 송신)
├── scripts/          # Ubuntu 설정과 빌드 보조 스크립트
└── vitis/            # Vitis 작업환경 재현 스크립트와 XSA (Zybo Z7-20)
```

헤더는 모듈 경로로 include 합니다.

```c
#include "common/robot_types.h"
#include "robot_calibration/robot_calibration.h"
```

## 호스트(PC) 빌드와 테스트

루트 `CMakeLists.txt`와 `scripts/build.sh`는 삭제된 `src/robot_calibration/kinematics_2d.c`,
`tests/robot_calibration/test_kinematics_2d.c`를 참조해서 지금은 configure가 실패합니다(정리 예정).
그동안은 gcc로 직접 빌드합니다. `robot_arm/` 폴더에서 실행하세요(`build/`는 git이 무시합니다).

```bash
mkdir -p build
A1="src/human_target_angle/agent1_stage.c src/human_target_angle/pose_hand.c src/human_target_angle/pose_joint.c src/human_target_angle/pose_mapping.c src/human_target_angle/pose_math.c src/human_target_angle/pose_reconstruction.c src/human_target_angle/pose_tracking.c"
A2="src/robot_calibration/motion_control.c src/robot_calibration/motion_limits.c src/robot_calibration/motion_smoothing.c src/robot_calibration/robot_calibration.c src/robot_calibration/robot_calibration_config.c src/robot_calibration/safety_check.c"
A3="src/output_controller/output_control.c src/output_controller/servo_config.c src/output_controller/servo_control.c src/output_controller/servo_hal.c src/drivers/servo_pwm_driver.c"
F="-std=c99 -Wall -Wextra -Wpedantic -Iinclude -Iconfig"

# Agent2 테스트 4개
for t in test_robot_calibration test_motion_limits test_motion_smoothing test_safety_check; do
  gcc $F $A2 tests/robot_calibration/$t.c -lm -o build/$t && build/$t
done

# Agent1 테스트
gcc $F $A1 tests/human_target_angle/test_pose_mapping.c -lm -o build/test_pose_mapping && build/test_pose_mapping

# Agent1 -> 2 -> 3 통합 스모크 테스트 (UART 파서, 부팅 순서, 20 ms 틱, 안전검사 거부, HOLD, 각도 wrap)
gcc $F $A1 $A2 $A3 src/uart_pose/uart_pose_protocol.c src/integration/agent_pipeline.c \
    tests/integration/test_integration_smoke.c -lm -o build/test_integration_smoke && build/test_integration_smoke

# UART 로그(trace) 테스트: -DROBOT_TRACE를 모든 파일에 준다(trace.c는 테스트가 #include 한다)
gcc $F -DROBOT_TRACE $A1 $A2 $A3 src/integration/agent_pipeline.c \
    tests/integration/test_trace.c -lm -o build/test_trace && build/test_trace
```

`src/drivers/servo_pwm_driver.c`는 호스트에서 mock으로 빌드됩니다(서보 레지스터 쓰기를 메모리에 기록).
Vitis 빌드에서는 컴파일 심볼 `SERVO_PWM_DRIVER_USE_XILINX`가 켜져 실제 하드웨어에 씁니다.

## Vitis 작업환경 (Zybo Z7-20)

`robot_arm/` 소스를 Vitis로 빌드하고 보드에서 실행하는 환경을 팀원 모두가 똑같이 만들기 위한 안내입니다.
손으로 클릭하며 설정하지 말고 스크립트를 쓰세요.

### 준비물

- Windows, **Vitis 2020.2** (XSA를 만든 Vivado도 2020.2 — 버전이 다르면 맞지 않습니다)
- 이 저장소를 clone한 폴더
- Zybo Z7-20 + USB(JTAG/UART) 케이블

### 빠른 시작 (저장소 루트에서)

```powershell
powershell -ExecutionPolicy Bypass -File robot_arm\vitis\setup_vitis.ps1 -Workspace D:\vws
```

- `-Workspace`는 아직 없는 **짧은 경로**여야 합니다(80자 이하, 예: `D:\vws`). 아래 "주의 1" 참고.
- 몇 분 뒤 `D:\vws\robot_testbench\Debug\robot_testbench.elf`가 만들어지고 `완료:`가 출력됩니다.
- 그 다음 Vitis를 열어 워크스페이스를 `D:\vws`로 지정하세요.
- 스크립트가 도는 동안에는 Vitis를 열지 마세요(워크스페이스가 잠깁니다).

| 경로 | 내용 |
|---|---|
| `vitis/setup_vitis.ps1` | 환경 생성 스크립트 |
| `vitis/xsa/robot_test_wrapper.xsa` | 하드웨어 사양(PS 설정, AXI Timer, servo_pwm IP, 비트스트림). Vivado 프로젝트에서 내보낸 파일 |

### 스크립트가 하는 일

1. XSA로 **플랫폼**(standalone, ps7_cortexa9_0)을 만들고 BSP를 빌드합니다.
2. 빈 **앱 `robot_testbench`**를 만들고 `robot_arm/src`를 **링크**(복사 아님)로 붙입니다
   (`drivers`, `human_target_angle`, `integration`, `output_controller`, `robot_calibration`, `uart_pose`, `main.c`).
3. include 경로 2개(`robot_arm/include`, `robot_arm/config`), 컴파일 심볼 `SERVO_PWM_DRIVER_USE_XILINX`,
   링크 라이브러리 `m`(-lm)을 설정합니다. 심볼이 없으면 서보 드라이버가 **mock**으로 빌드되어
   하드웨어에 아무것도 쓰지 않습니다(에러 없이 서보가 안 움직입니다).
4. 빌드에서 뺄 파일 2개를 등록합니다. 실행파일의 `main`은 `src/integration/main_integration.c` 하나여야 하기 때문입니다.
   - `src/main.c` (Agent3의 HAL 데모)
   - `src/human_target_angle/main_integration_shape.c` (Agent1의 참고용 main)
5. 앱을 빌드합니다.

### 주의

1. **워크스페이스 경로는 짧게.** 플랫폼(BSP) 안쪽 경로가 워크스페이스 아래로 약 170자까지 깊어져서,
   경로가 길면 Windows 경로 제한(260자)에 걸려 BSP 생성이 `파일 이름이나 확장명이 너무 깁니다`로 실패합니다.
2. **링크 안의 파일을 Vitis에서 편집하거나 삭제하지 마세요.** 링크가 저장소 원본을 직접 가리키므로 원본이 바뀝니다.
   편집은 다른 편집기에서 하고, Vitis는 빌드와 디버그용으로 씁니다.
3. **코드 갱신**은 `git pull` 후 Vitis에서 프로젝트를 새로고침(F5)하고 다시 빌드하면 됩니다.
   링크된 폴더 안에 새로 생긴 파일은 자동으로 잡힙니다.
4. `robot_arm/src/` **바로 아래**에 새 폴더나 새 파일을 만들면 자동으로 링크되지 않습니다.
   새 워크스페이스에서 스크립트를 다시 돌리거나 그 항목의 링크를 추가하세요.
5. 링크 경로는 절대경로로 저장됩니다. 저장소를 다른 폴더로 옮기면 스크립트를 새 워크스페이스로 다시 돌리세요.
6. **스크립트는 Debug 설정만 구성합니다.** include 경로, 컴파일 심볼(`SERVO_PWM_DRIVER_USE_XILINX`), `-lm`이 Debug 설정에만 들어가고
   Release 설정에는 없습니다. Release가 필요하면 같은 설정을 넣어야 합니다. 특히 `SERVO_PWM_DRIVER_USE_XILINX`가 빠지면
   서보 드라이버가 PC 테스트용 mock(레지스터 대신 메모리에 기록)으로 빌드되어 서보 PWM이 나오지 않습니다.
   진짜 드라이버인지는 Vitis 툴체인의 `arm-none-eabi-nm`(`C:/Xilinx/Vitis/2020.2/gnu/aarch32/nt/gcc-arm-none-eabi/bin`)으로
   `arm-none-eabi-nm <앱>.elf | findstr /i mock`을 실행해 아무것도 나오지 않는 것으로 확인합니다.

## Vitis 개발 워크플로

### 일상 작업 순서

1. **최신화**: `git pull`. 작업 브랜치는 `feat/robot/*`, PR 대상은 `dev/robot`입니다.
2. **수정**: 저장소 파일을 VS Code 등 다른 편집기에서 고칩니다(Vitis에서 링크 파일 편집 금지).
3. **호스트 테스트**: 위 gcc 명령으로 해당 모듈 테스트와 통합 스모크 테스트를 돌립니다. 보드 없이 논리를 먼저 확인합니다.
4. **Vitis 빌드**: 프로젝트 새로고침(F5) → 필요하면 Project → Clean → 빌드. 오류는 Problems 뷰에서 봅니다.
5. **보드 실행**: 앱 우클릭 → **Debug As → Launch on Hardware (Single Application Debug)**.
   Program FPGA와 ps7_init은 기본으로 켜져 있고, 비트스트림은 플랫폼의 것을 씁니다. 처음 확인하는 순서:
   1. **서보 전원 OFF**에서 시리얼 터미널(115200 8N1)에 배너 `[platform] ready (tick 20 ms)`가 한 줄 나오는지 (그 뒤에 출력이 없는 것이 정상)
   2. 20 ms 틱 인터럽트가 도는지 (아래 "디버깅 요령")
   3. 로직 분석기나 스코프로 PWM 프레임(20 ms 주기)과 펄스 폭(홈 자세 1500 µs)
   4. 서보를 **무부하(혼 분리)**로 연결. 홈 자세와 관절 캘리브레이션 값은 아직 실측 전입니다
   5. PC에서 pose 송신 (아래 "PC에서 pose 보내기")
6. **커밋과 PR**: 테스트가 통과하면 커밋하고 push한 뒤 `dev/robot`으로 PR을 올립니다.

### 코드를 추가할 때

- 새 파일은 기존 링크 폴더 안(`src/<모듈>/`)에 두고 헤더는 `include/<모듈>/`에 둡니다. Vitis에서 F5만 하면 잡힙니다.
- `xparameters.h`, `xuartps.h` 같은 Xilinx 전용 헤더를 쓰는 코드는 `*_vitis.c`로 분리하고 호스트 빌드와 테스트에는 넣지 않습니다
  (예: `uart_pose/uart_pose_rx_vitis.c`, `integration/platform_vitis.c`).
  공용 코드가 Vitis 전용 헤더에 의존하면 호스트 테스트가 깨집니다. 의존 방향은 "Vitis 전용 → 공용" 한 방향입니다.
- `src/` 바로 아래에 새 폴더를 만들었다면 스크립트를 새 워크스페이스로 다시 돌립니다.

### 디버깅 요령

Debug As로 실행한 상태에서 합니다. **Run**으로 실행하면 브레이크포인트와 변수 보기가 안 됩니다.

- **변수 보기**: 프로그램을 Suspend → Debug 뷰에서 `main()` 프레임을 선택 → Variables 뷰에서 `pipeline`을 펼칩니다.
  `ticks`(20 ms 틱 수), `servo_writes`, `servo_errors`, `pwm.*_pwm_us`(서보로 나가는 µs), `output.*`(Agent2 출력)를 봅니다.
- **틱 인터럽트 확인**: `integration/platform_vitis.c`의 `s_tick_count++;` 줄에 브레이크포인트를 겁니다.
  걸리면 타이머 → GIC → CPU → 드라이버 → 콜백 경로가 살아 있는 것입니다.
  `s_tick_count`가 초당 약 50씩 늘고 `s_tick_overruns`가 0인지 확인하고, 확인 후 브레이크포인트를 지웁니다(남기면 20 ms마다 멈춥니다).
- **PL 레지스터 직접 읽기**: Xilinx 메뉴 → XSCT Console에서 CPU를 멈춘 상태로
  `mrd 0x43C00000 8`(servo_pwm 레지스터 0x00~0x1C, 채널 값 1500=`0x5DC`, CONTROL(0x18)=1이 정상),
  `mrd 0x42800000 3`(AXI Timer). 주소는 XSA가 만든 `xparameters.h` 기준입니다.
- **프로그램을 멈추거나 종료해도 PL의 PWM은 마지막 값으로 계속 나옵니다.** 서보를 바로 멈추려면 서보 전원을 끊으세요
  (FPGA를 다시 프로그램해도 PWM이 꺼져서 서보가 힘이 빠집니다).
- 로그 UART(PS UART1)와 pose 송신이 같은 COM 포트라서 시리얼 터미널과 송신 스크립트는 동시에 열 수 없습니다.
- **프레임/틱마다의 입출력을 PC에서 보고 싶을 때**는 [UART 로그](#uart-로그-trace)를 켭니다(`ROBOT_TRACE`).

### PC에서 pose 보내기

```powershell
python robot_arm/pc/send_pose_uart.py --port COM3 --csv <pose CSV 경로> --hz 20
```

- `--csv`에는 Agent1이 만든 pose CSV를 지정합니다(예: `example_pose2d_1280x720_20hz.csv`).
  열은 `frame_id,time_sec,frame_valid,shoulder_l_x,shoulder_l_y,shoulder_l_valid, ...`(shoulder_l, shoulder_r, elbow, wrist, finger1, finger2)입니다.
- 처음에는 `--loop`를 붙이지 마세요(CSV의 끝과 처음 자세가 이어지지 않아 점프가 실제 움직임처럼 들어갑니다).
- 시리얼 터미널을 닫고 실행하세요. 송신 중에는 스크립트가 보드 로그도 같이 출력합니다(`--no-board-text`로 끔).
- 프로그램은 Suspend가 아니라 실행 중(Resume) 상태여야 프레임을 받습니다.
- trace를 켠 빌드(UART 921600 baud)는 `--baud 921600`을 붙입니다. 끈 빌드는 기본값(115200)입니다.

## UART 로그 (trace)

파이프라인 각 단계(Agent1/2/3)의 입출력을 PC로 한 줄씩 보내는 디버그 기능입니다.
**기본은 꺼져 있고**, 컴파일 심볼 `ROBOT_TRACE`를 정의한 빌드에서만 켜집니다. 끈 빌드는 기존과 같습니다
(`-O2`에서 `agent_pipeline.c`, `main_integration.c`, `platform_vitis.c`의 기계어가 수정 전과 똑같은 것을 확인했습니다).

### 켜는 방법

1. Vitis에서 앱(`robot_testbench`) → Properties → C/C++ Build → Settings → Symbols(Defined symbols)에 `ROBOT_TRACE`를 추가합니다. 이 워크스페이스는 Debug 설정에만 include 경로와 심볼이 들어 있으므로
   Debug 설정에 추가하고 Debug As로 실행합니다(아래 "주의"의 6번).
   IDE를 닫고 명령으로 하려면 xsct에서 `app config -name robot_testbench -add define-compiler-symbols ROBOT_TRACE`를 실행합니다.
2. 앱을 Clean 후 빌드합니다. 빌드 로그에 `[TRACE] ROBOT_TRACE enabled: UART runs at 921600 baud` 안내가 한 줄 나옵니다.
3. 실행하면 배너가 바뀝니다: `[platform] ready (tick 20 ms, uart 921600 baud, trace on)`.
   끈 빌드의 배너는 `[platform] ready (tick 20 ms)`이고 115200 baud입니다.

`setup_vitis.ps1`은 이 심볼을 넣지 않습니다. 그래서 팀원이 기본으로 만든 앱은 이전처럼 115200 baud이고 PC 스크립트의 기본값과 맞습니다.

**UART 속도가 다릅니다.** trace를 켠 빌드는 921600 baud입니다(PS UART는 송신과 수신이 같은 baud 발생기를 쓰므로 둘 다 바뀝니다).
PC 쪽도 `--baud 921600`으로 열어야 합니다. 속도가 다르면 배너가 깨져 보이고 pose는 CRC 오류로 버려집니다.

### 로그 받기

```powershell
python robot_arm/pc/send_pose_uart.py --port COM3 --csv <pose CSV 경로> --hz 20 --baud 921600 | Tee-Object run.log
Select-String -Path run.log -Pattern '^(A1|P3|A2|TK|SM|EV),'   # 로그 줄만 골라 보기
```

- 스크립트를 고치지 않고 화면 출력을 그대로 저장합니다. 스크립트가 찍는 `[TX] ...` 줄은 태그가 달라서 섞이지 않습니다.
  Windows PowerShell 5.1은 파일을 UTF-16으로 저장하므로 다른 도구로 읽을 때는 PowerShell 7을 쓰거나 UTF-8로 변환하세요.
- 스크립트는 프레임을 보낼 때(20 Hz면 50 ms마다)만 수신 버퍼를 읽습니다. `--hz`를 낮게 쓰면 그 사이에 쌓인 로그가 PC 수신 버퍼를 넘칠 수 있습니다.
  이렇게 잃은 줄은 보드의 `drop`으로는 안 보이므로 `fid`와 `tick`이 끊김 없이 이어지는지로 확인합니다.
- 시리얼 터미널과 송신 스크립트는 같은 COM 포트라서 동시에 열 수 없습니다.

### 줄 형식

한 줄이 한 레코드이고 첫 토큰이 태그입니다. 쉼표로 나누고, 값이 없으면 빈 칸입니다. 각도는 소수 1자리, 그리퍼는 2자리,
Point3D는 3자리입니다. `#`로 시작하는 줄은 컬럼 정의이며 부팅 때와 10초마다 다시 나옵니다(PC가 늦게 붙어도 받도록).

| 태그 | 언제 | 컬럼 |
|---|---|---|
| `A1` | `agent1_run` 직후, 프레임마다 | `fid, t_ms, dur_us, dt_ms, pv, vm, rc, ov, base, sh, el, wp, wr, grip` |
| `P3` | `A1` 바로 뒤, 프레임마다 | `fid, pm, fl, age_ms,` 6점의 `x, y, z` (`sl, sr, e, w, f1, f2`) |
| `A2` | `agent2_run` 직후, 프레임마다 | `fid, t_ms, dur_us, st, fg,` 타겟 5개(`ub, ush, ue, uwp, uwr`), 매핑된 명령 6개(`cb, csh, ce, cwp, cwr, cg`) |
| `TK` | 제어 틱마다(50 Hz) | `tick, t_ms, dur_us,` 출력 6개(`b, sh, e, wp, wr, g`), PWM 6개(`pb, psh, pe, pwp, pwr, pg`), `rem, w, er` |
| `SM` | 1초마다 | `t_ms, fr, tv, acc, rej, rt, tk, sw, se, ovr, crc, fmt, rng, ow, drop, hi` |
| `EV` | 상태가 바뀔 때만 | `t_ms, code, arg` |

형식 예시입니다(숫자는 형식을 보이려고 만든 값입니다).

```text
A1,412,20510,132,50,1,63,1,1,12.3,45.0,90.1,10.2,-5.0,0.50
P3,412,63,7,0,-0.213,0.041,0.850,0.198,0.037,0.861,-0.240,0.302,0.790,-0.315,0.512,0.702,-0.360,0.603,0.688,-0.342,0.611,0.700
A2,412,20511,14,N,0x0,12.3,45.0,90.1,10.2,-5.0,101.3,47.5,88.0,90.0,92.5,0.50
TK,1030,20520,9,101.3,47.5,88.0,90.0,92.5,0.50,1626,1028,1478,1500,1528,1500,3.2,1,0
SM,21000,420,418,415,3,12,1050,1050,0,0,0,0,0,0,0,96
EV,20990,A2_REJECT,431
```

### 값 읽는 법

- **`t_ms`, `dur_us`**: `t_ms`는 부팅 후 ms입니다. `dur_us`는 그 단계의 실행 시간(µs)입니다. A1은 `agent1_run`, A2는 `agent2_run`,
  TK는 `agent2_tick` + `agent3_run`이고 로그 만드는 시간은 뺍니다.
- **`pv`, `vm`, `rc`, `ov`(A1)**: `pv`는 pose 전체 valid입니다. `vm`은 입력 랜드마크 6개의 유효 마스크입니다(finger1=1, finger2=2, elbow=4, wrist=8,
  shoulder_l=16, shoulder_r=32, 모두 유효하면 63). `rc`는 Agent1의 반환값입니다(1 새 타겟, 0 HOLD, -1 타겟 없음).
  `ov`는 출력이 유효한지로 다음 단계 진행 여부의 기준이며, 0이면 각도 칸이 비어 있습니다.
- **`pm`, `fl`, `age_ms`(P3)**: `pm`은 Point3D 6점의 유효 마스크입니다(shoulder_l=1, shoulder_r=2, elbow=4, wrist=8, finger1=16, finger2=32).
  `fl`은 상태 플래그입니다(major=1, finger=2, body_frame=4). `age_ms`는 마지막으로 새 타겟을 만든 뒤 지난 시간입니다(0이면 이번 프레임, 0보다 크면 HOLD).
  3D 값은 재구성에 성공할 때만 갱신되므로 끊긴 동안에는 이전 값이 남습니다. 단위는 어깨너비 = 1.0입니다(`PM_SHOULDER_WIDTH_UNIT`).
- **`st`(A2)**: `N` 새 목표 승인, `S` 직전과 같은 명령이라 재계획 안 함, `R` 안전검사 거부, `V` 입력 검증 실패,
  `-` 이번 프레임에 Agent1 타겟이 없어 실행 안 함.
- **`fg`(A2)**: `R`일 때 거부 사유(안전검사 flags, 16진수, 여러 개가 겹칠 수 있음)입니다.
  `0x01` INVALID_COMMAND, `0x02` SELF_COLLISION, `0x04` FLOOR_COLLISION, `0x08` NEAR_SINGULARITY, `0x10` REACH_BOUNDARY.
  `robot_calibration_apply()`가 flags를 버리므로, 거부된 명령의 복사본(valid=1)으로 공개 함수 `safety_check_apply()`를 한 번 더 불러 얻습니다.
- **`ub..uwr`, `cb..cg`(A2)**: 앞쪽은 unwrap(±180° 경계 처리) 뒤의 타겟이고, 뒤쪽은 이번 프레임에 매핑된 명령입니다(거부됐으면 거부된 값).
- **`rem`, `w`, `er`(TK)**: 램프가 목표까지 남은 각도(관절 중 최대), 이번 틱에 서보 쓰기가 성공했는지, 서보 오류가 났는지입니다.
- **SM**: `fr` 받은 프레임, `tv` Agent1이 유효 타겟을 낸 수, `acc/rej/rt` Agent2 승인/거부/재계획, `tk` 틱, `sw/se` 서보 쓰기 성공/오류,
  `ovr` 밀려서 버린 틱(오르면 main 루프가 20 ms 안에 못 돈 것), `crc/fmt/rng` PC→보드 UART 패킷 오류, `ow` main이 읽기 전에 덮어쓴 프레임,
  `drop` 링버퍼가 차서 버린 로그 줄, `hi` 최근 1초의 링버퍼 최대 사용량(바이트, 8192면 가득).
- **EV**: `BOOT`(arg=baud), `A1_LOST`/`A1_BACK`(Agent1 유효 타겟 소실/복귀, arg=fid), `A2_REJECT`/`A2_BACK`(거부 시작/해소, arg=fid),
  `SERVO_ERR`, `TICK_OVERRUN`, `UART_ERR`, `TRACE_DROP`(arg=누적 횟수).

### 영향과 한계

- 켜면 코드가 약 10 KB, RAM이 약 8.3 KB(링버퍼 8 KB 포함) 늘어납니다(Vitis Debug `-O0` 빌드를 링크한 ELF 기준: text +10,208 B, bss +8,264 B).
- 로그는 링버퍼에 쌓기만 하고 `main` 루프 끝에서 TX FIFO(64 B)에 들어가는 만큼만 보냅니다. `xil_printf`처럼 기다리지 않습니다.
  링버퍼가 차면 줄을 통째로 버리고 `drop`을 셉니다.
- 프레임 경로에 로그 만드는 시간이 더해집니다. 호스트 실측을 보드로 환산한 추정으로 프레임당 수십 µs(Debug `-O0`)입니다.
  틱 로그(TK)는 서보 쓰기 뒤에 만들어서 서보 지연에 영향이 없습니다. 추정이므로 실제 값은 `dur_us`와 SM의 `ovr`로 확인하세요.
- 로그 함수는 ISR에서 부르지 않습니다. 루프가 시작된 뒤에는 `xil_printf`를 쓰지 않습니다(같은 UART에 섞여 줄이 깨집니다).

### 변경 지점

변경한 지점에는 `[TRACE]` 태그가 있습니다. `grep -rn "\[TRACE\]" robot_arm/src robot_arm/include`로 찾을 수 있습니다.
(Agent1의 getter만 Agent1 파일이라 태그 대신 "디버그/로그 전용" 주석이 있습니다.)

| 파일 | 변경 |
|---|---|
| `include/integration/trace.h`, `src/integration/trace.c` | 신규: trace 본체 |
| `src/integration/main_integration.c` | `TRACE_*` 호출 7줄 |
| `include/integration/agent_pipeline.h`, `src/integration/agent_pipeline.c` | `ROBOT_TRACE`일 때 `a1_rc`, `a2_result`, `a2_mapped` 기록 |
| `src/integration/platform_vitis.c` | baud 선택, 배너, trace 플랫폼 경계 3개(시간, 통계, 논블로킹 TX) |
| `include/human_target_angle/agent1_stage.h`, `src/human_target_angle/agent1_stage.c` | Agent1 담당자와 합의해 추가한 읽기 전용 getter `agent1_stage_debug_context()` 1개(항상 컴파일됨) |
| `tests/integration/test_trace.c` | 신규: 호스트 테스트 |

Agent2와 Agent3 소스, PC 스크립트는 수정하지 않았습니다.

## 하드웨어(XSA)가 바뀌었을 때

1. Vivado에서 비트스트림을 만들고 File → Export → Export Hardware(**Include bitstream**)로 XSA를 내보냅니다.
2. `vitis/xsa/robot_test_wrapper.xsa`를 교체합니다.
3. 새 워크스페이스에서 스크립트를 다시 돌립니다. 주소와 인터럽트 번호는 XSA가 만드는 `xparameters.h`
   매크로를 통해 코드에 반영되므로 소스를 고칠 필요가 없습니다(매크로 이름이 바뀌지 않는 한).

## 문제 해결

| 증상 | 원인과 해결 |
|---|---|
| BSP 생성 중 `파일 이름이나 확장명이 너무 깁니다` | 워크스페이스 경로가 깁니다. 짧은 경로로 다시 만드세요 |
| 링크 에러: `platform_init`, `platform_tick_due`, `input_pose_*` 미정의 | `src/integration/platform_vitis.c`가 없는 브랜치입니다. 이 파일이 있는 브랜치인지 확인하세요 |
| 앱 빌드에서 `cannot find -lxil` | 플랫폼 빌드가 실패한 상태입니다. 플랫폼 우클릭 → Clean Project → Build Project |
| 브레이크포인트에 안 멈춤 | Run으로 실행했을 가능성이 큽니다. Debug As로 다시 실행하세요 |
| 로그의 `pwm.*_pwm_us` 값은 정상인데 서보 PWM 핀에 신호가 없음 | 서보 드라이버가 mock으로 빌드됐을 수 있습니다. 앱 심볼에 `SERVO_PWM_DRIVER_USE_XILINX`가 있는지, ELF에 mock 심볼이 없는지 확인하세요(위 "주의"의 6번) |
| 저장소에 `.Xil` 폴더가 생김 | xsct를 저장소 폴더에서 직접 실행했을 때 생깁니다. 이 스크립트는 워크스페이스 옆 `_setup_logs` 폴더에서 실행해서 생기지 않습니다 |

참고: 서보 PWM IP(`servo_pwm`)의 자동 생성 드라이버 Makefile은 Windows용 Vitis 2020.2에서
`ar: *.o: Invalid argument`로 BSP 빌드를 실패시키는 알려진 문제가 있습니다. 이 저장소의 XSA에는
우회가 반영되어 있어 플랫폼 빌드가 통과합니다. 이 IP 드라이버는 코드에서 쓰지 않고
`src/drivers/servo_pwm_driver.c`로 레지스터를 직접 접근합니다.