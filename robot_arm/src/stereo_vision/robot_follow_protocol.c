#include "stereo_vision/robot_follow_protocol.h"

#include <float.h>
#include <math.h>
#include <string.h>

typedef char RobotFollowFloat32[(sizeof(float) == 4 && FLT_RADIX == 2 &&
    FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128) ? 1 : -1];

static void put32(uint8_t *destination, uint32_t value)
{
    unsigned index;
    for (index = 0; index < 4; ++index) destination[index] = (uint8_t)(value >> (8U * index));
}

static uint32_t get32(const uint8_t *source)
{
    unsigned index;
    uint32_t value = 0;
    for (index = 0; index < 4; ++index) value |= (uint32_t)source[index] << (8U * index);
    return value;
}

static void put_float(uint8_t *destination, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    put32(destination, bits);
}

static float get_float(const uint8_t *source)
{
    uint32_t bits = get32(source);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static size_t packet_size(unsigned type)
{
    switch (type) {
    case ROBOT_FOLLOW_REQUEST: return ROBOT_FOLLOW_REQUEST_BYTES;
    case ROBOT_FOLLOW_TARGET: return ROBOT_FOLLOW_TARGET_BYTES;
    case ROBOT_FOLLOW_GRIPPER: return ROBOT_FOLLOW_GRIPPER_BYTES;
    default: return 0;
    }
}

static int point_valid(Point2D point)
{
    return point.valid <= 1 && isfinite(point.x) && isfinite(point.y) &&
        (!point.valid || (point.x >= 0.0f && point.x < 1280.0f &&
                          point.y >= 0.0f && point.y < 720.0f));
}

static int packet_valid(const RobotFollowPacket *frame)
{
    const HumanForearmTarget *target;
    const HumanGripper2D *gripper;
    if (!frame->session) return 0;
    if (frame->type == ROBOT_FOLLOW_REQUEST) return frame->data.requested <= 1;
    if (!frame->coordinate_session) return 0;
    if (frame->type == ROBOT_FOLLOW_TARGET) {
        target = &frame->data.target.human;
        return isfinite(target->elbow_roll_deg) && isfinite(target->elbow_pitch_deg) &&
            isfinite(target->wrist_pitch_deg) && isfinite(target->wrist_roll_deg) &&
            isfinite(target->gripper_norm) && target->valid <= 1 &&
            target->elbow_roll_observable <= 1 && target->hand_fresh <= 1 &&
            target->wrist_valid <= 1 && target->gripper_valid <= 1 &&
            frame->data.target.stationary <= 1 && frame->data.target.wrist_fresh <= 1;
    }
    if (frame->type != ROBOT_FOLLOW_GRIPPER) return 0;
    gripper = &frame->data.gripper.gripper_2d;
    return frame->data.gripper.valid <= 1 && gripper->source >= 1 && gripper->source <= 2 &&
        point_valid(gripper->wrist) && point_valid(gripper->finger1) && point_valid(gripper->finger2) &&
        isfinite(gripper->reference_span_px) && gripper->reference_span_px >= 0.0f &&
        gripper->reference_span_px <= 1469.0f;
}

size_t robot_follow_encode(const RobotFollowPacket *frame, uint8_t *packet, size_t capacity)
{
    size_t length;
    const HumanForearmTarget *target;
    const HumanGripper2D *gripper;
    Point2D points[3];
    unsigned index;
    if (frame == NULL || packet == NULL) return 0;
    length = packet_size(frame->type);
    if (!length || capacity < length || !packet_valid(frame)) return 0;
    memset(packet, 0, length);
    memcpy(packet, "RFL1", 4);
    packet[4] = 1;
    packet[5] = (uint8_t)frame->type;
    packet[6] = (uint8_t)length;
    put32(packet + 8, frame->session);
    put32(packet + 12, frame->sequence);
    if (frame->type == ROBOT_FOLLOW_REQUEST) packet[16] = frame->data.requested;
    else {
        put32(packet + 16, frame->coordinate_session);
        put32(packet + 20, frame->coordinate_sequence);
        if (frame->type == ROBOT_FOLLOW_TARGET) {
            target = &frame->data.target.human;
            put32(packet + 24, target->frame_id);
            put32(packet + 28, frame->data.target.input_epoch);
            put_float(packet + 32, target->elbow_roll_deg);
            put_float(packet + 36, target->elbow_pitch_deg);
            put_float(packet + 40, target->wrist_pitch_deg);
            put_float(packet + 44, target->wrist_roll_deg);
            put_float(packet + 48, target->gripper_norm);
            packet[52] = (uint8_t)(target->valid | (target->elbow_roll_observable << 1) |
                (target->hand_fresh << 2) | (target->wrist_valid << 3) | (target->gripper_valid << 4));
            packet[53] = frame->data.target.stationary;
            packet[54] = frame->data.target.wrist_fresh;
        } else {
            gripper = &frame->data.gripper.gripper_2d;
            put32(packet + 24, frame->data.gripper.frame_id);
            packet[28] = frame->data.gripper.valid;
            packet[29] = gripper->source;
            points[0] = gripper->wrist;
            points[1] = gripper->finger1;
            points[2] = gripper->finger2;
            put_float(packet + 32, gripper->reference_span_px);
            for (index = 0; index < 3; ++index) {
                packet[30] |= (uint8_t)(points[index].valid << index);
                put_float(packet + 36 + 8U * index, points[index].x);
                put_float(packet + 40 + 8U * index, points[index].y);
            }
        }
    }
    put32(packet + length - 4, stereo_uart_crc32(packet, length - 4));
    return length;
}

static int decode(const uint8_t *packet, RobotFollowPacket *frame)
{
    HumanForearmTarget *target;
    HumanGripper2D *gripper;
    Point2D points[3];
    unsigned index;
    frame->type = (RobotFollowType)packet[5];
    frame->session = get32(packet + 8);
    frame->sequence = get32(packet + 12);
    if (frame->type == ROBOT_FOLLOW_REQUEST) {
        if (packet[17] || packet[18] || packet[19]) return -1;
        frame->data.requested = packet[16];
    } else {
        frame->coordinate_session = get32(packet + 16);
        frame->coordinate_sequence = get32(packet + 20);
        if (frame->type == ROBOT_FOLLOW_TARGET) {
            if ((packet[52] & ~31U) || packet[55]) return -1;
            target = &frame->data.target.human;
            target->frame_id = get32(packet + 24);
            frame->data.target.input_epoch = get32(packet + 28);
            target->elbow_roll_deg = get_float(packet + 32);
            target->elbow_pitch_deg = get_float(packet + 36);
            target->wrist_pitch_deg = get_float(packet + 40);
            target->wrist_roll_deg = get_float(packet + 44);
            target->gripper_norm = get_float(packet + 48);
            target->valid = packet[52] & 1U;
            target->elbow_roll_observable = (packet[52] >> 1) & 1U;
            target->hand_fresh = (packet[52] >> 2) & 1U;
            target->wrist_valid = (packet[52] >> 3) & 1U;
            target->gripper_valid = (packet[52] >> 4) & 1U;
            frame->data.target.stationary = packet[53];
            frame->data.target.wrist_fresh = packet[54];
        } else {
            if ((packet[30] & ~7U) || packet[31]) return -1;
            frame->data.gripper.frame_id = get32(packet + 24);
            frame->data.gripper.valid = packet[28];
            gripper = &frame->data.gripper.gripper_2d;
            gripper->source = packet[29];
            gripper->reference_span_px = get_float(packet + 32);
            for (index = 0; index < 3; ++index) {
                points[index].x = get_float(packet + 36 + 8U * index);
                points[index].y = get_float(packet + 40 + 8U * index);
                points[index].valid = (packet[30] >> index) & 1U;
            }
            gripper->wrist = points[0];
            gripper->finger1 = points[1];
            gripper->finger2 = points[2];
            frame->data.gripper.wrist = points[0];
            frame->data.gripper.finger1 = points[1];
            frame->data.gripper.finger2 = points[2];
        }
    }
    return packet_valid(frame);
}

void robot_follow_parser_init(RobotFollowParser *parser)
{
    if (parser != NULL) memset(parser, 0, sizeof(*parser));
}

static void discard_first(RobotFollowParser *parser)
{
    --parser->used;
    memmove(parser->bytes, parser->bytes + 1, parser->used);
    memmove(parser->times, parser->times + 1, parser->used * sizeof(parser->times[0]));
}

int robot_follow_parser_push(RobotFollowParser *parser, uint8_t byte,
                             uint64_t received_us, RobotFollowPacket *frame)
{
    RobotFollowPacket candidate;
    StereoUartParser coordinates;
    size_t length, index;
    int legacy, status;
    if (parser == NULL || frame == NULL) return 0;
    if (parser->used >= sizeof(parser->bytes)) parser->used = 0;
    parser->bytes[parser->used] = byte;
    parser->times[parser->used++] = received_us;
    while (parser->used >= 4) {
        legacy = memcmp(parser->bytes, "SCN1", 4) == 0;
        if (!legacy && memcmp(parser->bytes, "RFL1", 4) != 0) {
            discard_first(parser);
            continue;
        }
        if (parser->used < 8) return 0;
        length = legacy ? STEREO_UART_PACKET_BYTES : packet_size(parser->bytes[5]);
        if (!length || parser->bytes[4] != 1 ||
            (legacy && parser->bytes[5] != 1) ||
            parser->bytes[6] != length || parser->bytes[7]) {
            ++parser->format_errors;
            discard_first(parser);
            continue;
        }
        if (parser->used < length) return 0;
        if (get32(parser->bytes + length - 4) != stereo_uart_crc32(parser->bytes, length - 4)) {
            ++parser->crc_errors;
            discard_first(parser);
            continue;
        }
        memset(&candidate, 0, sizeof(candidate));
        if (legacy) {
            stereo_uart_parser_init(&coordinates);
            status = 0;
            for (index = 0; index < length; ++index)
                status = stereo_uart_parser_push(&coordinates, parser->bytes[index], &candidate.data.coordinates);
            parser->format_errors += coordinates.format_errors;
            parser->range_errors += coordinates.range_errors;
            candidate.type = ROBOT_FOLLOW_COORDINATES;
        } else {
            status = decode(parser->bytes, &candidate);
            if (status < 0) ++parser->format_errors;
            else if (!status) ++parser->range_errors;
        }
        if (status <= 0) {
            discard_first(parser);
            continue;
        }
        candidate.received_us = parser->times[0];
        *frame = candidate;
        parser->used -= length;
        memmove(parser->bytes, parser->bytes + length, parser->used);
        memmove(parser->times, parser->times + length, parser->used * sizeof(parser->times[0]));
        ++parser->accepted;
        return 1;
    }
    return 0;
}

int robot_follow_order_accept(RobotFollowOrder *order, uint32_t session, uint32_t sequence)
{
    unsigned index;
    uint32_t advance;
    if (order == NULL || !session) return 0;
    if (order->initialized) {
        if (order->session == session) {
            advance = sequence - order->sequence;
            if (!advance || advance >= 0x80000000U) return 0;
        } else {
            for (index = 0; index < order->retired_count; ++index)
                if (order->retired[index] == session) return 0;
            if (order->retired_count == ROBOT_FOLLOW_RETIRED_SESSIONS) return 0;
            order->retired[order->retired_count++] = order->session;
        }
    }
    order->session = session;
    order->sequence = sequence;
    order->initialized = 1;
    return 1;
}

int robot_follow_fresh(uint64_t now_us, uint64_t received_us)
{
    return now_us >= received_us && now_us - received_us < ROBOT_FOLLOW_MAX_AGE_US;
}
