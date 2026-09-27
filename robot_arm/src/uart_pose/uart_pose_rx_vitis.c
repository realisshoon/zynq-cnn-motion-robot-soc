#include "uart_pose/uart_pose_rx_vitis.h"

#include <stddef.h>
#include <string.h>

#include "xstatus.h"

int uart_pose_rx_init(
    UartPoseReceiver *rx,
    uint16_t device_id,
    uint32_t baud_rate
)
{
    XUartPs_Config *cfg;
    int status;

    if (rx == NULL) return XST_FAILURE;

    memset(rx, 0, sizeof(*rx));
    pose_uart_parser_init(&rx->parser);

    cfg = XUartPs_LookupConfig(device_id);
    if (cfg == NULL) {
        return XST_FAILURE;
    }

    status = XUartPs_CfgInitialize(
        &rx->uart,
        cfg,
        cfg->BaseAddress
    );
    if (status != XST_SUCCESS) {
        return status;
    }

    XUartPs_SetOperMode(&rx->uart, XUARTPS_OPER_MODE_NORMAL);

    status = XUartPs_SetBaudRate(&rx->uart, baud_rate);
    if (status != XST_SUCCESS) {
        return status;
    }

    /*
     * 임시 테스트에서는 interrupt 없이 polling.
     * 36 bytes x 20Hz = 720 bytes/s라 115200 baud에서 매우 여유롭다.
     */
    rx->pose_ready = 0U;
    return XST_SUCCESS;
}

void uart_pose_rx_poll(UartPoseReceiver *rx)
{
    HumanPose2D decoded;
    UINTPTR base;

    if (rx == NULL) return;

    base = rx->uart.Config.BaseAddress;

    while (XUartPs_IsReceiveData(base)) {
        uint8_t byte = (uint8_t)XUartPs_ReadReg(base, XUARTPS_FIFO_OFFSET);
        int ret = pose_uart_parser_push(&rx->parser, byte, &decoded);

        if (ret == 1) {
            if (rx->pose_ready) {
                /*
                 * Agent loop가 늦으면 오래된 frame보다 최신 frame이 낫다.
                 * 따라서 queue를 무한히 쌓지 않고 최신 frame으로 덮어쓴다.
                 */
                rx->overwritten_frames++;
            }

            rx->latest_pose = decoded;
            rx->pose_ready = 1U;
        }
    }
}

int uart_pose_rx_take_frame(
    UartPoseReceiver *rx,
    HumanPose2D *out_pose
)
{
    if (rx == NULL || out_pose == NULL) return 0;

    if (!rx->pose_ready) {
        return 0;
    }

    *out_pose = rx->latest_pose;
    rx->pose_ready = 0U;
    return 1;
}
