#ifndef UART_POSE_PROTOCOL_H
#define UART_POSE_PROTOCOL_H

#include <stdint.h>
#include "common/robot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * PC -> Zybo UART wire protocol
 *
 * 이 파일은 Xilinx/XSA/Vitis에 의존하지 않는다.
 * 따라서 PC GCC 테스트와 Zybo Vitis 코드가 같은 parser를 공유할 수 있다.
 *
 * Packet size: 36 bytes
 *
 * [0]      0xA5
 * [1]      0x5A
 * [2]      version = 1
 * [3]      payload size = 30
 * [4..7]   frame_id, uint32 little-endian
 * [8]      pose_valid
 * [9]      point valid mask
 * [10..33] six (x,y) uint16 little-endian pairs
 *           finger1, finger2, elbow, wrist, shoulder_l, shoulder_r
 * [34..35] CRC16-CCITT, little-endian
 *
 * CRC coverage: packet[2] ~ packet[33]
 */

#define POSE_UART_SYNC0              0xA5U
#define POSE_UART_SYNC1              0x5AU
#define POSE_UART_VERSION            0x01U

#define POSE_UART_IMAGE_WIDTH        1280U
#define POSE_UART_IMAGE_HEIGHT       720U

#define POSE_UART_PAYLOAD_SIZE       30U
#define POSE_UART_PACKET_SIZE        36U

#define POSE_UART_VALID_FINGER1      (1U << 0)
#define POSE_UART_VALID_FINGER2      (1U << 1)
#define POSE_UART_VALID_ELBOW        (1U << 2)
#define POSE_UART_VALID_WRIST        (1U << 3)
#define POSE_UART_VALID_SHOULDER_L   (1U << 4)
#define POSE_UART_VALID_SHOULDER_R   (1U << 5)

typedef enum {
    POSE_UART_WAIT_SYNC0 = 0,
    POSE_UART_WAIT_SYNC1,
    POSE_UART_COLLECT_PACKET
} PoseUartParserState;

typedef struct {
    uint8_t packet[POSE_UART_PACKET_SIZE];
    uint16_t index;
    PoseUartParserState state;

    uint32_t packets_ok;
    uint32_t crc_errors;
    uint32_t format_errors;
    uint32_t range_errors;
} PoseUartParser;

void pose_uart_parser_init(PoseUartParser *parser);

uint16_t pose_uart_crc16_ccitt(
    const uint8_t *data,
    uint32_t length
);

/*
 * UART에서 byte 하나 들어올 때마다 호출한다고 생각하면 된다.
 *
 * return
 *   1 : HumanPose2D 1 frame 완성
 *   0 : 아직 packet 수신 중
 *  -1 : packet 하나를 받았지만 CRC/format 오류
 */
int pose_uart_parser_push(
    PoseUartParser *parser,
    uint8_t byte,
    HumanPose2D *out_pose
);

#ifdef __cplusplus
}
#endif

#endif
