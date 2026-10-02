#include "stereo_vision/stereo_uart_protocol.h"

#include <string.h>

static const uint8_t magic[4] = {'S', 'C', 'N', '1'};

static void put16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *destination, uint32_t value)
{
    unsigned index;
    for (index = 0; index < 4; ++index) destination[index] = (uint8_t)(value >> (8U * index));
}

static uint16_t get16(const uint8_t *source)
{
    return (uint16_t)((uint16_t)source[0] | ((uint16_t)source[1] << 8));
}

static uint32_t get32(const uint8_t *source)
{
    unsigned index;
    uint32_t value = 0;
    for (index = 0; index < 4; ++index) value |= (uint32_t)source[index] << (8U * index);
    return value;
}

uint32_t stereo_uart_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = UINT32_MAX;
    size_t index;
    unsigned bit;
    for (index = 0; index < length; ++index) {
        crc ^= data[index];
        for (bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
    }
    return ~crc;
}

static int frame_valid(const StereoCoordinateFrame *frame)
{
    unsigned index;
    if (frame->session_id == 0 || (frame->joint_flags & ~0x1FFFFU) ||
        frame->metadata.exposure_time_verified > 1 ||
        frame->metadata.fixed_geometry_verified > 1 ||
        frame->metadata.localization_quality_verified > 1 ||
        (frame->metadata.exposure_time_verified && frame->metadata.shared_clock_epoch == 0)) return 0;
    for (index = 0; index < STEREO_UART_JOINTS; ++index)
        if (frame->joint[index].valid > 1 || (frame->joint[index].valid &&
            (frame->joint[index].x >= 1280 || frame->joint[index].y >= 720))) return 0;
    for (index = 0; index < STEREO_UART_MARKERS; ++index)
        if ((frame->markers[index] & 0x80000000U) &&
            ((frame->markers[index] & 0x7FFU) >= 1280 ||
             ((frame->markers[index] >> 11) & 0x3FFU) >= 720)) return 0;
    return 1;
}

int stereo_uart_encode(const StereoCoordinateFrame *frame,
                      uint8_t packet[STEREO_UART_PACKET_BYTES])
{
    unsigned index;
    if (frame == NULL || packet == NULL || !frame_valid(frame)) return 0;
    memset(packet, 0, STEREO_UART_PACKET_BYTES);
    memcpy(packet, magic, sizeof(magic));
    packet[4] = 1;
    packet[5] = 1;
    put16(packet + 6, STEREO_UART_PACKET_BYTES);
    put32(packet + 8, frame->session_id);
    put32(packet + 12, frame->sequence);
    put32(packet + 16, frame->frame_id);
    put32(packet + 20, frame->result_seq);
    put32(packet + 24, frame->cycle_count);
    put32(packet + 28, frame->joint_flags);
    put32(packet + 32, (uint32_t)frame->metadata.exposure_time_us);
    put32(packet + 36, (uint32_t)(frame->metadata.exposure_time_us >> 32));
    put32(packet + 40, frame->metadata.shared_clock_epoch);
    packet[44] = (uint8_t)(frame->metadata.exposure_time_verified |
        (frame->metadata.fixed_geometry_verified << 1) |
        (frame->metadata.localization_quality_verified << 2));
    for (index = 0; index < STEREO_UART_MARKERS; ++index)
        put32(packet + 48 + 4U * index, frame->markers[index]);
    for (index = 0; index < STEREO_UART_JOINTS; ++index) {
        uint8_t *point = packet + 64 + 6U * index;
        put16(point, frame->joint[index].x);
        put16(point + 2, frame->joint[index].y);
        point[4] = (uint8_t)frame->joint[index].score;
        point[5] = frame->joint[index].valid;
    }
    put32(packet + 166, stereo_uart_crc32(packet, 166));
    return 1;
}

void stereo_uart_parser_init(StereoUartParser *parser)
{
    if (parser != NULL) memset(parser, 0, sizeof(*parser));
}

static void discard_first(StereoUartParser *parser)
{
    --parser->used;
    memmove(parser->bytes, parser->bytes + 1, parser->used);
}

int stereo_uart_parser_push(StereoUartParser *parser, uint8_t byte,
                            StereoCoordinateFrame *frame)
{
    const uint8_t *packet;
    StereoCoordinateFrame candidate;
    unsigned index;
    if (parser == NULL || frame == NULL) return 0;
    parser->bytes[parser->used++] = byte;
    while (parser->used >= 4 && memcmp(parser->bytes, magic, 4) != 0) discard_first(parser);
    packet = parser->bytes;
    if (parser->used >= 8 && (packet[4] != 1 || packet[5] != 1 ||
        get16(packet + 6) != STEREO_UART_PACKET_BYTES)) {
        ++parser->format_errors;
        discard_first(parser);
        return 0;
    }
    if (parser->used != STEREO_UART_PACKET_BYTES) return 0;
    if (get32(packet + 166) != stereo_uart_crc32(packet, 166)) {
        ++parser->crc_errors;
        discard_first(parser);
        return 0;
    }
    if ((packet[44] & ~7U) || packet[45] || packet[46] || packet[47]) {
        ++parser->format_errors;
        discard_first(parser);
        return 0;
    }
    memset(&candidate, 0, sizeof(candidate));
    candidate.session_id = get32(packet + 8);
    candidate.sequence = get32(packet + 12);
    candidate.frame_id = get32(packet + 16);
    candidate.result_seq = get32(packet + 20);
    candidate.cycle_count = get32(packet + 24);
    candidate.joint_flags = get32(packet + 28);
    candidate.metadata.exposure_time_us = get32(packet + 32) | ((uint64_t)get32(packet + 36) << 32);
    candidate.metadata.shared_clock_epoch = get32(packet + 40);
    candidate.metadata.exposure_time_verified = packet[44] & 1U;
    candidate.metadata.fixed_geometry_verified = (packet[44] >> 1) & 1U;
    candidate.metadata.localization_quality_verified = (packet[44] >> 2) & 1U;
    for (index = 0; index < STEREO_UART_MARKERS; ++index)
        candidate.markers[index] = get32(packet + 48 + 4U * index);
    for (index = 0; index < STEREO_UART_JOINTS; ++index) {
        const uint8_t *point = packet + 64 + 6U * index;
        candidate.joint[index].x = get16(point);
        candidate.joint[index].y = get16(point + 2);
        candidate.joint[index].score = (int8_t)(point[4] < 128 ? point[4] : (int)point[4] - 256);
        candidate.joint[index].valid = point[5];
    }
    if (!frame_valid(&candidate)) {
        ++parser->range_errors;
        discard_first(parser);
        return 0;
    }
    *frame = candidate;
    parser->used = 0;
    ++parser->accepted;
    return 1;
}
