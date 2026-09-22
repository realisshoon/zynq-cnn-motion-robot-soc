# Agent3 Integration Handoff

기준: 로컬 HEAD `cba4832` 이후 Agent3 작업 트리 변경. Agent2 병렬 개발 중임을
전제로 현재 working tree만 확인했다. Agent2 브랜치 merge/pull/cherry-pick은 수행하지 않았다.

## 구현 상태 및 입력 계약

Software logical servo는 5개로 전환했다. `ForearmJointCommand`는 현재
`include/common/robot_types.h`에 없다. **BLOCKED BY AGENT2 INTERFACE**는
최종 입력 타입 및 이를 소비하는 public 변환 연결에 한정된다.
Agent2 공용 타입을 복제하거나 임의로 만들지 않았다.

현재 실제 선언:

```c
void output_control_init(void);
uint8_t output_control_update(const JointCommand *joint_cmd,
                              ServoPwmCommand *pwm_cmd);
uint8_t servo_control_convert(const JointCommand *joint_cmd,
                              ServoPwmCommand *pwm_cmd);
uint8_t servo_control_convert_channel(ServoChannel channel, float value,
                                      uint16_t *pwm_us);
void servo_hal_init(void);
int servo_hal_startup(void);
int servo_hal_apply(const ServoPwmCommand *cmd);
```

`output_control_update()`와 `servo_control_convert()`의 기존 signature는 임시로 남지만
**항상 0을 반환하고 출력 버퍼를 변경하지 않는다**. 기존 6축 입력을 새 관절로
재해석하지 않는다. 이 상태로 기존 integration에 연결해도 정상 변환은 불가능하다.
현재 integration의 옛 PWM 필드 참조도 별도 변경이 필요하다.

`servo_control_convert_channel()`은 Agent2 계약과 독립적인 하드웨어 변환 primitive다.
관절은 최종 서보 각도(deg), gripper는 0..1 값을 받는다. 사람 각도를 직접 넘기면 안 된다.
NULL/잘못된 채널/NaN/Inf는 실패(0)하며 출력은 유지한다. 유한 범위 초과는 clamp한다.

## 실제 5축 PWM 구조와 채널 매핑

`ServoPwmCommand`의 필드는 모두 `uint16_t`, 단위는 µs다.

| Logical channel | PWM 필드 | HW channel | Offset |
|---|---|---|---|
| 0 ELBOW_ROLL | elbow_roll_pwm_us | CH0 | 0x00 |
| 1 ELBOW_PITCH | elbow_pitch_pwm_us | CH1 | 0x04 |
| 2 WRIST_PITCH | wrist_pitch_pwm_us | CH2 | 0x08 |
| 3 WRIST_ROLL | wrist_roll_pwm_us | CH3 | 0x0C |
| 4 GRIPPER | gripper_pwm_us | CH4 | 0x10 |
| 없음 | 없음 | CH5 UNUSED | 0x14 |

`SERVO_COUNT=5`, `SERVO_PWM_DRIVER_CHANNEL_COUNT=5`,
`SERVO_PWM_DRIVER_HW_CHANNEL_COUNT=6`. CONTROL=0x18, UPDATE=0x1C를 유지했다.
CH5에 값을 쓰지 않는다. 이는 CH5의 물리 출력 disable 보장이 아니다.
기존 global enable과 RTL 동작은 그대로이며 실물에서는 미사용 채널로 취급해야 한다.

5개 ServoConfig 모두 기존 0/90/180 deg 및 500/1500/2500 µs를 임시 유지한다.
각 항목에 `NOT VERIFIED FOR NEW 5-AXIS MECHANISM`을 표시했다.
새 기구에 대한 실측 calibration이 아니며 startup center도 동일하게 미검증이다.

## 20 ms / 50 Hz 및 정상 흐름

호출 주기는 integration이 관리한다. 현재 platform의 20 ms tick과 동일하다.
최종 연결 후 흐름은 다음과 같다(현재는 Agent2 입력 경계에서 차단).

Agent2 Robot command → output_control_update → servo_control_convert
→ ServoPwmCommand → servo_hal_apply → driver.

최종 타입 확인 후 통합 담당자가 사용할 호출 패턴:

```c
/* joint_cmd의 타입/필드는 Agent2 최종 계약으로 확정해야 한다.
 * 현재 legacy 입력으로는 아래 성공 분기에 도달하지 않는다. */
if (output_control_update(joint_cmd, &pwm_cmd)) {
    if (!servo_hal_apply(&pwm_cmd)) {
        /* 기록: hardware apply 실패 */
    }
} else {
    /* 기록: command 변환 실패. pwm_cmd를 apply하지 않는다. */
}
```

## HAL, invalid 및 startup

HAL은 5개 PWM 전부를 먼저 검사한다. 하나라도 범위를 벗어나거나 NULL이면
쓰기를 시작하지 않고 0을 반환한다. 정상 command는 shadow 5회 → UPDATE 1회다.
startup은 config center 5회 → UPDATE → ENABLE 순서이며 실패 시 다음 단계로 진행하지 않는다.
초기화에는 output_control_init 및 servo_hal_init을 사용한다.

Driver 중간 실패에는 일부 shadow가 기록될 수 있고 rollback은 없다.
변환 실패는 disable/정지를 자동 실행하지 않으며 기존 하드웨어 출력 상태가 유지된다.
실패 후 재시도나 출력 중단 정책은 통합 담당자가 정한다.

## 필요한 integration / trace 변경 (이번 작업에서 수정하지 않음)

- Agent2 최종 merge 후 실제 ForearmJointCommand 존재와 필드/단위를 확인한다.
- Agent3 public 입력 signature 2개와 변환 구현을 실제 계약에 맞게 연결한다.
  임시 PWM 구조에 5개 변환을 모두 성공시킨 후 최종 출력을 갱신해 부분 출력을 방지한다.
- Integration context/pipeline을 최종 입력 타입과 새 5축 PWM 필드에 맞춘다.
- Trace의 기존 base/shoulder/elbow를 새 elbow_roll/elbow_pitch 의미로 변경하고,
  각도/PWM 출력, 배열 길이, 빈 필드 수, 파서 및 trace 테스트를 5채널로 갱신한다.
- 사람 각도, Robot 각도, PWM 단위를 구별한다. TK w는 HAL apply 성공일 때만 기록한다.
  변환 실패와 쓰기 실패를 구분하고 이전 PWM이 최신 결과로 보이지 않게 한다.
- 20 ms tick, 정상/invalid/driver 실패 경로를 통합 검증한다.

## Host/mock 검증

실행: `bash tests/output_controller/run_agent3_tests.sh`
C11, `-Wall -Wextra -Werror -pedantic`으로 Agent3 소스만 독립 빌드했다.

- test_servo_hal: PASS. logical/hardware 개수, 5필드 크기, 서로 다른 5 PWM의 주소/값/순서,
  CH5 쓰기 없음, 마지막 UPDATE, startup → enable, disable 주소,
  각 채널 상·하한 invalid의 쓰기 0회, NULL/잘못된 채널, driver 실패 시 중단 검증.
- test_servo_control: PASS. 5채널 보간/양끝 clamp, NaN/Inf/NULL/잘못된 채널 거부 검증.
- test_output_control: PASS. legacy valid/invalid/NULL을 거부하고 PWM 버퍼 보존 검증.

최종 설계 결정에 따라 Record/Playback 및 관련 buffer/button/test는 폐기한다.
이 테스트 삭제는 regression으로 분류하지 않으며 복구하지 않는다.
현재 소스에 RobotMode 사용처는 없고 CMake의 소스 참조만 남아 있어 새로 추가하지 않는다.
Output Controller 테스트는 invalid/NULL 거부, 호출별 PWM 버퍼 보존,
실패 시 HAL apply를 생략하는 호출자 패턴(실제 integration 테스트 아님)을 검증한다.
최종 5축 command → PWM 정상 성공 경로는 BLOCKED BY AGENT2 INTERFACE다.
HAL mock 테스트의 Xilinx 조건부 경계를 복구했고 기존 5채널 검증은 유지했다.
전용 스크립트는 HAL/output 테스트 번역 단위를 Xilinx 매크로로 문법 검사한다.
이는 BSP/real-driver 빌드나 보드 검증이 아니다.
공용 CMake는 변경하지 않았다. 전체 build는 실행하지 않았다. 현재 integration은
옛 6축 PWM 필드를 참조하므로 새 Agent3와 통합 빌드할 수 없는 상태다.

## Concurrent Agent2 Work Status

- Agent1 수정: NONE. Agent2 수정: NONE. common 수정: NONE. integration 수정: NONE.
- Vivado/RTL/XDC/XSA 및 공용 build script 수정: NONE.
- 기존 untracked `.zip`은 유지했다. 기존 handoff 문서는 삭제하지 않고 갱신했다.
- Agent2 외부 작업 완료 여부는 확인하지 않았다. 현재 ForearmJointCommand는 없음.
- BLOCKED: 최종 public 입력 계약 연결. 설정/PWM/HAL/Driver 5축 전환 및 독립 테스트는 완료.
- Board flash: NOT PERFORMED. Real servo drive: NOT PERFORMED.
