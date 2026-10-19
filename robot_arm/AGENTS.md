# 수정 지침 — 현행 5축 전완 로봇 (2026-10-19)

사용자 결정: 로봇 구동은 5축 forearm 경로만 지원한다. 과거 문서의
"legacy 6축 경로를 깨거나 삭제하지 말 것" 지침은 폐기한다.
이 문서와 README.md가 Git 배포본의 현행 수정·빌드 기준이다.
docs/, tests/, tools/, etc/는 로컬 전용 자료이며 Git에서 제공하지 않는다.
로컬 자료가 없다는 이유로 기본 빌드가 실패하거나 필요한 규칙을 확인할 수 없게 하지 않는다.
docs/의 날짜별 기록은 제공된 경우에만 참고할 개발 이력이다.
과거 Agent3/Agent4 handoff의 당시 상태나 수정 금지 목록을 현행 통합 작업에
그대로 적용하지 않는다.

- 최종 제품 빌드는 LEFT/RIGHT만 지원한다. 앱 매크로는 역할 하나와
  SERVO_PWM_DRIVER_USE_XILINX다. telemetry는 항상 포함하고 콘솔은 921600 baud다.
  Mono/단일 보드 양팔/One Euro/TRACE OFF 분기를 다시 추가하지 않는다.
- 공통 실행 흐름은 src/app/main.c, 명령은 src/app/uart/control_commands.c에 둔다.
  역할별 입력은 src/app/board/left_input.c 또는 right_input.c 하나만 빌드한다.
  main에 역할별 변수/조건부 컴파일을 다시 흩어놓거나 양쪽 main을 복제하지 않는다.
- 공개 통합 API는 RobotPipeline, human_target_update_stereo,
  robot_motion_set_target, robot_motion_step, gripper_control_update, servo_output_update다.
  UART A1/A2/P3/TK 태그·필드 순서는 호환성 때문에 유지한다.
- 마지막 설치 BOOT 이후 미배포 Virtual Compute·SD 작업을 보존한다.
  현재 소스와 설치 BOOT가 동일하다고 단정하지 않는다.
- 현행 경로: app/robot/vision → app/robot/control → drivers/pwm.
- src/ 최상위는 app, drivers다. app은 cnn, uart, board, robot으로 나누고,
  robot은 vision, control, record_replay로 나눈다. drivers는 pwm, uart, system이다.
  공개 헤더는 include/ 아래 같은 구조로 두고, 구현 전용 헤더는 소스 옆에 둔다.
  integration/ 같은 범용 폴더나 옛 경로의 전달 헤더를 다시 만들지 않는다.
- app/board는 LEFT/RIGHT 입력과 보드 간 통신 정책을 담당한다. UART0 초기화,
  IRQ/RX 링버퍼/FIFO 접근은 drivers/uart/board_uart.c에 둔다. 콘솔 UART1 입출력은
  drivers/uart/console_uart.c, 명령·메뉴·진단은 app/uart에 둔다.
  보드 공통 초기화·GIC·20 ms 타이머는 drivers/system/board_runtime.c다.
  HDMI 오버레이·SD 프레임 캡처는 UART 진단과 달리 app/cnn에 둔다.
- 채널 순서: elbow_roll, elbow_pitch, wrist_pitch, wrist_roll, gripper.
- A2 설정은 src/app/robot/control/forearm_calibration_config.c 한 곳이다.
  이전 robot_calibration_config.c 및 6축 A2 제어/FK는 다시 추가하지 않는다.
- JointCalibration은 forearm_calibration_config.h, RobotPoint3D는 robot_geometry.h에 둔다.
  공통 타입을 얻기 위해 폐기된 구현 헤더에 의존하지 않는다.
- 5축에서도 사용하는 A1 pose_mapping/pose_hand/HumanJointTarget과 관련
  진단·회귀 테스트는 유지한다. 이름이 과거형이라는 이유만으로 삭제하지 않는다.
- 소스 삭제 시 include, CMake, 테스트 실행기, Vitis 소스 등록 안내를 함께 검토한다.
- 서보 방향은 A2 direction에서 보정한다. 이를 위해 A1 각도 정의를 바꾸지 않는다.
  입력 매핑 direction과 FK의 물리 축 부호는 별개다. FK 부호를 자동 반전하지 않는다.
- 분리 보드 빌드에서 elbow_roll은 로봇0/RIGHT direction=+1(거울 시연),
  로봇1/LEFT direction=-1이며 offset=90이다. elbow_pitch는 direction=-1,
  offset=120이다. LEFT elbow_pitch 가속도는 60도/초², 최고속도는 30도/초다.
  LEFT 그리퍼 PWM은 1722~2200us로 제한한다(2026-10-13 닫힘 상한 변경). FK 물리 축은 반전하지 않는다.
  wrist 설정, PWM, 홈 자세 및 물리 가동범위는 이 결정으로 변경되지 않는다.
- 기본 CMake는 BUILD_TESTING=OFF이며 CPU 계산·제어 정적 라이브러리만 빌드한다.
  전체 보드 펌웨어는 vitis/setup_vitis.ps1로 빌드한다. 로컬 tests/, tools/, etc/가
  모두 준비된 경우에만 BUILD_TESTING=ON 및 로컬 검증 실행기를 사용한다.
  검증 자료가 없는데 시험을 통과했다고 보고하거나 기대값을 낮추지 않는다.
  실패는 수정 전 HEAD와 비교해 기존 실패/신규 회귀를 구분한다.
  보드 flash/서보 구동은 별도 사용자 요청 없이 하지 않는다.
- 통합에서는 CNN 완료 IRQ와 20 ms 로봇 제어 틱을 분리한다. 입력이 끊겨도
  마지막 승인 목표까지 이동한 후 유지한다. 입력 타임아웃만으로 Agent2 궤적을
  즉시 정지시키거나 로봇 PWM을 disable하지 않는다.
- 카메라 짐벌 PWM과 로봇팔 PWM은 서로 다른 IP다. 부팅 기본값은
  카메라 추적 OFF·pan/tilt PWM ON인 FIXED 모드다. UART `f`는 현재
  펄스에서 고정, `u`는 추적 ON/OFF 토글이다. 추적 중 `u`로 끄면
  종전처럼 카메라 PWM도 꺼지므로 고정이 필요하면 `f`를 쓴다.
- SD 가중치의 부팅 시 1회 로드는 유지한다. 통합 앱에서는 CNN 결과의
  SD CSV 상시 기록과 수동 `l` 메뉴를 사용하지 않는다. UART 명령과 TX 서비스는
  현재 foreground polling이며, 측정 없이 IRQ 방식으로 바꾸지 않는다.
- Agent3에게 별도로 로봇 동작 저장·재생 기능의 작업 지침이 전달됐다. CNN 결과 SD CSV와
  혼동하지 않는다. 녹화·재생의 통합 훅, 라이브 목표와의 모드 전환, SD 저장
  중 20 ms 틱 처리는 기존 구현의 경계를 유지하며 고정 50 Hz 시간축을 보존한다.
- 저장·재생을 연결할 새 코드는 `robot_motion_set_target()` 또는 `servo_output_update()`에 추가해도
  된다. 기존 라이브 경로의 검증, unwrap, 목표 설정, 20 ms 궤적 진행,
  PWM 변환·적용 로직과 호출 순서는 수정하지 않는다. 기본 모드에서는 기존과
  같은 입력에 같은 결과가 나와야 한다. 재생은 별도 모드/경로로 추가하고
  기존 분기를 재해석하거나 우회하지 않는다.

현재 검증 범위와 미해결 사항은 README.md에 요약한다. 상세 로그는 로컬 전용이다.
