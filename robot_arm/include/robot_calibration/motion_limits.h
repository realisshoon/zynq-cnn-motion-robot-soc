#ifndef ROBOT_CALIBRATION_MOTION_LIMITS_H
#define ROBOT_CALIBRATION_MOTION_LIMITS_H

/*
 * 관절 개수/배열에 의존하지 않는 순수 스칼라 유틸리티. "몇 개의 값을
 * 다루는가"는 이 모듈이 알 필요가 없고, 호출하는 쪽(예: robot_calibration.c의
 * ROBOT_MOTION_JOINT_COUNT)이 알아서 반복 호출하면 된다 -- 관절 개수를
 * 나타내는 상수를 이 헤더와 호출하는 쪽 양쪽에 중복 정의하지 않기 위한
 * 설계다. docs/agent2_design_log.md 2026-09-17 항목 참고.
 */

/*
 * current를 target 방향으로 최대 max_delta_per_tick만큼만 이동시킨다.
 * 입력이 비유한(non-finite)이거나 max_delta_per_tick이 양수가 아니면
 * current를 그대로 반환한다.
 */
float motion_limits_step_toward(float current,
                                float target,
                                float max_delta_per_tick);

/*
 * ceil(abs(target - current) / max_delta_per_tick)을 반환한다. 이미 같은
 * 값이면 0. 입력이 비유한이거나, 이동이 필요한데 제한값이 양수가 아니거나,
 * 결과가 int 범위를 벗어나면 -1을 반환한다.
 */
int motion_limits_ticks_to_target(float current,
                                  float target,
                                  float max_delta_per_tick);

#endif /* ROBOT_CALIBRATION_MOTION_LIMITS_H */
