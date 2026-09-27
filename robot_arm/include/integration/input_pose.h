#ifndef INTEGRATION_INPUT_POSE_H
#define INTEGRATION_INPUT_POSE_H

#include "common/robot_types.h"

/*
 * 입력 어댑터 경계 선언 (구현 없음).
 *
 * UART(PC 좌표 주입)든 CNN(레지스터 + 인터럽트)이든 HumanPose2D를 만들어서
 * 이 API로만 넘긴다. Agent1/2/3은 입력 방식을 몰라야 한다.
 *  - Vitis workspace 확정 후: UART 어댑터(uart_pose 파서 재사용) 구현
 *  - 호스트 테스트: 테스트 코드가 가짜 구현을 제공
 */

/* 어댑터 초기화 (파서 리셋 등). */
void input_pose_init(void);

/* 새 HumanPose2D 1개가 준비됐으면 1, 아니면 0. */
int input_pose_ready(void);

/*
 * 준비된 pose를 pose에 복사하고 직전 프레임과의 간격(초)을 dt_sec에 채운다.
 * 성공 1, 준비된 게 없으면 0. 첫 프레임의 dt_sec는 공칭 프레임 주기(20Hz면 0.05)를
 * 넣는 것을 권장한다(Agent1 담당자 확인).
 */
int input_pose_take(HumanPose2D *pose, float *dt_sec);

#endif /* INTEGRATION_INPUT_POSE_H */
