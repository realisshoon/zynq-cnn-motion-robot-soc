# STEP 1 최종 검증 보고

STEP 1 RESULT = PASS

실행일: 2026-09-22. 범위는 CSV mock CNN부터 canonical final output, Agent3/PWM host 경로와 실제 Unity Main UDP/관절 회전까지다. 하드웨어 미연결 시 허용된 software/PWM 검증 조건으로 PASS다. 실제 로봇팔의 운동을 확인했다는 의미는 아니다.

## 1. 실제로 읽은 기존 파일

외부 checkout과 Git blob을 읽었다. 빌드 기준 commit은 `240007988aac24049e3775fb25d69999f6d30552`, 보존한 checkout HEAD는 `905b943dcf323a96bb19a5515316fa23842264b7`다.

- `robot_arm/include/common/robot_types.h`: HumanPose2D, HumanJointTarget, JointCommand.
- `include/integration/agent_pipeline.h`, `src/integration/agent_pipeline.c`: 입력/tick 분리, accepted command와 current output, Agent1/2/3 API.
- `include/human_target_angle/agent1_stage.h`, `pose_mapping.h`, `src/human_target_angle/agent1_stage.c`, `pose_mapping.c`, `pose_tracking.c`, `pose_hand.c`, `pose_joint.c`, `pose_math.c`, `pose_reconstruction.c`: RIGHT ARM 계산 및 HOLD 정책.
- `src/robot_calibration/robot_calibration.c`, `robot_calibration_config.c`, `motion_control.c`, `motion_limits.c`, `motion_smoothing.c`, `safety_check.c`와 대응 header/config: validate, unwrap, limit, safety, smoothing.
- `src/output_controller/output_control.c`, `servo_config.c`, `servo_control.c`, `servo_hal.c`, `src/drivers/servo_pwm_driver.c`와 대응 header: 실제 PWM 변환 및 mock/board 경로.
- `tests/human_target_angle/test_pose_mapping.c`, robot_calibration 테스트 4개, servo control/HAL/output control 테스트, `tests/integration/test_integration_smoke.c`.
- Unity `Assets/Scripts/RobotArmController.cs`, `JointCommandData.cs`, `UdpJointCommandReceiver.cs`, `G51GripperVisual.cs`; `Assets/Scenes/Main.unity`; 기존 G51 visual builder; `Tools/AgentReplay`의 기존 converter/sender/build scripts.

Unity JSON은 frame_id(uint), valid(bool), base_deg, shoulder_deg, elbow_deg, wrist_pitch_deg, wrist_roll_deg, gripper_norm의 8개 필드다. `ApplyCommand`는 invalid/비유한값을 거부한다. Pivot chain은 `RobotArm/Base_Yaw/Shoulder_Pitch/Elbow_Pitch/Wrist_Pitch/Wrist_Roll/Gripper`이며 그대로 보존했다.

## 2. 추가한 파일

- `Tools/AgentReplay/test_csv_parser.c`, `verify_step1_host.py`, `verify_step1_unity.py`, `README.md`.
- `Assets/Editor/Step1ReplayValidation.cs`, `Assets/Scripts/Validation/JointCommandReplayProbe.cs` 및 Unity 생성 `.meta`.
- `Validation/Step1HostValidation.md`, `Step1UnityValidation.md`, `Step1MainCleanup.md`, 본 보고서, `step1_unity_applied.csv`, `step1_status.json`.
- `Tools/AgentReplay/build`의 canonical 원본 snapshot, 실행 파일, manifest, command/PWM/frame trace, host/Unity 결과 JSON, sender log.

## 3. 수정한 기존 파일

- `csv_pose_to_joint_trace.c`: CSV 엄격 검사, canonical Agent1/Agent2 결과 계수, 같은 final output의 Agent3/PWM 실행·trace, float round-trip 정밀도.
- `build_canonical_replay.py`: 로컬 canonical commit 고정, ROBOT_TRACE 진단 빌드, GCC DLL 경로와 원본 SHA256 기록.
- 앞선 replay 준비에서 정리한 `build_and_convert.ps1`, `build_and_convert_external_repo.ps1` 진입점을 계속 사용한다.

이번 데이터 흐름 구현에서 Main, RobotArmController, UDP receiver, G51 visual/pivot은 수정하지 않았다. 기존 Dual 편집 내용은 Main 전환 전에 보존 저장했고 STEP 1 판정에 포함하지 않았다.

## 4. CSV row count

`example_pose2d_1280x720_20hz.csv`: header 제외 **522행**. 1280×720 pixel 좌표를 그대로 HumanPose2D의 두 shoulder, right elbow/wrist, 두 finger로 매핑했다.

## 5. valid / hold / rejected 통계

| 계층 | 결과 |
|---|---|
| CSV frame valid / invalid | 522 / 0 |
| Agent1 새 target / HOLD / invalid | 522 / 0 / 0 |
| Agent2 새 목표 / 동일 목표 승인 | 46 / 3 |
| Agent2 입력 검증 거부 / 안전 검사 거부 | 0 / 473 |
| Agent2 target 없음 | 0 |

HOLD/invalid가 원본 CSV에 없어 별도 canonical fixture로 실행했다. 안전 거부 473개는 실패를 숨긴 것이 아니라 기존 정책대로 마지막 승인 목표를 유지한 결과다.

## 6. Agent1 결과 검증

PASS: CSV field mapping, 정상/repeated frame, body 짧은 HOLD/장기 invalid/복구, finger 누락 시 주요 관절 지속·wrist/gripper HOLD. 원본 pose mapping 및 integration smoke 실행. local parser 테스트에서도 실제 Agent1 API에 frame_valid=0을 주입하여 짧은 HOLD, timeout, 정상 frame 복구를 검증했다.

## 7. Agent2 결과 검증

PASS: calibration, joint limits, 안전 거부, unwrap `179,-179,179 → 179,181,179`, 20 ms smoothing, HOLD 중 재계획 억제, gripper 경로. canonical 테스트 8개 + local parser 1개 PASS. obsolete output_control 테스트 1개는 삭제된 motion_record 의존성으로 SKIP했으며 현재 Agent3/PWM 경로는 실제 전체 trace로 별도 검증했다.

## 8. 20 ms output tick 수

**제어 tick 1,353개 + 초기 home 1개 = trace 1,354행**, 0~27.06초. 같은 source frame 사이에서 값이 변하는 output 56개를 확인하여 pose와 servo 시계 분리를 검증했다. 모든 tick은 finite, joint limits 및 tick 속도 제한을 통과했다.

## 9. Unity UDP 전송 수

기존 sender로 loopback `127.0.0.1:5005`에 **1,354개** 전송. 실제 수신도 **1,354개**다.

## 10. Unity 통계

| applied | captured | stale | malformed | invalid | 캡처 누락 |
|---:|---:|---:|---:|---:|---:|
| 1,352 | 1,352 | 0 | 0 | 0 | 0 |

모든 적용 명령은 finite다. 기존 최신 mailbox 구조 때문에 렌더 사이 중간 tick 2개는 교체됐다. 적용된 1,352개 모두 같은 tick의 trace 및 실제 5개 pivot·gripper 회전을 대조했다. 마지막 frame 1354까지 도달했다.

## 11. Physical Robot path

PASS (HOST_MOCK_ONLY): `ctx.output → agent3_run → output_control_update → servo_control_convert → servo_hal_apply → servo_pwm_driver`. **1,354개**, servo errors **0**. 6채널 PWM 500~2500 µs, channel offsets, ENABLE/UPDATE register write를 검사했다. Canonical integration smoke는 startup shadow→UPDATE→ENABLE 순서도 통과했다.

## 12. Physical / Unity 동일 command

PASS: physical trace와 Unity 전송 trace **1,354개 전체 공통 필드 일치**. 실제 Unity 적용 **1,352개 전체 동일 frame_id로 일치**. 최대 명령 표시 오차 `0.000008`, 최대 transform 오차 `0.0000227°`. Agent1/2 수식을 C#으로 재구현하지 않았다.

## 13. Main regression

PASS: Manual 5관절, linkage gripper 0/1, invalid HOLD, NaN 거부, UDP 모드 Manual overwrite 방지. Play 종료 시 receiver thread Join 및 UDP 5005 exclusive 재bind PASS. 저장된 Main과 핵심 파일 9개 SHA256 불변. Play 종료 후 원래 Manual 상태로 복귀했다.

## 14. 외부 Git 변경 여부

이 작업은 외부 source/HEAD/branch를 변경하거나 fetch하지 않았다. 작업 파일 **35개 SHA256 및 파일 목록 불변**, HEAD 불변. 빌드 전후 git status도 일치한다. 현재 로컬 remote-tracking ref의 commit을 읽었으며 canonical 변경은 하지 않았다. 이전에 다른 작업으로 갱신된 remote ref를 이 작업의 변경으로 주장하지 않는다.

## 15. 사용자 실행 방법

`Tools/AgentReplay/README.md`의 PowerShell 명령으로 build/host test 실행 → Unity **Tools / Human Motion / STEP 1 / Run Main CSV Validation** → `verify_step1_unity.py` 실행 → Play 종료 순서다. Main asset을 수정할 필요가 없다.

## 16. 하드웨어 미검증 항목

이번 실행에서 사용할 보드/서보 연결이 없었다. 확인된 serial 목록은 Bluetooth COM7/8/9/10이었다. 실제 Zynq MMIO/PWM 핀 파형, 서보 운동, 부하·기계 offset, physical/Unity wall-clock 지연과 기계적 각도 일치는 미검증이다. Host mock을 실제 하드웨어 구동으로 보고하지 않는다.

## 17. STEP 2 진행 여부

**YES — 소프트웨어 데이터 흐름 기반으로 STEP 2 진행 가능.** 실제 로봇 운전 검증은 별도 남아 있다. Drawing Mode는 이번 작업에서 구현하지 않았다.

근거 파일: `Step1HostValidation.md`, `Step1UnityValidation.md`, `Step1MainCleanup.md`, `DualIntegrityValidation.md`, `../Tools/AgentReplay/build/step1_host_results.json`, `step1_unity_results.json`, `canonical_manifest.json`.
