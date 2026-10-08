#ifndef ROBOT_FOLLOW_PROTOCOL_H
#define ROBOT_FOLLOW_PROTOCOL_H

#include "common/robot_types.h"
#include "stereo_vision/stereo_uart_protocol.h"

#define ROBOT_FOLLOW_MAX_AGE_US 250000U
#define ROBOT_FOLLOW_HEARTBEAT_US 75000U
#define ROBOT_FOLLOW_REQUEST_BYTES 24U
#define ROBOT_FOLLOW_TARGET_BYTES 60U
#define ROBOT_FOLLOW_GRIPPER_BYTES 64U
#define ROBOT_FOLLOW_RETIRED_SESSIONS 32U

typedef enum {
    ROBOT_FOLLOW_COORDINATES = 0,
    ROBOT_FOLLOW_REQUEST = 1,
    ROBOT_FOLLOW_TARGET = 2,
    ROBOT_FOLLOW_GRIPPER = 3
} RobotFollowType;

typedef struct {
    RobotFollowType type;
    uint32_t session, sequence;
    uint32_t coordinate_session, coordinate_sequence;
    uint64_t received_us;
    union {
        StereoCoordinateFrame coordinates;
        uint8_t requested;
        struct {
            HumanForearmTarget human;
            uint32_t input_epoch;
            uint8_t stationary, wrist_fresh;
        } target;
        HumanPose2D gripper;
    } data;
} RobotFollowPacket;

typedef struct {
    uint8_t bytes[STEREO_UART_PACKET_BYTES];
    uint64_t times[STEREO_UART_PACKET_BYTES];
    size_t used;
    uint32_t accepted, crc_errors, format_errors, range_errors;
} RobotFollowParser;

typedef struct {
    uint32_t session, sequence;
    uint32_t retired[ROBOT_FOLLOW_RETIRED_SESSIONS];
    unsigned retired_count;
    uint8_t initialized;
} RobotFollowOrder;

size_t robot_follow_encode(const RobotFollowPacket *frame, uint8_t *packet, size_t capacity);
void robot_follow_parser_init(RobotFollowParser *parser);
int robot_follow_parser_push(RobotFollowParser *parser, uint8_t byte,
                             uint64_t received_us, RobotFollowPacket *frame);
int robot_follow_order_accept(RobotFollowOrder *order, uint32_t session, uint32_t sequence);
int robot_follow_fresh(uint64_t now_us, uint64_t received_us);

#endif
