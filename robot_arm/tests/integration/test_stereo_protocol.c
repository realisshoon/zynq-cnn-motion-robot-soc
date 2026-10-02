#include "stereo_vision/stereo_uart_protocol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void repair_crc(uint8_t *packet)
{
    uint32_t crc = stereo_uart_crc32(packet, 166);
    unsigned index;
    for (index = 0; index < 4; ++index) packet[166 + index] = (uint8_t)(crc >> (8U * index));
}

static int feed(StereoUartParser *parser, const uint8_t *packet, size_t length,
                StereoCoordinateFrame *output)
{
    size_t index;
    int accepted = 0;
    for (index = 0; index < length; ++index) accepted += stereo_uart_parser_push(parser, packet[index], output);
    return accepted;
}

int main(void)
{
    StereoCoordinateFrame frame, output;
    StereoUartParser parser;
    uint8_t packet[STEREO_UART_PACKET_BYTES], damaged[STEREO_UART_PACKET_BYTES];
    unsigned index;
    memset(&frame, 0, sizeof(frame));
    frame.session_id = 0x12345678;
    frame.sequence = UINT32_MAX;
    frame.frame_id = 987;
    frame.result_seq = 111;
    frame.cycle_count = 456;
    frame.joint_flags = 0x1FFFF;
    frame.metadata = (StereoFrameMetadata){UINT64_C(0x1122334455667788), 42, 1, 1, 1};
    for (index = 0; index < STEREO_UART_JOINTS; ++index)
        frame.joint[index] = (StereoCoordinateJoint){(uint16_t)(100 + index), (uint16_t)(200 + index), -128, 1};
    for (index = 0; index < STEREO_UART_MARKERS; ++index)
        frame.markers[index] = 0x80000000U | (300U << 11) | (600U + index);
    assert(stereo_uart_crc32((const uint8_t *)"123456789", 9) == 0xCBF43926U);
    assert(stereo_uart_encode(&frame, packet));
    assert(packet[8] == 0x78 && packet[11] == 0x12 && packet[64 + 4] == 0x80);
    stereo_uart_parser_init(&parser);
    assert(feed(&parser, (const uint8_t *)"noiseSC", 7, &output) == 0);
    assert(feed(&parser, packet, sizeof(packet) - 1, &output) == 0);
    assert(stereo_uart_parser_push(&parser, packet[sizeof(packet) - 1], &output));
    assert(output.session_id == frame.session_id && output.sequence == frame.sequence);
    assert(output.frame_id == frame.frame_id && output.result_seq == frame.result_seq);
    assert(output.metadata.exposure_time_us == frame.metadata.exposure_time_us);
    assert(output.metadata.shared_clock_epoch == 42 && output.metadata.localization_quality_verified);
    for (index = 0; index < STEREO_UART_JOINTS; ++index) {
        assert(output.joint[index].x == frame.joint[index].x);
        assert(output.joint[index].y == frame.joint[index].y && output.joint[index].score == -128);
    }
    for (index = 0; index < STEREO_UART_MARKERS; ++index) assert(output.markers[index] == frame.markers[index]);
    memcpy(damaged, packet, sizeof(packet));
    damaged[100] ^= 1;
    assert(!feed(&parser, damaged, sizeof(damaged), &output));
    assert(parser.crc_errors == 1);
    assert(feed(&parser, packet, sizeof(packet), &output) == 1);
    assert(!feed(&parser, packet, 88, &output));
    assert(feed(&parser, packet, sizeof(packet), &output) == 1);
    memcpy(damaged, packet, sizeof(packet));
    damaged[4] = 2;
    assert(!feed(&parser, damaged, sizeof(damaged), &output));
    assert(parser.format_errors);
    assert(feed(&parser, packet, sizeof(packet), &output) == 1);
    memcpy(damaged, packet, sizeof(packet));
    damaged[64] = 0;
    damaged[65] = 5;
    repair_crc(damaged);
    assert(!feed(&parser, damaged, sizeof(damaged), &output));
    assert(parser.range_errors == 1);
    assert(feed(&parser, packet, sizeof(packet), &output) == 1);
    memcpy(damaged, packet, sizeof(packet));
    damaged[45] = 1;
    repair_crc(damaged);
    assert(!feed(&parser, damaged, sizeof(damaged), &output));
    assert(feed(&parser, packet, sizeof(packet), &output) == 1);
    frame.joint[0].valid = 2;
    assert(!stereo_uart_encode(&frame, packet));
    frame.joint[0].valid = 1;
    frame.metadata.shared_clock_epoch = 0;
    assert(!stereo_uart_encode(&frame, packet));
    puts("test_stereo_protocol: PASS");
    return 0;
}
