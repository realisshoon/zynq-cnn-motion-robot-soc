# Robot Arm Digital Twin — Unity 단독 단계

`Assets/Scenes/Main.unity`의 RobotArm을 선택하고 Inspector의 **Test Command**를 펼쳐 값을 변경한다. Edit Mode와 Play Mode 모두 즉시 반영한다. `valid=false`이면 관절과 손가락의 마지막 자세를 유지한다.

- 생성/기존 부분 구조 확장: `Tools > Human Motion > Build Robot Arm`
- 독립 Scene 자동 검증: `Tools > Human Motion > Validate Robot Arm`
- 검증 결과: `Validation/RobotArmValidation.md` (실행 시 갱신, 소스 SHA256 포함)
- 생성은 Undo 한 번으로 되돌릴 수 있다. 완성된 RobotArm에서 메뉴를 다시 실행하면 기존 설정을 보존하고 해당 객체를 선택한다. 비활성 객체도 검색한다.
- 기존 `RobotArm`, `BaseMesh`, `Base_Yaw`를 재사용한다. 시작 상태는 `Validation/Main.before-robot-arm.unity.txt`에 보관했다.

## 입력 계약과 표시

입력은 `robot_calibration` 이후 **최종 JointCommand**다. CNN 좌표, HumanJointTarget, PWM을 받지 않는다. pose mapping, robot calibration, 안전 검사, 속도 제한, 스무딩, UDP는 구현하지 않았다.

`JointCommandData`는 원본과 같은 필드명을 쓴다. 원본 `uint8_t valid`의 0/nonzero 의미를 C# `bool`로 표현한다. `frame_id`는 원본 JointCommand에 없는 선택적 확장 필드이며 현재 동작을 제어하지 않는다. C 구조체와 binary layout이 같다는 의미는 아니다.

`localRotation = restLocalRotation * AngleAxis((command_deg - neutral_deg) * direction, axis)`로 표시한다. 재활성화/반복 적용 시 현재 회전을 새로운 기준으로 캡처하지 않는다. `restLocalRotation`은 Builder 연결 시의 기본 local 회전이다. 각 관절의 Pivot, Axis, Direction, Neutral Deg를 Inspector에서 조정할 수 있다. 기본 축은 Y/X/X/X/Y이고 기본 neutral은 90°다. 각 축은 부모 좌표계에 대한 local 축이다.

Robot +X forward → Unity +Z, Robot +Z up → Unity +Y, Unity +X는 좌우다. Neutral은 위로 뻗은 placeholder 자세이며 실제 조립 자세를 보증하지 않는다. gripper는 local X 방향으로 대칭 이동하고 0=CLOSE, 1=OPEN이다. 손가락 치수/이동량도 시각화용이다.

| 입력 | 기준 소스의 참고 범위 | Unity 처리 |
|---|---|---|
| base_deg | 10~170° | clamp 없이 표시 |
| shoulder_deg | 20~160° | clamp 없이 표시 |
| elbow_deg | 10~170° | clamp 없이 표시 |
| wrist_pitch_deg | 20~160° | clamp 없이 표시 |
| wrist_roll_deg | 0~180° | clamp 없이 표시 |
| gripper_norm | 0~1 | 표시 범위만 0~1로 제한 |

NaN/Infinity는 Unity Transform에 기록하지 않고 전체 입력을 거부한다. 이는 로봇의 안전 판정을 대신하지 않는다. 현재 `Update`는 Inspector 테스트 입력을 적용한다. 향후 외부 입력 연결 시에는 이 입력 경로를 교체해야 한다.

## 읽기 전용 기준 소스

저장소: https://github.com/realisshoon/zynq-cnn-motion-robot-soc/tree/dev/robot

2026-09-21 `git ls-remote`로 확인한 SHA: `f7d8c0495383642ef0401571fe26e289d9765cf6`. 로컬 `origin/dev/robot`과 일치하는 것을 확인한 뒤 `git show`로 읽었다. checkout, fetch, 소스 변경, push는 수행하지 않았다.

읽은 우선 파일 (`robot_arm/` 기준):

- `README.md`
- `include/common/robot_types.h`
- `include/human_target_angle/pose_mapping.h`
- `src/human_target_angle/pose_mapping.c`
- `include/robot_calibration/robot_calibration.h`
- `src/robot_calibration/robot_calibration.c`
- `src/robot_calibration/robot_calibration_config.c`
- `include/output_controller/servo_config.h`
- `src/output_controller/servo_config.c`
- `src/output_controller/servo_control.c`
- `docs/interface.md`
- `docs/coordinate_system.md`

추가로 `docs/agent2_design_log.md`의 중립 자세/실측 미확정 기록을 확인했다. calibration의 `zero_offset_deg=90`과 servo의 `center_deg=90`을 시각화 baseline으로 사용한다. servo 초기 범위 0/90/180° → 500/1500/2500 µs는 참고만 하며 Unity에서 PWM을 계산하지 않는다. 문서는 2D 모드의 Base/Roll 고정을 기술하지만 현재 pose 코드에는 Base/Roll 계산 경로가 있다. 이 Twin은 요청대로 최종 명령의 다섯 각도 모두를 표시한다.

G51 CAD, 정확한 링크 치수, 조립된 서보의 물리 축/방향, 실제 홈 자세는 확인되지 않았다. 소스의 방향 +1도 Unity 축 또는 실제 조립 방향을 확정하는 근거로 쓰지 않았다. Builder의 모든 길이는 임의 Unity unit의 시각화 테스트 치수다.

## 재현할 자세

각 테스트는 Neutral(각도 전부 90°, gripper=1, valid=true)에서 시작한다.

| 테스트 | 입력 변경 | 기대 결과 |
|---|---|---|
| Neutral | 없음 | 각 Pivot local rotation=기본 회전 |
| A | base=120 | Base local Y +30°, 하위 전체 동반 회전 |
| B | shoulder=120, elbow=70 | local X +30°/-20°, Elbow world X +10° |
| C | wrist_pitch=110, wrist_roll=130 | local X +20° / local Y +40° |
| D | gripper=0 → 1 | 닫힘 → 열림 |
| HOLD | valid=false 후 값 변경 | 마지막 Transform 유지 |

다음 단계는 `JointCommand → UDP → Unity receiver`이며 이번 구현에는 포함되지 않는다.
