# CNN + 5축 로봇 통합 작업 기준 (2026-09-30)

## 현행 우선 계약: 승열3 (2026-10-08)

운영 소스를 정상 `src/`, `include/`, `config/`에 통합했다. 아래 날짜별 계약은 이력이며,
현재 로봇0 RIGHT/JB와 로봇1 LEFT/JC의 독립 제어·G 필터·녹화 준비/종료 경계는
[승열3 릴리스](seungyeol3_release.md), [Unity UART 계약](uart_protocol_unity.md),
[로봇별 명령](robot0_robot1_commands.md)을 우선한다. 이전 `captures/` 스냅샷과
SD는 복구 자료로 보존하고, 이번 통합에서 실제 SD 교체·보드 부팅은 하지 않았다.
SDK의 고정 CNN ABI 0x77D4E3BB와 SD SHA sidecar 검증은 유지한다.

이 문서는 `dev/integration`에서 모든 에이전트가 공유하는 **현행 통합 계약과
수정 범위**다. 실행 결과와 Agent4 리뷰 근거는 [cnn_integration.md](cnn_integration.md),
5축 수정 정책은 [../AGENTS.md](../AGENTS.md)를 따른다. 과거 Agent3/Agent4 handoff는
당시 상태의 기록이므로 현재 코드나 이 문서와 충돌하면 현행 코드와 이 문서를 확인한다.
아래 상태 표에서 구현과 보드 미검증 항목을 구분한다.

## 운영 소스 복원 상태 (2026-10-04)

사용자 요청으로 운영 펌웨어 전체를 `captures/acg_build_20261004_acg_final/source/`로 복원했다.
운영 A는 One Euro이며 외부 G에만 칼만 UART 조정을 추가한다. 이후 시간 정책·이동 재획득
수정은 제거했다. 기존 SD/실행 보드는 별도 BOOT 교체 전까지 그대로다.
복원 전 자료는 `captures/restore_G_baseline_20261004/before/`에 보관한다.
현재 정책은 [One Euro 문서](stereo_one_euro.md#이력과-시간)를 따른다.

## 현재 실행 흐름

1. `src/integration/main_integration.c`가 플랫폼과 파이프라인을 초기화한 뒤
   `cnn_app_service()`를 루프에서 호출한다. 카메라/VDMA는 영상을 DDR에 계속 쓰고,
   PS가 프레임용 SG를 준비하여 CNN 추론을 시작한다. CNN 완료 **IRQ**는 완료 상태를
   기록하고, 결과 판독과 다음 프레임 시작은 foreground의 `cnn_bringup_service()`가 한다.
2. CNN 결과를 `input_pose_cnn_publish()`가 `HumanPose2D`로 바꾼다. 새 결과가
   준비되면 `agent1_run()`이 `HumanForearmTarget`을 만든다. LIVE/RECORDING에서만
   `agent2_run()`이 검증된 `ForearmJointCommand` 목표를 갱신한다. 재생 중에도 A1은
   실행하지만 라이브 목표로 재생 목표를 덮지 않는다. 입력 주기는 CNN 처리 시간에 따라 가변이다.
3. AXI Timer IRQ는 20 ms 틱을 기록한다. 메인 루프가 틱을 소비할 때
   `motion_record_replay_control_tick()`이 모드에 따라 Agent2와 Agent3를 호출한다.
   LIVE/RECORDING은 `agent2_tick()` → 명령 검증 → `agent3_apply_command()` 순서다.
   ALIGNING은 Agent2 램프와 gripper 제한을 거치고, PLAYING은 저장 샘플을 검증한 뒤
   Agent3에 직접 전달한다. HOLDING은 마지막 성공 명령을 재적용한다.
   `agent3_run()`은 호환 래퍼로 남지만 이 메인 경로에서는 별도로 호출하지 않는다.
   현재 `vitis/setup_vitis.ps1` 기본 빌드는 `ROBOT_ARM_PWM_ENABLE`을 정의해
   로봇 PWM을 실제 적용한다. 로봇을 연결하지 않는 진단 빌드는 이 심볼을 제외한다.
4. LIVE/RECORDING에서 입력이 잠시 없으면 Agent2는 **마지막 승인 목표까지 계속 이동**하고 도착 후
   마지막 명령 자세를 유지한다. 새 프레임이 없을 때 Agent1의 HOLD 나이가
   자동으로 증가하지 않는 점은 사실이나, 이를 이유로 이동 중인 궤적을
   타임아웃 시 즉시 취소하는 watchdog은 추가하지 않는다. 서보 위치 피드백은
   없으므로 여기서 자세는 실측값이 아니라 명령값이다.

CNN 완료, 타이머 틱은 인터럽트 기반이지만 Agent1/2/3과 UART 메뉴는 ISR에서
실행하지 않는다. UART RX 명령과 TX FIFO 서비스는 foreground polling이다.
TRACE 빌드는 921600 8N1이며, 이 전송 속도가 무제한 로그나 동기 SD 작업의
지연을 없애지는 않는다.

`main_integration.c`에는 CNN/UART 서비스, 프레임 경로, 20 ms 제어 경로,
로그 배출을 순서대로 둔다. UART R/P 전환 및 응답 형식은 파일 내부의
`handle_record_or_play_event()`로 묶고, 모드별 출력 검증과 기록 처리는
Record/Replay 제어기 안에서 수행한다. main에서 Agent3를 중복 호출하지 않는다.

## 합의된 운영 방향과 현재 차이

2026-10-02 Stereo 런타임 PWM 제어: LEFT/RIGHT는 부팅 OFF를 유지하고 UART
`E`(활성화), `X`(해제), `V`(조회)를 CNN 앱 software event로 전달한다.
main foreground에서 소비하며 20 ms 틱/기존 Agent2·RecordReplay 호출 순서는 유지한다.
LEFT 활성화는 거부하고 RIGHT의 기존 오른팔 CH0..CH4만 대상으로 한다.
활성화는 LIVE, gripper catch-up 종료, 정지 상태 및 최초 홈/마지막 HAL 명령과
현재 출력·최근 Agent3 명령·축 위치·목표 일치를 요구한다. OFF 중 누적된 가상 이동을
하드웨어로 즉시 적용하지 않는다. shadow → UPDATE → ENABLE 순서이며 동기화 gate는
그대로다. 실제 위치 피드백이 없으므로 켤 때 홈/마지막 명령으로 움직일 수 있고,
명시적인 X 해제는 유지 토크를 없애므로 팔 지지가 필요하다. 입력 소실 시 자동 해제는 없다.
상세 명령과 PC 보호 옵션은 [UART 참고](uart_reference.md)를 따른다.

| 항목 | 합의된 방향 | 현재 코드 |
|---|---|---|
| 입력 중단 | 마지막 승인 목표까지 이동한 뒤 유지. 강제 정지·PWM 해제 없음. 장시간 무입력은 진단 통계로 관찰 | 목표 유지/추종은 구현됨. 장시간 무입력 전용 진단은 미구현 |
| 카메라 초기 자세 검증 | 추적 계산을 멈추고 pan/tilt PWM을 일정한 값으로 유지해 카메라를 고정. 최종 통합 기능인 사람 추적은 보존 | 부팅 FIXED(초기 1500 µs), UART `f` 고정, `u` 추적 재개 구현. 실제 펄스 유지·방향은 보드 미검증 |
| SD | 부팅 시 CNN 가중치 로드는 유지. 추론 중 CNN 결과 CSV 기록은 끄고 SD 쓰기 지연을 제거 | 자동·프레임별 CSV와 수동 `l` 제거. 가중치 로드는 유지 |
| UART | 먼저 TRACE의 틱 누락·출력량과 실제 수신 문제를 측정. 필요할 때 RX IRQ/TX 정책 검토 | 수신 메뉴와 TX FIFO 배출 모두 polling. `q`는 출력만 음소거/재개하며 CNN·로봇 제어는 계속 실행. 출력이 켜졌을 때 `outbyte()`는 32 KiB 큐가 가득 차면 기다림 |
| 로봇 PWM | PWM 없는 TRACE 검증 후 별도 서보 시험 | 2026-09-30 기본 빌드 활성; 카메라 PWM은 별도 IP |

카메라 고정은 PS UART `f` 메뉴에서 전환하며 UART RX IRQ는 필요하지 않다.
FIXED는 tracker enable과 camera PWM enable을 분리한다. 기존 `u`는
추적 ON/OFF 토글을 유지하므로 추적 중 `u`로 끄면 PWM도 꺼진다.
고정 기준 펄스와 실제 카메라 방향은 보드에서 확인한다.

## 수정 범위와 인계

| 담당 | 수정할 영역 / 확인할 일 | 다른 담당과의 경계 |
|---|---|---|
| 통합 담당(Codex) | `src/integration/main_integration.c`, `agent_pipeline.c/h`, `input_pose_cnn.c/h`, `platform_vitis.c/h`, `trace.c/h`, `vitis/setup_vitis.ps1` 등 연결부. 데이터 흐름, 로봇 PWM 빌드 설정, 무입력 진단, 20 ms 틱과 UART 지연 계측 | A1 각도 정의, A2 보정/FK, A3 서보 보정값을 임의 변경하지 않음 |
| CNN/카메라 담당(Agent4, 통합 담당에게 설계 권한 위임) | `src/cnn_firmware/*`, `src/integration/cnn_app.c`, `cnn_console.c/h`, CNN XSA 경계. SD CSV 상시 기록 제거, 고정 카메라 모드, 추론 중 `w`/`g` 안전 처리, `s`/`x` 동작 정리, 오류 snapshot, 원래 CNN/HDMI/overlay/카메라 추적 기능 보존 | `main_integration.c` 및 로봇 파이프라인의 public 계약을 바꿀 때 통합 담당과 함께 검토 |
| Agent1 | `HumanPose2D` → `HumanForearmTarget`, 재구성·각도 계산과 해당 테스트 | 카메라 고정/UART/로봇 PWM 정책을 A1 각도 계산에 임의 반영하지 않음 |
| Agent2 | `HumanForearmTarget` 검증·unwrap·보정·안전검사·20 ms 궤적 및 해당 테스트 | 무입력 시 마지막 승인 목표 유지가 현재 정책. 입력 타임아웃 즉시 정지를 추가하지 않음 |
| Agent3 | `src/output_controller/*`, `src/drivers/servo_pwm_driver.c`, 대응 헤더·테스트. 5축 PWM 변환, HAL·레지스터 출력, 물리 채널·펄스 보정 검증. **동작 저장·재생 기능의 별도 작업 지침 전달됨** | CNN의 `pwm_camera`와 로봇 `servo_pwm`은 다른 IP. CNN 결과 SD CSV 기록은 Agent3 기능이 아님. 녹화·재생 연결과 통합 trace/UART 변경은 통합 담당과 조율 |

Agent3의 현행 입력은 Agent2가 승인한 `ForearmJointCommand`의 서보 각도
(`elbow_roll`, `elbow_pitch`, `wrist_pitch`, `wrist_roll`)와 정규화된 `gripper`다.
출력은 `ServoPwmCommand`의 5개 펄스(µs)이며 로봇 IP CH0..CH4에 대응한다.
Agent3는 사람 각도나 CNN 픽셀 좌표를 직접 받지 않는다. 로봇 PWM 심볼을
제외한 진단 빌드에서는 변환 결과를 검사할 수 있어도 레지스터 적용은 하지 않는다.
무입력 시 마지막 목표 추종에는 Agent3 API 변경이 필요하지 않다.

### Agent3 동작 저장·재생 통합 (PR #75)

사용자 승인으로 PR #75(`8366eb1`)를 `dev/integration`에 병합했다(`d0fcf0b`).
`src/record_replay/motion_record_replay.c`는 Agent3가 작성한 **통합 제어기**이며,
Agent2 궤적 모듈이나 Agent3 HAL로 편입된 것은 아니다. 모드에 따라 이들의 API를
호출한다. 기존 A1 각도 정의, A2 보정/FK/램프, A3 PWM 보정값은 이번 가독성 정리에서
변경하지 않는다.

- UART `R`은 녹화 시작/종료, `P`는 재생 시작/종료다. main이 CNN 앱의 제어 이벤트를
  소비한다. GPIO 버튼 및 SD 저장은 이번 v1에 포함되지 않는다.
- DDR의 static `MotionSample` 배열 두 개에 각각 최대 1024개를 보관한다.
  한 샘플은 5개 float(20 bytes), 합계 40,960 bytes이며 50 Hz 기준 약 20.48초다.
  앱 링커가 보호하는 영역을 사용하고 새 고정 DDR 주소를 잡지 않는다.
- 녹화는 재목표 이벤트가 아닌 **20 ms 출력 명령 샘플링**이다. Agent2 출력 검증과
  Agent3 적용 성공 뒤에만 저장한다. 실측 관절 위치가 아니며 HAL 성공 기록과
  PWM-disabled software 출력 기록을 구분한다. 버퍼가 차면 자동 확정한다.
- 재생은 전체 preflight 및 매 틱 검증(finite/range/FK, 회전축 속도·가속도,
  gripper 변화량)을 거쳐 저장 샘플을 Agent3에 전달한다. PLAY에서는 Agent2 램프를
  다시 적용하지 않는다. 첫 샘플 진입은 ALIGN에서 Agent2 램프를 사용하며
  직전 두 성공 명령으로 진입 속도를 복원한다. 현재 통합 앱은 반복 재생을 사용하며,
  마지막 샘플에서 Sample0으로 직접 점프하지 않고 ALIGN을 거쳐 다시 재생한다.
  반복을 끄면 1회 재생 후 HOLDING/COMPLETED에서 마지막 명령을 유지한다.
  마지막 샘플에서 멈추는 감속도 preflight/PLAY에서 검사한다. 이동 중 녹화를
  끊어 종료 감속 제한을 넘는 데이터는 ACCELERATION으로 거부한다.
  완료 후 `P`를 누르면 LIVE로 복귀한다. 그 다음 `P`로 다시 재생할 수 있다.
- 틱 누락은 녹화 종료 또는 ALIGN/PLAY의 HOLD를 유발한다. 녹화 중 HAL 실패 샘플은
  저장하지 않고 성공한 부분을 보존한다. ALIGN/PLAY 실패는 HOLD로 전환한다.
- 사용자 승인 시험값(2026-09-29): main에서 `motion_record_replay_configure_align()`을
  `gripper_max_delta_norm_per_tick=0.01`, `align_timeout_ticks=500`으로 호출한다.
  명령상 gripper 전 범위 이동 2초, ALIGN 최대 10초다. 실측으로 검증한 정격값은 아니다.
  녹화 중 gripper 변화량 제한은 녹화 종료 후 LIVE에서도 목표에 도달할 때까지 유지한다.
  2026-09-30 기본 Vitis 빌드에서는 로봇 PWM을 활성화했다.
- A2 elbow_pitch 상한은 A3의 180도와 일치시켰다(기존 200도). 다른 보정값은 유지한다.

과거 `MotionKeyframe` 재목표 기록/SD flush/버튼2 제안과
[Agent3 integration handoff](agent3_integration_handoff.md)는 설계 이력이다.
향후 SD 저장을 추가할 때는 20 ms 루프 지연 대책과 파일 검증을 별도로 설계한다.
Agent3의 후속 작업은 현행 통합 기준에서 PR로 검토하고, 통합 담당은 사용자가
승인한 범위에서 병합한다. 이 문서의 과거 dev/robot 경유 절차는 이번 PR에 적용하지 않는다.

2026-09-29 재검증: PR 최신 커밋은 호스트 10 PASS와 기준점 `c49d0df`에도 존재하는
5개 실패(`test_integration_smoke`, `test_trace`, `test_axis_replay`,
`test_forearm_calibration`, `test_forearm_replay`)를 재현했다. PWM-disabled Vitis 빌드
성공, record/replay `.bss` 버퍼는 각각 `0x0014803c`/`0x0014d03c`(각 `0x5000` bytes)로
CNN 프레임 버퍼와 겹치지 않는다. 이 검증은 보드 동작 검증을 대신하지 않는다.

2026-09-30 RGBY pack77 통합: PR #80을 병합하고 기본 XSA를
`cnn_camera_gimbal_rgby_pack77.xsa`로 바꿨다. `ROBOT_ARM_PWM_ENABLE`을 정의한
새 Vitis workspace `D:\vws_rgby80`에서 FSBL/BSP/ELF 빌드 성공.
`E:\BOOT.BIN`은 해당 FSBL·bitstream·ELF의 Bootgen 이미지로 교체했다
(SHA-256 `67B418F0A3749A0B0BC5BF33B3022244AEA77E32C0AF8DEAE117A85227461A85`).
SD 가중치 SHA-256은
`77D4E3BB0E747AB5DF62AAA41A85784BE3215489941C6D271F401A50218E7DE3`이다.
보드 실행은 아직 하지 않았고, CNN팀 보고의 새 RTL 전체 timing FAIL은 남아 있다.

## 검증 순서와 남은 문제

2026-10-04 사용자 승인 A안: 운영 3D One Euro 기본값은 최소 0.5 Hz,
beta 0.001/mm, 미분 1 Hz이며 앞단 2D EMA tau=0.10 s는 유지한다.
RIGHT 전용 UART filter 프레임으로 조회/계수 조정/기본값 복원을 제공한다.
계수 변경은 필터 이력을 보존하고, 재획득 epoch에서는 변경한 설정을 재적용한다.
기존 PWM OFF 부팅, A/S 동작, 입력 기하/JUMP 및 A2/20 ms 궤적 계약을 유지한다.
C(약한 EMA+2D One Euro+3D One Euro)와 G(3D Kalman)는 외부 하네스의
복사 소스에서만 구성하며 운영 소스에 통합하지 않는다. 상세 명령과 범위는
stereo_one_euro.md 및 stereo_uart_monitor_guide.md를 따른다.

2026-10-04 One Euro 채택: `src/stereo_vision/stereo_one_euro.c`와
`stereo_pose_filter.c`를 운영 소스에 추가하고 입력 어댑터의 승인된 3D 소비 경로에서
기본 적용한다. 외부 빌드 훅 없이 RIGHT의 일반 Vitis 빌드에 포함한다. elbow/wrist/
빨강/초록 XYZ를 필터링하며 원본 JUMP 참조값, 2D EMA, A1 각도 EMA와 A2 안전·궤적을
유지한다. baseline/다른 비교 필터는 빌드 심볼로 기본 One Euro를 꺼 중복 적용을 막는다.
초기값·이력 초기화·검증 방법·별도 미해결 손목 방향 리뷰는
[stereo_one_euro.md](stereo_one_euro.md)를 따른다.

2026-10-04 사용자 승인 안정화: 로봇 링크는 전완 16 cm, 손목~손끝 20 cm로 확정한다.
팔꿈치 원점은 테이블보다 10 cm 위이며 FK 테이블면은 z=-10 cm다(이전 -5 cm).
기존 FK 좌표축/서보 보정/충돌 검사는 유지하며 이전 24/10 cm와 달리
기존 관절 범위 내에서도 테이블 충돌이 발생할 수 있다. 가동범위 검사만으로 안전을
판정하지 않는다. 실제 서보 위치 피드백·충돌 센서 측정이 아닌 명령 기반 모델이다.
비동기 LIVE는 250 ms 입력 나이·100 ms 수신 간격을 제한하고 최근 RIGHT/가까운 LEFT를
선택한다. 2D major 도약은 후보 확인 후 재획득, 손가락은 손목 거리 품질을 검사한다.
3D 마지막 입력 승인 기준은 500 ms 이하까지 보존하며 그 안에서 150 mm 이내 변화는
승인한다. 150 mm 초과·기준 위치 만료·보드 세션 변경 시 신선한 서로 다른 유효 쌍 3개로
재획득한다. 고정 50 mm 군집 또는 일관된 이동을 확인하며, 이동 후보는 팔꿈치/손목 각각
수신 간격 기반 속도 ≤1500 mm/s·샘플 간 이동 ≤150 mm·세 번째 위치의 선형 예측 오차
≤50 mm를 모두 요구한다. 좌우 각각 후보 간 수신 간격 ≤250 ms이고 불량 후보·긴 간격은
확인 수를 초기화한다. 옛 승인 위치와의 누적 거리는 재획득을 막지 않는다.
수신 기반 이동 일관성은 노출 동기화나 실측 물리 속도 보증이 아니며 승인은 입력 어댑터 단계다.
재획득 성공 시 3D 필터 이력만 초기화하고 마지막 로봇 출력에서 기존 A2/20 ms 궤적으로 이동한다.
250 ms 입력 신선도와 승인/필터의 500 ms 경계는 별개다. 정확히 500,000 µs는 이력 유지,
500,001 µs부터 장기 공백으로 취급한다. 필터에는 마지막 실제 성공 갱신 이후의 시각 차이를
전달하며 덮어쓴/버린/탈락 입력은 갱신하지 않는다. 누락된 점은 출력하지 않고 짧은 공백의
이력만 유지한다. 손가락 누락은 팔꿈치·손목 이력을 지우지 않는다. 자세한 시간 정책은
[One Euro 이력 계약](stereo_one_euro.md)을 따른다. 정지 depth 흔들림 개선은 별도 검증 항목이다.
노출 동기화 메타데이터/strict gate, PWM 부팅 OFF 및 명시적 E/A, SD SHA 계약은 유지한다.
UART RQ는 재획득과 입력 나이를 기록하고 commands 화면에서 숨기되 원문 파일에는 남긴다.

2026-10-03 gripper 수정: 스테레오 gripper의 2D 판정을 손가락 3D·손목 각도
성공 여부에서 분리한다. 같은 카메라의 wrist/빨강/초록이 모두 보이는 영상을
사용하고 현재 영상이 불완전하면 반대쪽 완전한 영상으로 전환한다.
`HumanForearmTarget.gripper_valid`는 최초 유효 2D 관측과 부팅 기본값을 구분해,
손목 각도 미복원 시에도 관측된 gripper 목표가 A2에 전달되도록 한다.
양쪽 마커 누락 시 마지막 목표를 유지하며 입력 프레임 승인·A2 안전검사는 유지한다.
호스트에서 겹친 마커/손가락 depth 실패/손목 각도 미복원 상태의 CLOSE가 PWM까지
전달되는지, 영상 전환/양쪽 누락/한 프레임 spike를 회귀 검증한다.

2026-10-02 스테레오 통합은 [stereo_board_uart.md](stereo_board_uart.md)를 따른다.
새 dual-arm UART0 XSA의 LEFT/RIGHT 빌드는 UART0로 17개 CNN 관절과 RGBY 마커를
전송하고 오른쪽 보드에서 depth를 만든다. 콘솔은 UART1을 유지하며 역할 빌드는
로봇 PWM을 기본 비활성화한다. 노출 시각·공통 epoch·고정 기하·검출 품질이 검증된
쌍만 기본 A1 실측 3D 경로에 넣는다. 실제 노출 시각 공급은 미구현이며 기본값은
비동기 depth 진단이다. 2026-10-02 사용자 승인으로 A/S/T의 별도 비동기 양안 시험
경로를 추가한다. RIGHT PWM ON 후 A가 필요하고 부팅은 두 기능 모두 OFF다.
동기화 플래그/기존 strict 상태는 위조하지 않으며 수신기 나이·간격/기하/급격한
측정 이동을 거부한다. 스테레오 A1은 어깨 기반 몸통 좌표계·어깨 폭 정규화 없이
고정 왼쪽 카메라 rig의 mm 절대좌표에서 각도를 계산한다. 기존 Mono 경로와
A2 보정/FK 및 20 ms 마지막 목표 유지 정책은 바꾸지 않는다.
새 XSA에는 카메라 짐벌 PWM이 없으므로 물리적으로 고정해야 하며 없는 IP에는
MMIO하지 않는다. 두 로봇 PWM을 활용한 양팔 제어는 아직 미구현이다.

1. 호스트에서 CNN 결과 변환, 메뉴 명령과 추론의 동시성, 고정 카메라 모드,
   5축 출력 계약을 각각 확인한다. 기존 전체 테스트의 5개 실패는 통합 전
   기준점과 분리해서 보고한다.
2. 새 XSA/ELF를 빌드하고 서보 전원을 분리한 상태에서 CNN 완료 IRQ, 카메라
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
