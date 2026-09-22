#ifndef AGENT1_STAGE_H
#define AGENT1_STAGE_H

#include <stdint.h>

#include "common/robot_types.h"
#include "human_target_angle/pose_mapping.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Agent1 integration wrapper.
 *
 * 목적:
 * - main/integration code가 PoseMappingContext 내부 상태를 직접 다루지 않게 함
 * - 기존 pose_mapping_* API는 그대로 유지
 * - UART/CNN 입력 방식과 Agent1을 분리
 */

/* Agent1 context/output 초기화 */
int agent1_stage_init(void);

/*
 * HumanPose2D 1 frame 처리.
 *
 * return:
 *   1 : 새 HumanJointTarget 계산
 *   0 : 이전 정상 target HOLD
 *  -1 : 사용할 target 없음
 *
 * 다음 stage 진행 여부는 return 값이 아니라
 * agent1_stage_output_valid()로 판단할 것.
 */
int agent1_stage_run(
    const HumanPose2D *pose,
    PoseArmSide active_arm,
    float dt_sec
);

/* 마지막 Agent1 output 조회 */
const HumanJointTarget *agent1_stage_output(void);

/* 마지막 Agent1 output의 valid 여부 */
int agent1_stage_output_valid(void);

/*
 * 디버그/로그 전용: Agent1 내부 상태(PoseMappingContext)를 읽기 전용으로 조회한다.
 * - 3D 재구성 결과(shoulder_l_3d 등)와 target_age_sec를 UART 로그로 뽑을 때 쓴다.
 * - const 포인터라 호출자가 내부 상태를 바꿀 수 없다.
 * - 제어 흐름 판단에는 쓰지 말 것(다음 단계 진행 여부는 agent1_stage_output_valid()).
 * - 내용은 다음 agent1_stage_run() 호출 때 바뀐다.
 */
const PoseMappingContext *agent1_stage_debug_context(void);

/*
 * Wrist Roll zero calibration wrapper.
 *
 * 1차 통합에서는 호출하지 않아도 된다.
 * 추후 버튼/CLI 등의 명시적 trigger에서
 * agent1_stage_start_roll_zero_calibration()을 호출한다.
 */
int agent1_stage_start_roll_zero_calibration(void);

/* 현재 roll zero calibration 진행 중이면 1, 아니면 0 */
uint8_t agent1_stage_is_roll_zero_calibrating(void);

/* 저장된 roll zero offset 및 calibration 상태 초기화 */
void agent1_stage_clear_roll_zero(void);

#ifdef __cplusplus
}
#endif

#endif /* AGENT1_STAGE_H */
