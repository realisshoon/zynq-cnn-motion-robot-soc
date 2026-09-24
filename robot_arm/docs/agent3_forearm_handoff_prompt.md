> 이 문서는 과거 인계 기록이다. 현행 5축 전용 수정 정책은 [../AGENTS.md](../AGENTS.md)를 따른다. 6축 보존 지침은 폐기되었다.

# Agent3(Output Controller) 담당 전달용 — 5축 인터페이스 변경 (2026-09-22)

Agent1→Agent2 계약은 이미 확정돼 있다(`HumanForearmTarget`, Agent1이 정의·구현·
호스트 검증 완료: `docs/agent1_forearm.md`, `docs/agent2_forearm_handoff_prompt.md`).
이 문서는 그 다음 단계, **Agent2→Agent3 계약**을 정리한다. Agent2(Claude)가
자기 파일 범위 안에서 새 5축 출력 타입과 보정 로직을 이미 구현·검증했지만,
**Agent3 쪽은 아직 아무것도 바뀌지 않았고 Agent2도 Agent3 파일을 건드리지 않았다.**
지금 보드는 100% 기존 6축 legacy 경로로 동작한다 — 이 문서는 다음 단계를 위한
계약 제안이지, 지금 당장 Agent3가 뭔가 고쳐야 한다는 뜻이 아니다.

## 1. 지금 상태

- `src/integration/agent_pipeline.c`, `main_integration.c`: 안 바뀜. 여전히
  `agent1_stage_run()` → `robot_calibration_apply()`(legacy `JointCommand`) →
  Agent3로 흐른다.
- `include/output_controller/*`, `src/output_controller/*`, `src/drivers/*`:
  Agent2가 전혀 안 건드렸다. `SERVO_COUNT=6`, `JointCommand` 6필드 그대로다.
- Agent2가 새로 만든 것(`include/robot_calibration/forearm_*`,
  `src/robot_calibration/forearm_*`)은 지금 어디에서도 호출되지 않는
  독립 모듈이다 — 파이프라인 연결은 이번 스코프 밖(사용자 지시로 보류)이었다.

## 2. 기존 계약 (변경 없음, 참고용)

```c
/* include/output_controller/servo_config.h */
typedef enum {
    SERVO_BASE = 0, SERVO_SHOULDER, SERVO_ELBOW,
    SERVO_WRIST_PITCH, SERVO_WRIST_ROLL, SERVO_GRIPPER,
    SERVO_COUNT   /* = 6 */
} ServoChannel;

/* include/common/robot_types.h */
typedef struct {
    float base_deg, shoulder_deg, elbow_deg, wrist_pitch_deg, wrist_roll_deg;
    float gripper_norm;
    uint8_t valid;
} JointCommand;   /* robot_calibration -> output_controller, 필드 순서 = ServoChannel 순서 */
```

`servo_control.c`/`servo_config.c`/`output_control.c`는 이 6필드/6채널을 그대로
가정하고 짜여 있다(`servo_configs[SERVO_COUNT]` 배열 등). **이번 문서가 제안하는
변경 전까지는 그대로 둘 것.**

## 3. 새 Agent2 출력 계약 (제안)

```c
/* include/robot_calibration/forearm_motion_control.h (신규, Agent2 소유) */
typedef struct {
    float elbow_roll_deg;   /* M0 */
    float elbow_pitch_deg;  /* M1 */
    float wrist_pitch_deg;  /* M2 */
    float wrist_roll_deg;   /* M3 */
    float gripper_norm;     /* M4, 0=CLOSE, 1=OPEN */
    uint8_t valid;
} ForearmJointCommand;
```

- `include/common/robot_types.h`의 `JointCommand`와 **별개 타입**이다. 공유
  헤더는 건드리지 않았으므로 기존 legacy 빌드/Agent3에는 영향이 없다.
- 값은 전부 "보정 이후 서보 목표각(도)". 90=중립으로 가정했지만 **미실측**
  (아래 6절).
- 채널 수가 6→5로 줄었다: `base`+`shoulder` 2축이 `elbow_roll`+`elbow_pitch`
  2축으로 대체된 게 아니라, 예전 3관절(base/shoulder/elbow)이 2관절
  (elbow_roll/elbow_pitch)로 합쳐졌다. `wrist_pitch`/`wrist_roll`/`gripper`는
  이름은 같지만 **새 물리 관절이다**(전완 길이/방향 기준이 다름 — 예전
  wrist 캘리브레이션을 재사용하면 안 된다. `docs/agent1_forearm.md` 4절).

## 4. Agent3가 결정해야 할 것 (Agent2가 대신 정하지 않음)

1. **채널 표현**: `ServoChannel`/`SERVO_COUNT`를 5로 줄일지, 6을 유지하고
   1개(base 또는 shoulder에 해당하던 채널)를 미사용으로 둘지. PL(AXI/PWM IP)
   배선을 바꿀 수 있는지에 달려있다 — 이건 실물/Vivado 쪽 결정이라
   Agent2가 판단할 수 없다.
2. **M0~M4 실제 배선**: 어느 PWM 채널이 elbow_roll인지 elbow_pitch인지 등
   물리 배선 확인 필요.
3. **PWM 보정**: 새 관절 4개(elbow_roll/elbow_pitch/wrist_pitch/wrist_roll)는
   전부 새 서보거나 재배치된 서보라, `servo_configs[]`의 기존 us 범위를
   그대로 재사용해도 되는지 실측 확인 필요. 기존 실측 PWM calibration을
   임의로 덮어쓰지 말 것(Agent1 프롬프트에서도 명시).
4. **변환 함수**: 기존 `output_control.c`의 `JointCommand -> ServoPwmCommand`
   함수를 그대로 둔 채 `ForearmJointCommand -> ServoPwmCommand`를 새로
   추가할지(legacy 경로 보존, Agent1/Agent2가 해온 "새 파일 병행" 패턴과
   일관됨), 아니면 다른 방식으로 통합할지.
5. **trace/record 자료형**: `docs/agent1_forearm.md` 12절이 이미 지적한 대로
   trace/record-playback 채널 길이·저장 포맷도 5채널 기준으로 다시 봐야
   한다. 실제 record 소스가 있는지부터 확인할 것(존재하지 않는 소스를
   추측하지 말 것).

## 5. Agent2가 이번에 한 일 / 안 한 일 (재확인용)

했음: `forearm_calibration_config.c/.h`, `forearm_motion_control.c/.h`,
`forearm_safety_check.c/.h`, `forearm_calibration.c/.h` 신규 4쌍 +
`tests/robot_calibration/test_forearm_*` 3개 + `run_tests.py` 확장.
`python tests/robot_calibration/run_tests.py` 12개 스위트 PASS(기존 9개
legacy 무변경 회귀 포함).

안 했음: `agent_pipeline.c`/`main_integration.c` 연결, Agent3 파일 전부,
보드 빌드/실물 서보 구동.

## 6. 실측 전 미확정값 (Agent3 작업 전 필요)

- elbow_roll/elbow_pitch/wrist_pitch/wrist_roll: 실제 회전 방향, 90도가
  진짜 중립인지, 실제 가동범위(현재 `forearm_calibration_config.c`에
  scale=1,direction=1,offset=90,[20,160] 임시값).
- 링크 24cm(팔꿈치-손목)/10cm(손목-그리퍼) 배정: 사진 기반 추정, 확인 필요
  (`forearm_safety_check.c` 상수 2개).
- 팔꿈치 원점의 테이블면 대비 높이: 현재 `TABLE_SURFACE_Z_CM=-5`(보수적
  가정, 실측 아님).
- 손목 기구 순서(roll이 먼저 비트는지 pitch가 먼저 굽히는지).

## 7. Agent3 담당 Codex/작업자 전달용 프롬프트 (복사해서 사용)

```text
너는 이 C 로봇팔 프로젝트의 Agent3(Output Controller / PWM) 담당이다.
Agent1(HumanForearmTarget)과 Agent2(ForearmJointCommand, 아직 파이프라인
미연결)가 새 5축 수평 설치 구조로 이미 전환했다. 이 문서
(docs/agent3_forearm_handoff_prompt.md) 2~6절이 계약과 미확정값 목록이다.

먼저 읽을 파일:
- docs/agent3_forearm_handoff_prompt.md (이 문서)
- docs/agent1_forearm.md, docs/agent2_forearm_handoff_prompt.md
- include/robot_calibration/forearm_motion_control.h (ForearmJointCommand 정의)
- include/output_controller/servo_config.h, servo_control.h, output_control.h
- src/output_controller/*.c, src/drivers/servo_pwm_driver.c

지금 하지 말 것:
- 기존 legacy 6축 경로(SERVO_COUNT=6, JointCommand)를 깨거나 삭제하지 말 것 -
  보드는 아직 그 경로로만 동작한다.
- 실측 안 된 PWM 값을 임의로 만들지 말 것(위 6절 목록). 안전 승인/보드 flash도
  별도 지시 없이 하지 말 것.
- Agent1/Agent2 파일(include/human_target_angle/*, include/robot_calibration/
  forearm_*)을 고치지 말 것 - 인터페이스 문제를 발견하면 Agent2에 보고하고
  이 문서에 기록하되, 직접 바꾸지 말 것.

할 일:
- 4절의 5개 결정 사항에 대한 제안(채널 표현, M0~M4 배선, PWM 보정 범위,
  변환 함수 추가 방식, trace/record 자료형)을 문서로 정리해 보고하라.
- 가능하면 ForearmJointCommand -> ServoPwmCommand 변환 함수의 초안을 새
  파일로 작성하되(기존 output_control.c는 건드리지 않는다), 6절의 미확정
  값은 명시적으로 TODO/미보정으로 표시하고 실제 서보에 연결하지 마라.
- 결과를 이 문서 8절(신설)에 "Agent3 처리 결과"로 추가하라.
```
