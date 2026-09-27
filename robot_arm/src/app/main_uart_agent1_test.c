/*
 * Vitis bare-metal UART + Agent1 test application
 *
 * Temporary test path:
 * PC CSV/video coordinate data
 *   -> UART 115200 / 20Hz
 *   -> uart_pose_rx
 *   -> HumanPose2D
 *   -> pose_mapping_update() (Agent1)
 *   -> HumanJointTarget
 *
 * IMPORTANT:
 * Agent1 source itself is not modified for UART.
 * UART is only an input adapter that reconstructs the same HumanPose2D that
 * the real CNN register/interrupt path will eventually provide.
 */

#include <stdint.h>

#include "xparameters.h"
#include "xil_printf.h"
#include "xstatus.h"
#include "xtime_l.h"

#include "common/robot_types.h"
#include "human_target_angle/pose_mapping.h"
#include "uart_pose/uart_pose_rx_vitis.h"

#ifndef UART_POSE_DEVICE_ID
#define UART_POSE_DEVICE_ID XPAR_XUARTPS_0_DEVICE_ID
#endif

#define UART_POSE_BAUD_RATE 115200U

/*
 * 현재 UART packet의 HumanPose2D에는 active_arm 필드가 없다.
 * 임시 테스트에서 사용할 팔을 여기서 선택한다.
 */
#define UART_TEST_ACTIVE_ARM POSE_ARM_RIGHT

static float seconds_from_ticks(XTime ticks)
{
    return (float)((double)ticks / (double)COUNTS_PER_SECOND);
}

static float calculate_frame_dt(
    XTime now,
    XTime *prev_time,
    uint8_t *prev_valid
)
{
    float dt;

    if (!(*prev_valid)) {
        *prev_time = now;
        *prev_valid = 1U;
        return 0.050f; /* 첫 frame fallback: 20 Hz */
    }

    dt = seconds_from_ticks(now - *prev_time);
    *prev_time = now;

    /*
     * Serial debug/PC scheduling 지연으로 비정상 dt가 생겨도
     * 첫 통합 테스트가 무너지지 않도록 최소한의 sanity check.
     * Agent1 내부에서 filter dt는 추가 sanitize된다.
     */
    if (dt <= 0.0f || dt > 0.500f) {
        dt = 0.050f;
    }

    return dt;
}

static int deg_x100(float deg)
{
    if (deg >= 0.0f) {
        return (int)(deg * 100.0f + 0.5f);
    }
    return (int)(deg * 100.0f - 0.5f);
}

int main(void)
{
    UartPoseReceiver pose_rx;
    PoseMappingContext agent1_ctx;

    HumanPose2D pose;
    HumanJointTarget target;

    XTime prev_frame_time = 0U;
    uint8_t prev_frame_time_valid = 0U;

    int status;

    xil_printf("\r\n");
    xil_printf("=== UART -> Agent1 test start ===\r\n");
    xil_printf("UART: 115200, pose: 1280x720, target: 20Hz\r\n");

    status = uart_pose_rx_init(
        &pose_rx,
        UART_POSE_DEVICE_ID,
        UART_POSE_BAUD_RATE
    );
    if (status != XST_SUCCESS) {
        xil_printf("[ERR] uart_pose_rx_init failed: %d\r\n", status);
        return XST_FAILURE;
    }

    if (pose_mapping_init(&agent1_ctx) != 0) {
        xil_printf("[ERR] pose_mapping_init failed\r\n");
        return XST_FAILURE;
    }

    xil_printf("[OK] waiting pose packets...\r\n");

    while (1) {
        XTime now;
        float dt_sec;
        int agent1_ret;

        /*
         * 1) UART byte 수신/packet parser
         *    packet 완성 시 pose_ready flag가 set된다.
         */
        uart_pose_rx_poll(&pose_rx);

        /*
         * 2) 새 1-frame HumanPose2D가 있을 때만 Agent1 실행
         */
        if (!uart_pose_rx_take_frame(&pose_rx, &pose)) {
            continue;
        }

        XTime_GetTime(&now);
        dt_sec = calculate_frame_dt(
            now,
            &prev_frame_time,
            &prev_frame_time_valid
        );

        agent1_ret = pose_mapping_update(
            &agent1_ctx,
            &pose,
            UART_TEST_ACTIVE_ARM,
            dt_sec,
            &target
        );

        /*
         * xil_printf는 float printf를 지원하지 않는 환경이 많아서
         * 각도를 x100 정수로 출력한다.
         */
        xil_printf(
            "[A1] frame=%lu pose=%u ret=%d target=%u "
            "base_x100=%d sh_x100=%d el_x100=%d "
            "pitch_x100=%d roll_x100=%d "
            "crc_err=%lu overwrite=%lu\r\n",
            (unsigned long)pose.frame_id,
            (unsigned int)pose.valid,
            agent1_ret,
            (unsigned int)target.valid,
            deg_x100(target.base_deg),
            deg_x100(target.shoulder_deg),
            deg_x100(target.elbow_deg),
            deg_x100(target.wrist_pitch_deg),
            deg_x100(target.wrist_roll_deg),
            (unsigned long)pose_rx.parser.crc_errors,
            (unsigned long)pose_rx.overwritten_frames
        );
    }
}
