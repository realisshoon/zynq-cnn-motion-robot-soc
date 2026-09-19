#ifndef UART_POSE_RX_VITIS_H
#define UART_POSE_RX_VITIS_H

#include <stdint.h>

#include "xuartps.h"
#include "common/robot_types.h"
#include "uart_pose/uart_pose_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    XUartPs uart;
    PoseUartParser parser;

    HumanPose2D latest_pose;

    /*
     * 현재 구현은 polling이므로 이 flag 접근은 single-threaded이다.
     * 나중에 UART interrupt 방식으로 바꿀 경우 take 시 critical section을
     * 추가하거나 ring buffer를 사용하는 것이 좋다.
     */
    volatile uint8_t pose_ready;

    /* main이 아직 읽지 않았는데 새 frame이 오면 최신 frame으로 덮어씀 */
    uint32_t overwritten_frames;
} UartPoseReceiver;

/*
 * device_id: 보통 XPAR_XUARTPS_0_DEVICE_ID
 * baud_rate: 115200
 */
int uart_pose_rx_init(
    UartPoseReceiver *rx,
    uint16_t device_id,
    uint32_t baud_rate
);

/*
 * UART RX FIFO에 들어온 byte를 모두 parser에 넣는다.
 * 완전한 CRC-valid frame이 생기면 latest_pose 갱신 + pose_ready=1.
 *
 * Agent1/2/3은 이 함수 안에서 호출하지 않는다.
 */
void uart_pose_rx_poll(UartPoseReceiver *rx);

/*
 * 새 frame이 있으면 1을 반환하고 out_pose에 복사한 뒤 ready flag를 clear.
 * 없으면 0.
 */
int uart_pose_rx_take_frame(
    UartPoseReceiver *rx,
    HumanPose2D *out_pose
);

#ifdef __cplusplus
}
#endif

#endif /* UART_POSE_RX_VITIS_H */
