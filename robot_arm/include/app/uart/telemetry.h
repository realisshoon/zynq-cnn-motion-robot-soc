#ifndef INTEGRATION_TELEMETRY_H
#define INTEGRATION_TELEMETRY_H

#include <stdint.h>

#include "app/robot/control/robot_pipeline.h"
#include "app/robot/vision/stereo/stereo_link.h"

#include "firmware_config.h"

/* 플랫폼이 세는 누적 통계. SM 줄에 실린다. */
typedef struct {
    uint32_t tick_overruns;       /* 한 틱보다 더 밀려서 버린 틱 수 */
    uint32_t uart_crc_errors;     /* PC->보드 UART 패킷 CRC 오류 */
    uint32_t uart_format_errors;  /* PC->보드 UART 패킷 형식 오류 */
    uint32_t uart_range_errors;   /* PC->보드 UART 패킷 좌표 범위 오류 */
    uint32_t uart_overwritten;    /* main이 읽기 전에 덮어쓴 프레임 수 */
} TelemetryPlatformStats;

/*
 * ---- 플랫폼 경계: 구현은 platform_vitis.c(보드) 또는 테스트 코드(호스트) ----
 */

/* 부팅 후 시간(마이크로초). 32비트라 71분마다 한 바퀴 돈다. 차이만 쓰므로 한 바퀴 돌아도 맞다. */
uint32_t platform_telemetry_time_us(void);

/* 누적 통계를 채운다. */
void platform_telemetry_stats(TelemetryPlatformStats *out);

/* TX FIFO에 지금 들어가는 만큼만 넣고 넣은 바이트 수를 돌려준다. 기다리지 않는다. */
uint32_t platform_telemetry_tx(const uint8_t *data, uint32_t len);

/*
 * ---- telemetry 본체 (telemetry.c) ----
 */

/* 부팅 직후 한 번: 컬럼 정의(# 줄)와 BOOT 이벤트를 쌓는다. platform_init() 뒤에 부른다. */
void telemetry_init(void);

/* 실행시간(dur_us) 측정 시작 시각을 지금으로 잡는다. */
void telemetry_mark(void);

/* human_target_update 직후. A1 줄과 P3 줄(Agent1 내부 Point3D)을 쌓는다. robot_motion_set_target 전에 불러야 원본 타겟이 보인다. */
void telemetry_pose_target(const RobotPipeline *ctx);

/* robot_motion_set_target 직후. A2 줄(결과, 거부 사유, 보정된 명령)을 쌓는다. */
void telemetry_robot_target(const RobotPipeline *ctx);
void telemetry_gripper(const RobotPipeline *ctx, const StereoDepthResult *depth, int result);

/* servo_output_update 직후(제어 틱 1회 뒤). TK 줄을 쌓는다. */
void telemetry_tick(const RobotPipeline *ctx);

/* main 루프 한 바퀴의 맨 끝: 링버퍼를 UART로 비우고, 1초마다 SM 줄을 쌓는다. */
void telemetry_poll(const RobotPipeline *ctx);
void telemetry_cnn_frame(uint32_t frame_id, uint32_t result_seq,
                     uint32_t irq_count, uint32_t elapsed_us,
                     uint32_t joint_flags, uint32_t overwritten);
void telemetry_cnn_error(uint32_t frame_id, int error, uint32_t irq_count);
void telemetry_camera(uint32_t frame_id, uint32_t state,
                  uint32_t pan_us, uint32_t tilt_us,
                  uint32_t pan_target_us, uint32_t tilt_target_us);
void telemetry_input(const HumanPose2D *pose);
void telemetry_cnn_coordinates(const StereoCoordinateFrame *frame);
void telemetry_stereo_pair(uint32_t pair_id, const StereoDepthResult *depth,
                       const Point2D *left_points, const Point2D *right_points);
void telemetry_stereo_admission(uint32_t pair_id, const StereoDepthResult *depth,
                            int accepted, const char *reason);
void telemetry_stereo_tracking(uint32_t pair_id, const StereoDepthResult *depth,
                           const char *state, unsigned candidates, uint32_t epoch,
                           uint64_t now_us);
/* Discard pending TRACE lines while muted; resume with a fresh schema. */
void telemetry_set_output_enabled(int enabled);
/* Suppress only robot-pipeline records; CNN/camera records remain visible. */
void telemetry_set_robot_output_enabled(int enabled);
int telemetry_robot_output_enabled(void);

#endif /* INTEGRATION_TELEMETRY_H */
