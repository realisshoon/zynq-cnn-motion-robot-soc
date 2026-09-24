# 수정 지침 — 현행 5축 전완 로봇 (2026-09-24)

사용자 결정: 로봇 구동은 5축 forearm 경로만 지원한다. 과거 문서의
"legacy 6축 경로를 깨거나 삭제하지 말 것" 지침은 폐기한다.
이 문서가 현행 수정 지침이며 docs/agent2_notes.md 등 날짜별 기록은 이력이다.

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

이번 정리와 검증 결과: docs/agent2_cleanup_20260924.md.
