#include "stereo_vision/robot_follow_protocol.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void put32(uint8_t *destination, uint32_t value)
{
    unsigned index;
    for (index = 0; index < 4; ++index) destination[index] = (uint8_t)(value >> (8U * index));
}

static void repair_crc(uint8_t *packet, size_t length)
{
    put32(packet + length - 4, stereo_uart_crc32(packet, length - 4));
}

static int feed(RobotFollowParser *parser, const uint8_t *bytes, size_t length,
                uint64_t received, RobotFollowPacket *output)
{
    size_t index;
    int count = 0;
    for (index = 0; index < length; ++index)
        count += robot_follow_parser_push(parser, bytes[index], received + index, output);
    return count;
}

static RobotFollowPacket target_packet(void)
{
    RobotFollowPacket packet;
    memset(&packet, 0, sizeof(packet));
    packet.type = ROBOT_FOLLOW_TARGET;
    packet.session = 91;
    packet.sequence = UINT32_MAX;
    packet.coordinate_session = 77;
    packet.coordinate_sequence = 12;
    packet.data.target.human = (HumanForearmTarget){-123.5f, 46.25f, -67.5f, 123.0f, 0.75f,
        0xABCDEF12U, 1, 0, 0, 1, 1};
    packet.data.target.input_epoch = UINT32_MAX;
    packet.data.target.stationary = 1;
    packet.data.target.wrist_fresh = 1;
    return packet;
}

static void test_roundtrip(void)
{
    RobotFollowPacket packet = target_packet(), output;
    RobotFollowParser parser;
    uint8_t bytes[STEREO_UART_PACKET_BYTES];
    unsigned flags;
    size_t length;
    robot_follow_parser_init(&parser);
    assert(stereo_uart_crc32((const uint8_t *)"123456789", 9) == 0xCBF43926U);
    for (flags = 0; flags < 32; ++flags) {
        HumanForearmTarget *target = &packet.data.target.human;
        target->valid = flags & 1U;
        target->elbow_roll_observable = (flags >> 1) & 1U;
        target->hand_fresh = (flags >> 2) & 1U;
        target->wrist_valid = (flags >> 3) & 1U;
        target->gripper_valid = (flags >> 4) & 1U;
        length = robot_follow_encode(&packet, bytes, sizeof(bytes));
        assert(length == ROBOT_FOLLOW_TARGET_BYTES && bytes[52] == flags);
        assert(feed(&parser, bytes, length, 1000, &output) == 1);
        assert(output.received_us == 1000 && output.session == 91 && output.sequence == UINT32_MAX);
        assert(output.coordinate_session == 77 && output.coordinate_sequence == 12);
        assert(output.data.target.human.frame_id == 0xABCDEF12U);
        assert(output.data.target.human.elbow_roll_deg == -123.5f);
        assert(output.data.target.human.elbow_pitch_deg == 46.25f);
        assert(output.data.target.human.wrist_pitch_deg == -67.5f);
        assert(output.data.target.human.wrist_roll_deg == 123.0f);
        assert(output.data.target.human.gripper_norm == 0.75f);
        assert(output.data.target.human.valid == target->valid);
        assert(output.data.target.human.elbow_roll_observable == target->elbow_roll_observable);
        assert(output.data.target.human.hand_fresh == target->hand_fresh);
        assert(output.data.target.human.wrist_valid == target->wrist_valid);
        assert(output.data.target.human.gripper_valid == target->gripper_valid);
        assert(output.data.target.stationary == 1 && output.data.target.wrist_fresh == 1);
        assert(output.data.target.input_epoch == UINT32_MAX);
    }
    packet.type = ROBOT_FOLLOW_GRIPPER;
    memset(&packet.data, 0, sizeof(packet.data));
    packet.data.gripper.frame_id = 55;
    packet.data.gripper.valid = 0;
    packet.data.gripper.gripper_2d = (HumanGripper2D){{100.25f, 200.5f, 1},
        {110.5f, 190.25f, 1}, {125.75f, 192.5f, 0}, 2, 155.75f};
    length = robot_follow_encode(&packet, bytes, sizeof(bytes));
    assert(length == ROBOT_FOLLOW_GRIPPER_BYTES);
    memset(&output, 0x55, sizeof(output));
    assert(feed(&parser, bytes, length, 2000, &output) == 1);
    assert(output.data.gripper.frame_id == 55 && !output.data.gripper.valid);
    assert(output.data.gripper.gripper_2d.source == 2);
    assert(output.data.gripper.gripper_2d.reference_span_px == 155.75f);
    assert(output.data.gripper.gripper_2d.wrist.x == 100.25f);
    assert(output.data.gripper.gripper_2d.finger1.y == 190.25f);
    assert(output.data.gripper.gripper_2d.finger2.x == 125.75f);
    assert(!output.data.gripper.gripper_2d.finger2.valid);
    assert(!output.data.gripper.elbow.valid && !output.data.gripper.shoulder_l.valid);
    packet.type = ROBOT_FOLLOW_REQUEST;
    packet.data.requested = 0;
    length = robot_follow_encode(&packet, bytes, sizeof(bytes));
    assert(length == ROBOT_FOLLOW_REQUEST_BYTES);
    assert(feed(&parser, bytes, length, 3000, &output) == 1 && !output.data.requested);
    packet.data.requested = 1;
    assert(robot_follow_encode(&packet, bytes, sizeof(bytes)) == length);
    assert(feed(&parser, bytes, length, 3000, &output) == 1 && output.data.requested);
    assert(!robot_follow_encode(NULL, bytes, sizeof(bytes)));
    assert(!robot_follow_encode(&packet, NULL, sizeof(bytes)));
    assert(!robot_follow_encode(&packet, bytes, length - 1));
}

static void test_malformed(void)
{
    RobotFollowPacket packet = target_packet(), output, sentinel;
    RobotFollowParser parser;
    uint8_t bytes[STEREO_UART_PACKET_BYTES], damaged[STEREO_UART_PACKET_BYTES];
    const unsigned corrupt_offsets[] = {4, 5, 6, 7, 52, 55};
    const unsigned float_offsets[] = {32, 36, 40, 44, 48};
    unsigned index, pattern;
    size_t length = robot_follow_encode(&packet, bytes, sizeof(bytes));
    memset(&sentinel, 0x5A, sizeof(sentinel));
    robot_follow_parser_init(&parser);
    for (index = 0; index < sizeof(corrupt_offsets) / sizeof(corrupt_offsets[0]); ++index) {
        memcpy(damaged, bytes, length);
        damaged[corrupt_offsets[index]] = 0xFF;
        repair_crc(damaged, length);
        output = sentinel;
        assert(!feed(&parser, damaged, length, 0, &output));
        assert(memcmp(&output, &sentinel, sizeof(output)) == 0);
        assert(feed(&parser, bytes, length, 0, &output) == 1);
    }
    for (index = 0; index < length; ++index) {
        memcpy(damaged, bytes, length);
        damaged[index] ^= 0x80;
        assert(!feed(&parser, damaged, length, 0, &output));
        assert(feed(&parser, bytes, length, 0, &output) == 1);
    }
    for (index = 0; index < sizeof(float_offsets) / sizeof(float_offsets[0]); ++index)
        for (pattern = 0; pattern < 3; ++pattern) {
            const uint32_t nonfinite[] = {0x7FC01234U, 0x7F800000U, 0xFF800000U};
            memcpy(damaged, bytes, length);
            put32(damaged + float_offsets[index], nonfinite[pattern]);
            repair_crc(damaged, length);
            assert(!feed(&parser, damaged, length, 0, &output));
            assert(feed(&parser, bytes, length, 0, &output) == 1);
        }
    memcpy(damaged, bytes, length);
    put32(damaged + 8, 0);
    repair_crc(damaged, length);
    assert(!feed(&parser, damaged, length, 0, &output));
    assert(feed(&parser, bytes, length, 0, &output) == 1);
    packet.data.target.human.gripper_norm = NAN;
    assert(!robot_follow_encode(&packet, bytes, sizeof(bytes)));
    packet = target_packet();
    packet.data.target.wrist_fresh = 2;
    assert(!robot_follow_encode(&packet, bytes, sizeof(bytes)));
    packet = target_packet();
    packet.type = ROBOT_FOLLOW_GRIPPER;
    memset(&packet.data, 0, sizeof(packet.data));
    packet.data.gripper.gripper_2d.source = 2;
    packet.data.gripper.gripper_2d.wrist = (Point2D){1280, 10, 1};
    assert(!robot_follow_encode(&packet, bytes, sizeof(bytes)));
    packet.data.gripper.gripper_2d.wrist.x = 100;
    packet.data.gripper.gripper_2d.reference_span_px = NAN;
    assert(!robot_follow_encode(&packet, bytes, sizeof(bytes)));
    assert(parser.crc_errors && parser.format_errors && parser.range_errors);
}

static void test_multiplex_and_atomic_frames(void)
{
    StereoCoordinateFrame coordinates;
    RobotFollowPacket packet = target_packet(), output;
    RobotFollowPacket embedded;
    RobotFollowParser parser;
    uint8_t bytes[STEREO_UART_PACKET_BYTES], legacy[STEREO_UART_PACKET_BYTES];
    size_t length = robot_follow_encode(&packet, bytes, sizeof(bytes)), index;
    memset(&coordinates, 0, sizeof(coordinates));
    coordinates.session_id = 77;
    coordinates.sequence = 12;
    coordinates.frame_id = 55;
    assert(stereo_uart_encode(&coordinates, legacy));
    memset(&embedded, 0, sizeof(embedded));
    embedded.type = ROBOT_FOLLOW_REQUEST;
    embedded.session = 91;
    embedded.sequence = 1;
    embedded.data.requested = 1;
    assert(robot_follow_encode(&embedded, legacy + 16, sizeof(legacy) - 16) == ROBOT_FOLLOW_REQUEST_BYTES);
    repair_crc(legacy, sizeof(legacy));
    robot_follow_parser_init(&parser);
    assert(feed(&parser, (const uint8_t *)"noiseRFL", 8, 100, &output) == 0);
    assert(feed(&parser, legacy, sizeof(legacy), 200, &output) == 1);
    assert(output.type == ROBOT_FOLLOW_COORDINATES);
    assert(output.data.coordinates.frame_id == 0x314C4652U);
    assert(feed(&parser, bytes, length, 500, &output) == 1 && output.type == ROBOT_FOLLOW_TARGET);
    for (index = 0; index < length; ++index) {
        robot_follow_parser_init(&parser);
        assert(!feed(&parser, bytes, index, 100, &output));
        assert(feed(&parser, bytes + index, length - index, 100 + index, &output) == 1);
        assert(output.received_us == 100 && output.data.target.human.frame_id == 0xABCDEF12U);
    }
    robot_follow_parser_init(&parser);
    assert(!feed(&parser, bytes, 31, 100, &output));
    assert(feed(&parser, bytes, length, 500, &output) == 1);
    assert(output.received_us == 500);
    assert(!feed(&parser, bytes, 20, 100, &output));
    assert(feed(&parser, bytes + 20, length - 20, 300000, &output) == 1);
    assert(output.received_us == 100 && !robot_follow_fresh(300100, output.received_us));
}

static void test_gripper_wire_validation(void)
{
    RobotFollowPacket packet = target_packet(), output;
    RobotFollowParser parser;
    uint8_t bytes[ROBOT_FOLLOW_GRIPPER_BYTES], damaged[ROBOT_FOLLOW_GRIPPER_BYTES];
    const unsigned float_offsets[] = {32, 36, 40, 44, 48, 52, 56};
    const unsigned flag_offsets[] = {28, 29, 30, 31};
    unsigned index;
    packet.type = ROBOT_FOLLOW_GRIPPER;
    memset(&packet.data, 0, sizeof(packet.data));
    packet.data.gripper.gripper_2d = (HumanGripper2D){{100, 200, 1},
        {110, 190, 1}, {130, 195, 1}, 1, 120};
    assert(robot_follow_encode(&packet, bytes, sizeof(bytes)) == sizeof(bytes));
    robot_follow_parser_init(&parser);
    for (index = 0; index < sizeof(float_offsets) / sizeof(float_offsets[0]); ++index) {
        memcpy(damaged, bytes, sizeof(bytes));
        put32(damaged + float_offsets[index], 0x7FC01234U);
        repair_crc(damaged, sizeof(damaged));
        assert(!feed(&parser, damaged, sizeof(damaged), 0, &output));
        assert(feed(&parser, bytes, sizeof(bytes), 0, &output) == 1);
    }
    for (index = 0; index < sizeof(flag_offsets) / sizeof(flag_offsets[0]); ++index) {
        memcpy(damaged, bytes, sizeof(bytes));
        damaged[flag_offsets[index]] = 0xFF;
        repair_crc(damaged, sizeof(damaged));
        assert(!feed(&parser, damaged, sizeof(damaged), 0, &output));
        assert(feed(&parser, bytes, sizeof(bytes), 0, &output) == 1);
    }
    memcpy(damaged, bytes, sizeof(bytes));
    put32(damaged + 16, 0);
    repair_crc(damaged, sizeof(damaged));
    assert(!feed(&parser, damaged, sizeof(damaged), 0, &output));
    assert(feed(&parser, bytes, sizeof(bytes), 0, &output) == 1);
    assert(parser.range_errors && parser.format_errors);
}

static void test_order_and_age(void)
{
    RobotFollowOrder order = {0};
    unsigned index;
    assert(!robot_follow_order_accept(&order, 0, 1));
    assert(robot_follow_order_accept(&order, 10, UINT32_MAX - 1U));
    assert(robot_follow_order_accept(&order, 10, UINT32_MAX));
    assert(robot_follow_order_accept(&order, 10, 0));
    assert(!robot_follow_order_accept(&order, 10, UINT32_MAX));
    assert(!robot_follow_order_accept(&order, 10, 0));
    assert(!robot_follow_order_accept(&order, 10, 0x80000000U));
    assert(robot_follow_order_accept(&order, 10, 1));
    assert(robot_follow_order_accept(&order, 5, 0));
    assert(!robot_follow_order_accept(&order, 10, 999));
    assert(robot_follow_order_accept(&order, 5, 1));
    assert(robot_follow_order_accept(&order, UINT32_MAX, 0));
    assert(!robot_follow_order_accept(&order, 5, 99));
    for (index = 0; index < ROBOT_FOLLOW_RETIRED_SESSIONS - 2U; ++index)
        assert(robot_follow_order_accept(&order, 100 + index, 0));
    assert(!robot_follow_order_accept(&order, 200, 0));
    assert(robot_follow_order_accept(&order, order.session, 1));
    assert(robot_follow_fresh(100, 100));
    assert(robot_follow_fresh(250099, 100));
    assert(!robot_follow_fresh(250100, 100));
    assert(!robot_follow_fresh(99, 100));
    assert(robot_follow_fresh(UINT64_MAX, UINT64_MAX - 249999U));
}

int main(void)
{
    test_roundtrip();
    test_malformed();
    test_multiplex_and_atomic_frames();
    test_gripper_wire_validation();
    test_order_and_age();
    puts("test_robot_follow_protocol: PASS");
    return 0;
}
