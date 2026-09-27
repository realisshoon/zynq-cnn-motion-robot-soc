#include "uart_pose/uart_pose_protocol.h"

#include <stddef.h>
#include <string.h>

static uint16_t get_u16_le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] |
                      ((uint16_t)p[1] << 8));
}

static uint32_t get_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

uint16_t pose_uart_crc16_ccitt(
    const uint8_t *data,
    uint32_t length
)
{
    uint16_t crc = 0xFFFFU;
    uint32_t i;
    uint8_t bit;

    if (data == NULL) return 0U;

    for (i = 0U; i < length; ++i) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);

        for (bit = 0U; bit < 8U; ++bit) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            } else {
                crc <<= 1;
            }
        }
    }

    return crc;
}

void pose_uart_parser_init(PoseUartParser *parser)
{
    if (parser == NULL) return;

    memset(parser, 0, sizeof(*parser));
    parser->state = POSE_UART_WAIT_SYNC0;
}

static void parser_reset(PoseUartParser *parser)
{
    parser->index = 0U;
    parser->state = POSE_UART_WAIT_SYNC0;
}

static void decode_point(
    PoseUartParser *parser,
    Point2D *point,
    uint8_t pose_valid,
    uint8_t valid_mask,
    uint8_t valid_bit,
    uint16_t x,
    uint16_t y
)
{
    uint8_t in_range;

    point->x = (float)x;
    point->y = (float)y;

    in_range = (uint8_t)(
        (x < POSE_UART_IMAGE_WIDTH) &&
        (y < POSE_UART_IMAGE_HEIGHT)
    );

    if (!in_range) {
        parser->range_errors++;
    }

    point->valid = (uint8_t)(
        (pose_valid != 0U) &&
        ((valid_mask & valid_bit) != 0U) &&
        (in_range != 0U)
    );
}

static int decode_packet(
    PoseUartParser *parser,
    const uint8_t packet[POSE_UART_PACKET_SIZE],
    HumanPose2D *out_pose
)
{
    uint16_t rx_crc;
    uint16_t calc_crc;
    uint8_t pose_valid;
    uint8_t valid_mask;
    uint32_t off = 10U;

    if (packet[0] != POSE_UART_SYNC0 ||
        packet[1] != POSE_UART_SYNC1 ||
        packet[2] != POSE_UART_VERSION ||
        packet[3] != POSE_UART_PAYLOAD_SIZE) {

        parser->format_errors++;
        return -1;
    }

    rx_crc = get_u16_le(&packet[34]);
    calc_crc = pose_uart_crc16_ccitt(&packet[2], 32U);

    if (rx_crc != calc_crc) {
        parser->crc_errors++;
        return -1;
    }

    memset(out_pose, 0, sizeof(*out_pose));

    out_pose->frame_id = get_u32_le(&packet[4]);
    pose_valid = packet[8] ? 1U : 0U;
    valid_mask = packet[9];
    out_pose->valid = pose_valid;

#define DECODE_POINT(FIELD, MASKBIT) do {                      \
        uint16_t px = get_u16_le(&packet[off]);                \
        uint16_t py = get_u16_le(&packet[off + 2U]);           \
        decode_point(parser, &out_pose->FIELD,                 \
                     pose_valid, valid_mask, MASKBIT, px, py); \
        off += 4U;                                             \
    } while (0)

    DECODE_POINT(finger1,    POSE_UART_VALID_FINGER1);
    DECODE_POINT(finger2,    POSE_UART_VALID_FINGER2);
    DECODE_POINT(elbow,      POSE_UART_VALID_ELBOW);
    DECODE_POINT(wrist,      POSE_UART_VALID_WRIST);
    DECODE_POINT(shoulder_l, POSE_UART_VALID_SHOULDER_L);
    DECODE_POINT(shoulder_r, POSE_UART_VALID_SHOULDER_R);

#undef DECODE_POINT

    parser->packets_ok++;
    return 1;
}

int pose_uart_parser_push(
    PoseUartParser *parser,
    uint8_t byte,
    HumanPose2D *out_pose
)
{
    int ret;

    if (parser == NULL || out_pose == NULL) {
        return -1;
    }

    switch (parser->state) {
    case POSE_UART_WAIT_SYNC0:
        if (byte == POSE_UART_SYNC0) {
            parser->packet[0] = byte;
            parser->index = 1U;
            parser->state = POSE_UART_WAIT_SYNC1;
        }
        return 0;

    case POSE_UART_WAIT_SYNC1:
        if (byte == POSE_UART_SYNC1) {
            parser->packet[1] = byte;
            parser->index = 2U;
            parser->state = POSE_UART_COLLECT_PACKET;
        } else if (byte == POSE_UART_SYNC0) {
            parser->packet[0] = byte;
            parser->index = 1U;
        } else {
            parser_reset(parser);
        }
        return 0;

    case POSE_UART_COLLECT_PACKET:
        parser->packet[parser->index++] = byte;

        if (parser->index < POSE_UART_PACKET_SIZE) {
            return 0;
        }

        ret = decode_packet(parser, parser->packet, out_pose);
        parser_reset(parser);
        return ret;

    default:
        parser_reset(parser);
        return -1;
    }
}
