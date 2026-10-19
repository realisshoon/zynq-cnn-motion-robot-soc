#include "app/robot/control/forearm_calibration_config.h"
#include "robot_board_config.h"

#if defined(ROBOT_STEREO_RIGHT)
#define ROBOT_ELBOW_ROLL_DIRECTION 1
#else
#define ROBOT_ELBOW_ROLL_DIRECTION -1
#endif

#if defined(ROBOT_STEREO_LEFT)
#define ROBOT_ELBOW_PITCH_AMAX_DEG_S2 60.0f
#else
#define ROBOT_ELBOW_PITCH_AMAX_DEG_S2 120.0f
#endif

const ForearmCalibrationConfig forearm_calibration_config = {
    //중앙은 맞는데 기본적인 자세가 오른팔을 기준으로 안쪽으로 기울어져 있어서 왼쪽에 있는 경향성이 있음
    .elbow_roll  = { .scale = 1.0f, .direction = ROBOT_ELBOW_ROLL_DIRECTION, .zero_offset_deg = 90.0f,
                      .min_deg = 10.0f, .max_deg = 170.0f, .max_delta_deg = 0.60f },
    //마찬가지로 기본적인 자세가 로봇+방향(밑쪽)으로 기울어져 있어서 offset과 max를 높였음
    .elbow_pitch = { .scale = 1.0f, .direction = -1, .zero_offset_deg = 120.0f,
                      /* A3 ServoConfig의 180도 상한과 일치시킨다. */
                      .min_deg = 20.0f, .max_deg = 180.0f, .max_delta_deg = 0.60f },
    //초기값은 손목이 ㄱ자 방향으로 설정되어서 기준을 조금 낮췄음
    .wrist_pitch = { .scale = 1.0f, .direction = 1, .zero_offset_deg = 80.0f,
                      .min_deg = 10.0f, .max_deg = 160.0f, .max_delta_deg = 0.70f },
    .wrist_roll  = { .scale = 1.0f, .direction = -1, .zero_offset_deg = 87.0f,
                      .min_deg = 10.0f, .max_deg = 170.0f, .max_delta_deg = 0.70f },
    /* 순서: elbow_roll, elbow_pitch, wrist_pitch, wrist_roll. */
    .amax_deg_s2 = { 120.0f, ROBOT_ELBOW_PITCH_AMAX_DEG_S2, 120.0f, 120.0f },
};
