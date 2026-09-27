# 5축 A2 단일 경로 정리 — 2026-09-24

## 결정 및 수정

사용자 요청으로 기존 6축 A2 보존 지침을 폐기했다. 현행 정책은
[../AGENTS.md](../AGENTS.md)이다. 과거 인계 문서와 통합 기록에 정책 링크를 추가했다.

- 설정은 `src/robot_calibration/forearm_calibration_config.c`만 사용한다.
- elbow_roll/elbow_pitch direction: +1 → -1. scale=1, offset=90,
  범위 [20,160], 속도/가속도, wrist/gripper/PWM/홈 설정은 유지했다.
- 0° 입력 → 90° 명령, +30° 입력 → 60° 명령, -30° 입력 → 120° 명령.
- 6축 A2의 robot_calibration, robot_calibration_config, motion_control,
  motion_limits, motion_smoothing, safety_check 소스/헤더 12개와 전용 테스트
  4개를 삭제했다. 사용처가 사라진 6축 JointCommand 타입도 제거했다.
- JointCalibration은 forearm_calibration_config.h, RobotPoint3D는
  robot_geometry.h로 분리했다. 현행 5축 헤더가 삭제된 구현에 의존하지 않는다.
- CMake의 A2 소스/테스트를 forearm 경로로 전환했다. Python 실행기는
  기존 12개에서 폐기된 6축 전용 4개를 제외한 8개를 실행한다.
- 5축이 사용하는 A1 pose_mapping/pose_hand/HumanJointTarget 및 진단 테스트는
  유지했다. A1 연산과 Agent3 PWM 로직은 수정하지 않았다.
- README에 현재 설정/테스트/Vitis 소스 등록 안내를 추가했다. 아래 6축 안내는
  과거 기록으로 명시했다. main.c의 오래된 6채널 주석도 수정했다.

## 검증

기준 HEAD: `f59a91f`. 원본은 git archive로 저장소 밖 임시 폴더에 풀어 비교했다.

- GCC C99 `-Wall -Wextra -Wpedantic -Werror`: Python 실행기 8개 모두 컴파일 성공.
- 변경한 매핑의 중립/부호/양쪽 clamp 경계, 모션 속도·재목표 및 FK/충돌 단위
  테스트 PASS. A1 pose_mapping/body_frame도 PASS.
- Python 전체 결과: 4 PASS / 4 FAIL. 실패 4개는 원본에서도 같은 assert로 재현:
  - test_integration_smoke:425 — targets_valid/commands_accepted 기대값
  - test_trace:411 — A1 trace 필드 기대값
  - test_axis_replay:64 — agent1_run 성공 기대값
  - test_forearm_replay:206 — fresh==522 기대값
- CMake/Ninja Debug 전체 호스트 빌드 성공 (`CMAKE_C_FLAGS=-Werror`).
- CTest: 4 PASS / 2 FAIL. forearm_replay는 위와 동일.
  output_control:12의 거부 기대값 실패도 원본 소스를 별도 GCC 빌드해 재현했다.
- 기존 실패 테스트를 삭제하거나 assert를 완화하지 않았다. 같은 첫 assert가
  실패한다는 사실은 이후 경로까지 회귀가 없음을 보장하지 않는다.
- 삭제한 A2 파일에 대한 실행 코드/include/빌드 목록 참조 없음.

## 남은 확인

- 전체 테스트는 아직 GREEN이 아니다. 기존 A1/replay/trace/output 테스트의
  기대값과 현재 동작을 별도 진단해야 한다. 통과용 숫자로 덮어쓰지 않았다.
- 입력 direction 변경은 FK의 물리 축 부호를 자동 보정하지 않는다.
  서보 명령 증가 시 실제 축 방향과 FK 자세 일치는 실측해야 하며, 이번에는
  FK 수식이나 충돌 임계값을 임의 변경하지 않았다.
- Vitis 외부 workspace의 6축 A2 linked source를 제거하고 forearm_*.c 및
  motion.c를 등록해야 한다. 외부 Vitis 프로젝트는 이번에 수정/빌드하지 않았다.
- 보드 flash, 실제 서보 구동, 커밋/푸시 없음.
