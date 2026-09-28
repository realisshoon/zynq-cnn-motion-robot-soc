# 수정 지침 — 현행 5축 전완 로봇 (2026-09-24)

사용자 결정: 로봇 구동은 5축 forearm 경로만 지원한다. 과거 문서의
"legacy 6축 경로를 깨거나 삭제하지 말 것" 지침은 폐기한다.
이 문서가 현행 수정 지침이며 docs/agent2_notes.md 등 날짜별 기록은 이력이다.
CNN 통합의 현행 실행 흐름, 사용자 결정, 에이전트별 수정 경계는
[docs/integration_work_plan.md](docs/integration_work_plan.md)를 따른다.
과거 Agent3/Agent4 handoff의 당시 상태나 수정 금지 목록을 현행 통합 작업에
그대로 적용하지 않는다.

- 현행 경로: agent1_forearm_stage → forearm_calibration → output_controller.
- 채널 순서: elbow_roll, elbow_pitch, wrist_pitch, wrist_roll, gripper.
- A2 설정은 src/robot_calibration/forearm_calibration_config.c 한 곳이다.
  이전 robot_calibration_config.c 및 6축 A2 제어/FK는 다시 추가하지 않는다.
- JointCalibration은 forearm_calibration_config.h, RobotPoint3D는 robot_geometry.h에 둔다.
  공통 타입을 얻기 위해 폐기된 구현 헤더에 의존하지 않는다.
- 5축에서도 사용하는 A1 pose_mapping/pose_hand/HumanJointTarget과 관련
  진단·회귀 테스트는 유지한다. 이름이 과거형이라는 이유만으로 삭제하지 않는다.
- 소스 삭제 시 include, CMake, 테스트 실행기, Vitis 소스 등록 안내를 함께 검토한다.
- 서보 방향은 A2 direction에서 보정한다. 이를 위해 A1 각도 정의를 바꾸지 않는다.
  입력 매핑 direction과 FK의 물리 축 부호는 별개다. FK 부호를 자동 반전하지 않는다.
- 현재 elbow_roll/elbow_pitch direction=-1, offset=90이다.
  wrist 설정, PWM, 홈 자세 및 물리 가동범위는 이 결정으로 변경되지 않는다.
- 검증: python tests/robot_calibration/run_tests.py (robot_arm에서 실행).
  실패는 수정 전 HEAD와 비교해 기존 실패/신규 회귀를 구분한다.
  보드 flash/서보 구동은 별도 사용자 요청 없이 하지 않는다.
- 통합에서는 CNN 완료 IRQ와 20 ms 로봇 제어 틱을 분리한다. 입력이 끊겨도
  마지막 승인 목표까지 이동한 후 유지한다. 입력 타임아웃만으로 Agent2 궤적을
  즉시 정지시키거나 로봇 PWM을 disable하지 않는다.
- 카메라 짐벌 PWM과 로봇팔 PWM은 서로 다른 IP다. 초기 자세 검증 중에는
  카메라 추적을 중지하고 PWM으로 pan/tilt를 고정하는 모드를 계획한다.
  현재 UART `u`는 추적과 카메라 PWM을 함께 끄므로 고정 모드가 아니다.
- SD 가중치의 부팅 시 1회 로드는 유지한다. CNN 결과의 SD CSV 상시 기록은
  중단하기로 결정했지만 아직 구현되지 않았다. UART 명령과 TX 서비스는
  현재 foreground polling이며, 측정 없이 IRQ 방식으로 바꾸지 않는다.
- Agent3에게 별도로 로봇 동작 저장·재생 기능의 작업 지침이 전달됐다. CNN 결과 SD CSV와
  혼동하지 않는다. 녹화·재생의 통합 훅, 라이브 목표와의 모드 전환, SD 저장
  중 20 ms 틱 처리는 docs/integration_work_plan.md의 인계 경계를 따른다.
- 저장·재생을 연결할 새 코드는 `agent2_run()` 또는 `agent3_run()`에 추가해도
  된다. 기존 라이브 경로의 검증, unwrap, 목표 설정, 20 ms 궤적 진행,
  PWM 변환·적용 로직과 호출 순서는 수정하지 않는다. 기본 모드에서는 기존과
  같은 입력에 같은 결과가 나와야 한다. 재생은 별도 모드/경로로 추가하고
  기존 분기를 재해석하거나 우회하지 않는다.

이번 정리와 검증 결과: docs/agent2_cleanup_20260924.md.
