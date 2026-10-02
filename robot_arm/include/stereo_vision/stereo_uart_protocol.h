#ifndef STEREO_UART_PROTOCOL_H
#define STEREO_UART_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include "stereo_vision/stereo_pose.h"

#define STEREO_UART_JOINTS 17U
#define STEREO_UART_MARKERS 4U
#define STEREO_UART_PACKET_BYTES 170U

typedef struct {
    uint16_t x, y;
    int8_t score;
    uint8_t valid;
} StereoCoordinateJoint;

typedef struct {
    uint32_t session_id, sequence;
    uint32_t frame_id, result_seq, cycle_count, joint_flags;
    StereoFrameMetadata metadata;
    uint32_t markers[STEREO_UART_MARKERS];
    StereoCoordinateJoint joint[STEREO_UART_JOINTS];
} StereoCoordinateFrame;

typedef struct {
    uint8_t bytes[STEREO_UART_PACKET_BYTES];
    size_t used;
    uint32_t accepted, crc_errors, format_errors, range_errors;
} StereoUartParser;

uint32_t stereo_uart_crc32(const uint8_t *data, size_t length);
int stereo_uart_encode(const StereoCoordinateFrame *frame,
                      uint8_t packet[STEREO_UART_PACKET_BYTES]);
void stereo_uart_parser_init(StereoUartParser *parser);
int stereo_uart_parser_push(StereoUartParser *parser, uint8_t byte,
                            StereoCoordinateFrame *frame);

#endif
