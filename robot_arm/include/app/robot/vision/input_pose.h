#ifndef INTEGRATION_INPUT_POSE_H
#define INTEGRATION_INPUT_POSE_H

#include "common/robot_types.h"

/*
 * 입력 어댑터 경계 선언. 보드 구현은 input_pose_cnn.c에 있다.
 *
 * CNN 결과를 HumanPose2D로 만들어서
 * 이 API로만 넘긴다. Agent1/2/3은 입력 방식을 몰라야 한다.
 *  - 호스트 테스트: 테스트 코드가 가짜 구현을 제공
 */

/* 어댑터 초기화. */
void input_pose_init(void);

/* 새 HumanPose2D 1개가 준비됐으면 1, 아니면 0. */
int input_pose_ready(void);

/*
 * 준비된 pose를 pose에 복사하고 직전 프레임과의 간격(초)을 dt_sec에 채운다.
 * 성공 1, 준비된 게 없으면 0. 첫 프레임은 임시 공칭 주기, 이후는 실제 완료 간격이다.
 */
int input_pose_take(HumanPose2D *pose, float *dt_sec);

#endif /* INTEGRATION_INPUT_POSE_H */
