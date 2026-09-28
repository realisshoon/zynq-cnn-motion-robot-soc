# CNN + 5축 로봇 통합 작업 기준 (2026-09-28)

이 문서는 `dev/integration`에서 모든 에이전트가 공유하는 **현행 통합 계약과
수정 범위**다. 실행 결과와 Agent4 리뷰 근거는 [cnn_integration.md](cnn_integration.md),
5축 수정 정책은 [../AGENTS.md](../AGENTS.md)를 따른다. 과거 Agent3/Agent4 handoff는
당시 상태의 기록이므로 현재 코드나 이 문서와 충돌하면 현행 코드와 이 문서를 확인한다.
아래 상태 표에서 구현과 보드 미검증 항목을 구분한다.

## 현재 실행 흐름

1. `src/integration/main_integration.c`가 플랫폼과 파이프라인을 초기화한 뒤
   `cnn_app_service()`를 루프에서 호출한다. 카메라/VDMA는 영상을 DDR에 계속 쓰고,
   PS가 프레임용 SG를 준비하여 CNN 추론을 시작한다. CNN 완료 **IRQ**는 완료 상태를
   기록하고, 결과 판독과 다음 프레임 시작은 foreground의 `cnn_bringup_service()`가 한다.
2. CNN 결과를 `input_pose_cnn_publish()`가 `HumanPose2D`로 바꾼다. 새 결과가
   준비되면 `agent1_run()`이 `HumanForearmTarget`을, `agent2_run()`이 검증된
   `ForearmJointCommand` 목표를 만든다. 입력 주기는 CNN 처리 시간에 따라 가변이다.
3. AXI Timer IRQ는 20 ms 틱을 기록한다. 메인 루프가 틱을 소비할 때
   `agent2_tick()`이 궤적을 한 번 진행하고 `agent3_run()`이
   `ServoPwmCommand`로 변환한다. 기본 빌드는 로봇 PWM 적용을 끈다.
   `ROBOT_ARM_PWM_ENABLE`을 정의한 별도 빌드에서만 로봇 PWM을 실제 적용한다.
4. 입력이 잠시 없으면 Agent2는 **마지막 승인 목표까지 계속 이동**하고 도착 후
   마지막 명령 자세를 유지한다. 새 프레임이 없을 때 Agent1의 HOLD 나이가
   자동으로 증가하지 않는 점은 사실이나, 이를 이유로 이동 중인 궤적을
   타임아웃 시 즉시 취소하는 watchdog은 추가하지 않는다. 서보 위치 피드백은
   없으므로 여기서 자세는 실측값이 아니라 명령값이다.

CNN 완료, 타이머 틱은 인터럽트 기반이지만 Agent1/2/3과 UART 메뉴는 ISR에서
실행하지 않는다. UART RX 명령과 TX FIFO 서비스는 foreground polling이다.
TRACE 빌드는 921600 8N1이며, 이 전송 속도가 무제한 로그나 동기 SD 작업의
지연을 없애지는 않는다.

## 합의된 운영 방향과 현재 차이

| 항목 | 합의된 방향 | 현재 코드 |
|---|---|---|
| 입력 중단 | 마지막 승인 목표까지 이동한 뒤 유지. 강제 정지·PWM 해제 없음. 장시간 무입력은 진단 통계로 관찰 | 목표 유지/추종은 구현됨. 장시간 무입력 전용 진단은 미구현 |
| 카메라 초기 자세 검증 | 추적 계산을 멈추고 pan/tilt PWM을 일정한 값으로 유지해 카메라를 고정. 최종 통합 기능인 사람 추적은 보존 | 부팅 FIXED(초기 1500 µs), UART `f` 고정, `u` 추적 재개 구현. 실제 펄스 유지·방향은 보드 미검증 |
| SD | 부팅 시 CNN 가중치 로드는 유지. 추론 중 CNN 결과 CSV 기록은 끄고 SD 쓰기 지연을 제거 | 자동·프레임별 CSV와 수동 `l` 제거. 가중치 로드는 유지 |
| UART | 먼저 TRACE의 틱 누락·출력량과 실제 수신 문제를 측정. 필요할 때 RX IRQ/TX 정책 검토 | 수신 메뉴와 TX FIFO 배출 모두 polling. `q`는 출력만 음소거/재개하며 CNN·로봇 제어는 계속 실행. 출력이 켜졌을 때 `outbyte()`는 32 KiB 큐가 가득 차면 기다림 |
| 로봇 PWM | PWM 없는 TRACE 검증 후 별도 서보 시험 | 기본 빌드 비활성; 카메라 PWM은 별도 IP라 켜질 수 있음 |

카메라 고정은 PS UART `f` 메뉴에서 전환하며 UART RX IRQ는 필요하지 않다.
FIXED는 tracker enable과 camera PWM enable을 분리한다. 기존 `u`는
추적 ON/OFF 토글을 유지하므로 추적 중 `u`로 끄면 PWM도 꺼진다.
고정 기준 펄스와 실제 카메라 방향은 보드에서 확인한다.

## 수정 범위와 인계

| 담당 | 수정할 영역 / 확인할 일 | 다른 담당과의 경계 |
|---|---|---|
| 통합 담당(Codex) | `src/integration/main_integration.c`, `agent_pipeline.c/h`, `input_pose_cnn.c/h`, `platform_vitis.c/h`, `trace.c/h`, `vitis/setup_vitis.ps1` 등 연결부. 데이터 흐름, 로봇 PWM 기본 비활성, 무입력 진단, 20 ms 틱과 UART 지연 계측 | A1 각도 정의, A2 보정/FK, A3 서보 보정값을 임의 변경하지 않음 |
| CNN/카메라 담당(Agent4, 통합 담당에게 설계 권한 위임) | `src/cnn_firmware/*`, `src/integration/cnn_app.c`, `cnn_console.c/h`, CNN XSA 경계. SD CSV 상시 기록 제거, 고정 카메라 모드, 추론 중 `w`/`g` 안전 처리, `s`/`x` 동작 정리, 오류 snapshot, 원래 CNN/HDMI/overlay/카메라 추적 기능 보존 | `main_integration.c` 및 로봇 파이프라인의 public 계약을 바꿀 때 통합 담당과 함께 검토 |
| Agent1 | `HumanPose2D` → `HumanForearmTarget`, 재구성·각도 계산과 해당 테스트 | 카메라 고정/UART/로봇 PWM 정책을 A1 각도 계산에 임의 반영하지 않음 |
| Agent2 | `HumanForearmTarget` 검증·unwrap·보정·안전검사·20 ms 궤적 및 해당 테스트 | 무입력 시 마지막 승인 목표 유지가 현재 정책. 입력 타임아웃 즉시 정지를 추가하지 않음 |
| Agent3 | `src/output_controller/*`, `src/drivers/servo_pwm_driver.c`, 대응 헤더·테스트. 5축 PWM 변환, HAL·레지스터 출력, 물리 채널·펄스 보정 검증. **동작 저장·재생 기능의 별도 작업 지침 전달됨** | CNN의 `pwm_camera`와 로봇 `servo_pwm`은 다른 IP. CNN 결과 SD CSV 기록은 Agent3 기능이 아님. 녹화·재생 연결과 통합 trace/UART 변경은 통합 담당과 조율 |

Agent3의 현행 입력은 Agent2가 승인한 `ForearmJointCommand`의 서보 각도
(`elbow_roll`, `elbow_pitch`, `wrist_pitch`, `wrist_roll`)와 정규화된 `gripper`다.
출력은 `ServoPwmCommand`의 5개 펄스(µs)이며 로봇 IP CH0..CH4에 대응한다.
Agent3는 사람 각도나 CNN 픽셀 좌표를 직접 받지 않는다. 로봇 PWM이 기본
비활성인 빌드에서는 변환 결과를 검사할 수 있어도 레지스터 적용은 하지 않는다.
무입력 시 마지막 목표 추종에는 Agent3 API 변경이 필요하지 않다.

### Agent3 동작 저장·재생 병렬 작업

사용자는 기준 커밋 `b4a695204f7fba9259301fd3c76ce8851a4bbfbc`에서 Agent3에게
**로봇 동작 저장·재생** 기능의 설계 지침을 이미 전달했다. 이것은 CNN 결과의
SD CSV 기록과 별도 기능이며, 현재 통합 소스에 구현된 상태는 아니다. 과거
[Agent3 integration handoff](agent3_integration_handoff.md)의 "Record/Playback 폐기"는
당시 구현을 정리한 이력이지, 이번 신규 작업을 취소하는 지침이 아니다.
남은 `tests/output_controller/test_record.c`는 삭제된 `motion_record.h`를
참조하는 과거 테스트이므로 새 기능의 동작 증거로 사용하지 않는다.

Agent3에게 전달된 **제안**은 앱 링커가 보호하는 DDR 영역 안에 static
`MotionKeyframe` 버퍼(상대시각과 4개 서보 각도·gripper)를 두고, Agent2가
승인한 `ForearmJointCommand` 재목표 이벤트를 기록하는 A안이다. 녹화 중
SD 쓰기 없음, 저장 확정 시 SD flush, 재생 전 파일 무결성 및 각 명령의
가동범위·안전검사 재확인을 요구했다. 새 고정 DDR 주소를 사용하지 않는다.
버튼2의 "누르는 동안 녹화, 놓을 때 저장" 의미는 아직 사용자 확정이 필요하다.

통합 경계에서 아래 네 가지를 Agent3와 함께 확정해야 한다. 연결용 새 코드는
`agent2_run()`과 `agent3_run()` 중 알맞은 위치에 **추가할 수 있다**. 기존
라이브 경로의 검증·unwrap·목표 설정·20 ms 궤적·PWM 변환/적용 로직 및
호출 순서는 수정하지 않는다. 기본 모드는 같은 입력에 같은 출력을 내야 하며,
재생은 별도 모드 경로로 추가한다.

1. `agent2_run()`과 `agent3_run()`은 둘 다 `src/integration/agent_pipeline.c`에
   있다. 전자는 새 입력이 있을 때, 후자는 20 ms 틱마다 호출된다.
   `agent3_run()`에서 `retargets` 증가만 비교하면 한 틱 사이에 발생한 여러
   재목표를 놓칠 수 있으므로 이벤트 누락 여부를 설계·검증한다. 기록 훅은
   어느 함수에 추가해도 되지만 기존 함수의 로직은 그대로 보존한다.
   연결부는 통합 담당과 Agent3가 함께 검토하며 Agent1/2 내부를 고치지 않는다.
2. 재생 시 라이브 CNN 목표가 같은 `ForearmMotionState`에 동시에 새 목표를
   쓰지 않도록 별도 모드/경로가 필요하다. 일반 라이브 모드의 기존 호출은
   바꾸지 않는다. 저장된 명령도 신뢰하지 않고 범위·안전검사를 거친 뒤
   현재 20 ms 궤적 경로에 넣어야 한다. Agent3가 직접 PWM을 재생해 Agent2
   안전검사·속도제한을 우회하지 않는다.
3. 버튼을 놓은 뒤 1회만 하는 SD 저장이라도 foreground에서 수백 ms 이상
   걸리면 20 ms 틱이 누락된다. "1초 안팎이므로 괜찮다"는 실측 근거가 없다.
   저장 중 제어를 어떻게 유지할지(작은 청크로 분할, 명시적 저장 상태 등)
   정하고 보드에서 지연을 측정해야 한다. CNN CSV 상시 기록 중단 결정과
   혼동하지 않는다.
4. Agent3 작업은 먼저 `dev/robot`에 PR로 반영해 검증하고, 사용자가
   `dev/integration`으로 최종 병합한다는 전달 절차를 따른다. 통합 담당은
   Agent3 병렬 작업의 파일을 중복 구현하거나 임의로 병합하지 않는다.

## 검증 순서와 남은 문제

1. 호스트에서 CNN 결과 변환, 메뉴 명령과 추론의 동시성, 고정 카메라 모드,
   5축 출력 계약을 각각 확인한다. 기존 전체 테스트의 4개 실패는 통합 전
   기준점과 분리해서 보고한다.
2. 새 XSA/ELF를 빌드하고 로봇 PWM 없이 보드에서 CNN 완료 IRQ, 카메라
   PWM 고정/추적 전환, `CN`→`IN`→`A1`→`A2`와 `TK`/`SM`, SD 쓰기 중단,
   UART 출력량·틱 누락을 측정한다.
3. 그다음 실제 서보 방향·중립·가동범위와 A2 FK/충돌 가정을 확인한다.
   서보 시험은 별도 사용자 요청 전에는 하지 않는다.

Agent4 리뷰의 우선 수정 사항은 추론 중 `w`/`g`가 사용 중인 DDR
가중치/SG 디스크립터를 다시 쓰지 못하게 하는 것이었다. 현재 앱 메뉴와
`cnn_bringup` 진입점에서 실행 중 쓰기를 거부하고, `s`는 실행 중 거부,
`x`는 진행 프레임 완료·timeout 뒤 reset하도록 변경했다. 비동기 fault에는
한 번의 진단 덤프를 남긴다. 이 동작의 실보드 검증은 아직 없다. CNN AXI
레지스터의 RTL bit-field와 Vivado XDC 출처도 여전히 독립 근거가 부족하다.
