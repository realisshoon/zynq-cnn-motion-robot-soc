#include "integration/stereo_board.h"
#include "integration/platform_vitis.h"
#include "stereo_vision/robot_follow_protocol.h"
#include "xuartps.h"
#include "xuartps_hw.h"
#include "xtime_l.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if defined(ROBOT_STEREO_LEFT) || defined(ROBOT_STEREO_RIGHT)

static XScuGic fake_gic;
static XUartPs_Config configs[2] = {{0, 0xE0000000U}, {1, 0xE0001000U}};
static Xil_InterruptHandler irq_handler;
static void *irq_reference;
static uint8_t rx_bytes[10000], tx_bytes[100000];
static unsigned rx_count, rx_read, tx_count, tx_budget, read_tx;
static uint32_t irq_status;
static XTime now = 1000000;
static unsigned gate_transitions;
static int gate_enabled;
#ifdef ROBOT_STEREO_RIGHT
static int accept_next = 1;
static StereoDepthResult published;
static unsigned publication_count;
#endif

void XTime_GetTime(XTime *time) { *time = now; }
int xil_printf(const char *format, ...) { (void)format; return 0; }
XScuGic *platform_vitis_gic(void) { return &fake_gic; }
XUartPs_Config *XUartPs_LookupConfig(u16 device) { return device < 2 ? &configs[device] : NULL; }
int XUartPs_CfgInitialize(XUartPs *uart, XUartPs_Config *config, UINTPTR address)
{
    assert(address == 0xE0000000U);
    uart->Config = *config;
    return 0;
}
int XUartPs_SetBaudRate(XUartPs *uart, u32 baud) { (void)uart; assert(baud == 115200); return 0; }
int XUartPs_SetDataFormat(XUartPs *uart, XUartPsFormat *format)
{
    assert(format->DataBits == 0 && format->Parity == 4 && format->StopBits == 0);
    return XUartPs_SetBaudRate(uart, format->BaudRate);
}
void XUartPs_SetInterruptMask(XUartPs *uart, u32 mask) { (void)uart; (void)mask; }
void XUartPs_SetOperMode(XUartPs *uart, u8 mode) { (void)uart; assert(mode == 0); }
void XUartPs_SetFifoThreshold(XUartPs *uart, u8 threshold) { (void)uart; assert(threshold == 16); }
void XUartPs_SetRecvTimeout(XUartPs *uart, u8 timeout) { (void)uart; assert(timeout == 8); }
int XScuGic_Connect(XScuGic *gic, u32 interrupt, Xil_InterruptHandler handler, void *reference)
{
    assert(gic == &fake_gic && interrupt == 59);
    irq_handler = handler;
    irq_reference = reference;
    return 0;
}
void XScuGic_SetPriorityTriggerType(XScuGic *gic, u32 interrupt, u8 priority, u8 trigger)
{
    assert(gic == &fake_gic && interrupt == 59 && priority == 0xA0 && trigger == 1);
}
void XScuGic_Enable(XScuGic *gic, u32 interrupt) { assert(gic == &fake_gic && interrupt == 59); }
u32 XUartPs_ReadReg(UINTPTR base, u32 offset)
{
    assert(base == 0xE0000000U);
    if (offset == XUARTPS_ISR_OFFSET) return irq_status;
    if (offset == XUARTPS_CR_OFFSET) return 0;
    assert(offset == XUARTPS_FIFO_OFFSET && rx_read < rx_count);
    return rx_bytes[rx_read++];
}
void XUartPs_WriteReg(UINTPTR base, u32 offset, u32 value)
{
    assert(base == 0xE0000000U);
    if (offset != XUARTPS_FIFO_OFFSET) return;
    assert(tx_budget && tx_count < sizeof(tx_bytes));
    --tx_budget;
    tx_bytes[tx_count++] = (uint8_t)value;
}
int XUartPs_IsReceiveData(UINTPTR base) { assert(base == 0xE0000000U); return rx_read < rx_count; }
int XUartPs_IsTransmitFull(UINTPTR base) { assert(base == 0xE0000000U); return tx_budget == 0; }
void input_pose_cnn_set_stereo(int enabled) { assert(enabled); }
void input_pose_cnn_set_async_test(int enabled) { gate_enabled = enabled; ++gate_transitions; }
void input_pose_cnn_discard_stereo_pending(void) { }
void input_pose_cnn_discard_stereo_arm_pending(void) { }
int input_pose_cnn_publish_stereo(const StereoDepthResult *depth)
{
#ifdef ROBOT_STEREO_RIGHT
    published = *depth;
    ++publication_count;
    return accept_next;
#else
    (void)depth;
    return 0;
#endif
}
const char *input_pose_cnn_admission_reason(void) { return "TEST"; }
const char *input_pose_cnn_tracking_state(void) { return "TEST"; }
unsigned input_pose_cnn_reacquire_count(void) { return 0; }
uint32_t input_pose_cnn_filter_epoch(void) { return gate_transitions; }

static void inject(const uint8_t *bytes, unsigned length)
{
    assert(length <= sizeof(rx_bytes));
    memcpy(rx_bytes, bytes, length);
    rx_count = length;
    rx_read = 0;
    irq_status = XUARTPS_IXR_RXOVR | XUARTPS_IXR_TOUT;
    while (rx_read < rx_count) irq_handler(irq_reference);
}

static void deliver(RobotFollowPacket *packet)
{
    uint8_t bytes[STEREO_UART_PACKET_BYTES];
    size_t length = robot_follow_encode(packet, bytes, sizeof(bytes));
    assert(length);
    inject(bytes, (unsigned)length);
    stereo_board_service();
}

static void flush_tx(void)
{
    unsigned index;
    tx_budget = 10000;
    for (index = 0; index < 12; ++index) stereo_board_service();
}

static unsigned collect(RobotFollowPacket *frames, unsigned capacity)
{
    RobotFollowParser parser;
    unsigned count = 0;
    RobotFollowPacket packet;
    robot_follow_parser_init(&parser);
    while (read_tx < tx_count)
        if (robot_follow_parser_push(&parser, tx_bytes[read_tx++], now, &packet)) {
            assert(count < capacity);
            frames[count++] = packet;
        }
    assert(!parser.used && !parser.crc_errors && !parser.format_errors && !parser.range_errors);
    return count;
}

static cnn_result_t local_result(uint32_t frame_id)
{
    cnn_result_t result;
    unsigned index;
    memset(&result, 0, sizeof(result));
    result.frame_id = frame_id;
    result.result_seq = frame_id;
    for (index = 0; index < CNN_JOINT_COUNT; ++index)
        result.joint[index] = (cnn_joint_t){750, 350, 50, 1};
    result.joint[8].y = 250;
    result.red_marker = 0x80000000U | (340U << 11) | 760U;
    result.green_marker = 0x80000000U | (345U << 11) | 780U;
    return result;
}

#ifdef ROBOT_STEREO_RIGHT

static void request(uint32_t session, uint32_t sequence, int enabled)
{
    RobotFollowPacket packet;
    memset(&packet, 0, sizeof(packet));
    packet.type = ROBOT_FOLLOW_REQUEST;
    packet.session = session;
    packet.sequence = sequence;
    packet.data.requested = (uint8_t)enabled;
    deliver(&packet);
}

static void pair(uint32_t session, uint32_t sequence, uint32_t frame_id, int accepted)
{
    StereoCoordinateFrame frame;
    cnn_result_t result = local_result(frame_id + 1000);
    uint8_t bytes[STEREO_UART_PACKET_BYTES];
    unsigned index, previous = publication_count;
    memset(&frame, 0, sizeof(frame));
    frame.session_id = session;
    frame.sequence = sequence;
    frame.frame_id = frame_id;
    for (index = 0; index < STEREO_UART_JOINTS; ++index)
        frame.joint[index] = (StereoCoordinateJoint){900, 350, 50, 1};
    frame.joint[8].y = 250;
    frame.markers[0] = 0x80000000U | (340U << 11) | 910U;
    frame.markers[2] = 0x80000000U | (345U << 11) | 930U;
    assert(stereo_uart_encode(&frame, bytes));
    inject(bytes, sizeof(bytes));
    accept_next = accepted;
    stereo_board_service();
    assert(stereo_board_on_result(&result, NULL));
    stereo_board_service();
    if (publication_count != previous + 1)
        fprintf(stderr, "pair session=%lu seq=%lu frame=%lu publications=%u expected=%u\n",
                (unsigned long)session, (unsigned long)sequence, (unsigned long)frame_id,
                publication_count, previous + 1);
    assert(publication_count == previous + 1);
    assert(published.left_sequence == sequence && !published.time_verified);
}

static void test_right(void)
{
    HumanForearmTarget target = {10, 20, 30, 40, 0.7f, 100, 1, 1, 0, 1, 1};
    HumanPose2D first_gripper;
    RobotFollowPacket frames[16], packet;
    uint8_t bytes[ROBOT_FOLLOW_REQUEST_BYTES];
    unsigned count, transitions;
    assert(!stereo_board_set_remote_follow(1));
    assert(!stereo_board_send_target(&target, 1, 1, 9));
    assert(!stereo_board_async_test_enabled());
    request(77, UINT32_MAX - 1U, 1);
    assert(stereo_board_remote_requested() && gate_enabled && gate_transitions == 1);
    assert(!stereo_board_async_test_enabled());
    transitions = gate_transitions;
    assert(stereo_board_set_async_test(1));
    assert(stereo_board_async_test_enabled());
    assert(stereo_board_set_async_test(0));
    assert(!stereo_board_async_test_enabled() && gate_enabled && gate_transitions == transitions);
    pair(77, 1, 100, 1);
    first_gripper = published.image_pose;
    pair(77, 2, 101, 0);
    tx_budget = 5;
    assert(stereo_board_send_target(&target, 1, 1, 9));
    assert(!stereo_board_send_target(&target, 1, 1, 9));
    assert(stereo_board_send_gripper(&first_gripper));
    assert(!stereo_board_send_gripper(&first_gripper));
    target.frame_id = 101;
    assert(!stereo_board_send_target(&target, 0, 0, 9));
    assert(stereo_board_send_gripper(&published.image_pose));
    first_gripper.gripper_2d.finger2.x += 1;
    assert(!stereo_board_send_gripper(&first_gripper));
    pair(77, 3, 102, 1);
    target.frame_id = 102;
    assert(stereo_board_send_target(&target, 0, 0, 10));
    pair(77, 4, 103, 1);
    target.frame_id = 103;
    assert(stereo_board_send_target(&target, 1, 0, 10));
    flush_tx();
    count = collect(frames, 16);
    assert(count == 3);
    assert(frames[0].type == ROBOT_FOLLOW_TARGET && frames[0].coordinate_sequence == 1);
    assert(frames[0].data.target.human.frame_id == 100 && frames[0].data.target.wrist_fresh == 1);
    assert(!frames[0].data.target.human.hand_fresh && frames[0].data.target.stationary == 1);
    assert(frames[1].type == ROBOT_FOLLOW_GRIPPER && frames[1].coordinate_sequence == 2);
    assert(frames[2].type == ROBOT_FOLLOW_TARGET && frames[2].coordinate_sequence == 4);
    assert(frames[2].data.target.human.frame_id == 103 && frames[2].data.target.wrist_fresh == 0);
    assert(frames[0].sequence < frames[1].sequence && frames[1].sequence < frames[2].sequence);
    now += 100000;
    request(77, UINT32_MAX - 1U, 1);
    assert(gate_transitions == transitions);
    now += 150000;
    assert(!stereo_board_remote_requested() && !gate_enabled && gate_transitions == transitions + 1);
    request(77, UINT32_MAX - 2U, 1);
    assert(!stereo_board_remote_requested());
    request(77, UINT32_MAX, 1);
    assert(stereo_board_remote_requested());
    request(77, 0, 1);
    assert(stereo_board_remote_requested());
    transitions = gate_transitions;
    assert(stereo_board_set_async_test(1));
    request(77, 1, 0);
    assert(!stereo_board_remote_requested() && gate_enabled && gate_transitions == transitions);
    assert(!stereo_board_send_target(&target, 1, 1, 10));
    assert(stereo_board_set_async_test(0));
    assert(!gate_enabled);
    request(88, 1, 1);
    request(77, 999, 1);
    now += 250000;
    assert(!stereo_board_remote_requested());
    memset(&packet, 0, sizeof(packet));
    packet.type = ROBOT_FOLLOW_REQUEST;
    packet.session = 88;
    packet.sequence = 2;
    packet.data.requested = 1;
    assert(robot_follow_encode(&packet, bytes, sizeof(bytes)) == sizeof(bytes));
    inject(bytes, 10);
    stereo_board_service();
    now += 250000;
    inject(bytes + 10, sizeof(bytes) - 10);
    stereo_board_service();
    assert(!stereo_board_remote_requested());
    request(88, 2, 1);
    assert(stereo_board_remote_requested());
    pair(88, 1, 104, 1);
    target.frame_id = 104;
    tx_budget = 0;
    assert(stereo_board_send_target(&target, 1, 1, 11));
    now += 250000;
    flush_tx();
    assert(!collect(frames, 16));
}

#else

static StereoCoordinateFrame send_coordinate(uint32_t frame_id)
{
    RobotFollowPacket frames[8];
    StereoCoordinateFrame frame;
    cnn_result_t result = local_result(frame_id);
    unsigned count, index, found = 0;
    assert(stereo_board_on_result(&result, NULL));
    flush_tx();
    count = collect(frames, 8);
    memset(&frame, 0, sizeof(frame));
    for (index = 0; index < count; ++index)
        if (frames[index].type == ROBOT_FOLLOW_COORDINATES) {
            frame = frames[index].data.coordinates;
            ++found;
        }
    assert(found == 1 && frame.frame_id == frame_id);
    return frame;
}

static RobotFollowPacket derived(StereoCoordinateFrame reference, uint32_t session,
                                 uint32_t sequence, RobotFollowType type)
{
    RobotFollowPacket packet;
    memset(&packet, 0, sizeof(packet));
    packet.type = type;
    packet.session = session;
    packet.sequence = sequence;
    packet.coordinate_session = reference.session_id;
    packet.coordinate_sequence = reference.sequence;
    if (type == ROBOT_FOLLOW_TARGET) {
        packet.data.target.human = (HumanForearmTarget){10, 20, 30, 40, 0.6f,
            reference.frame_id, 1, 1, 0, 1, 1};
        packet.data.target.input_epoch = 42;
        packet.data.target.stationary = 1;
        packet.data.target.wrist_fresh = 1;
    } else {
        packet.data.gripper.frame_id = reference.frame_id;
        packet.data.gripper.gripper_2d = (HumanGripper2D){{100, 200, 1},
            {110, 190, 1}, {130, 195, 1}, 2, 120};
    }
    return packet;
}

static void test_left(void)
{
    RobotFollowPacket frames[16], packet, gripper;
    StereoCoordinateFrame reference, previous;
    HumanForearmTarget target;
    HumanPose2D pose;
    uint32_t epoch, last_epoch;
    int stationary, wrist_fresh, new_session;
    unsigned count, before;
    uint8_t bytes[ROBOT_FOLLOW_TARGET_BYTES];
    assert(!stereo_board_set_async_test(1) && !stereo_board_async_test_enabled());
    assert(!stereo_board_remote_requested());
    tx_budget = 10000;
    assert(stereo_board_set_remote_follow(1));
    assert(stereo_board_remote_follow_enabled());
    assert(collect(frames, 16) == 1 && frames[0].type == ROBOT_FOLLOW_REQUEST && frames[0].data.requested);
    before = tx_count;
    assert(stereo_board_set_remote_follow(1));
    assert(tx_count == before);
    now += ROBOT_FOLLOW_HEARTBEAT_US - 1;
    stereo_board_service();
    assert(tx_count == before);
    now += 1;
    stereo_board_service();
    assert(collect(frames, 16) == 1 && frames[0].data.requested);
    reference = send_coordinate(100);
    packet = derived(reference, 91, UINT32_MAX - 2U, ROBOT_FOLLOW_TARGET);
    deliver(&packet);
    assert(stereo_board_take_target(&target, &stationary, &wrist_fresh, &epoch));
    assert(target.frame_id == 100 && stationary == 1 && wrist_fresh == 1 && !target.hand_fresh);
    last_epoch = epoch;
    assert(!stereo_board_take_target(&target, &stationary, &wrist_fresh, &epoch));
    deliver(&packet);
    assert(!stereo_board_take_target(&target, &stationary, &wrist_fresh, &epoch));
    packet.sequence++;
    deliver(&packet);
    assert(!stereo_board_take_target(&target, &stationary, &wrist_fresh, &epoch));
    gripper = derived(reference, 91, UINT32_MAX, ROBOT_FOLLOW_GRIPPER);
    deliver(&gripper);
    assert(stereo_board_take_gripper(&pose, &new_session) && new_session);
    assert(pose.frame_id == 100 && pose.gripper_2d.source == 2 && !pose.valid);
    assert(pose.gripper_2d.finger2.x == 130 && pose.gripper_2d.reference_span_px == 120);
    reference = send_coordinate(101);
    packet = derived(reference, 91, 0, ROBOT_FOLLOW_TARGET);
    deliver(&packet);
    assert(stereo_board_take_target(&target, &stationary, &wrist_fresh, &epoch) && epoch == last_epoch);
    gripper = derived(reference, 91, 1, ROBOT_FOLLOW_GRIPPER);
    deliver(&gripper);
    assert(stereo_board_take_gripper(&pose, &new_session) && !new_session);
    reference = send_coordinate(102);
    packet = derived(reference, 91, 2, ROBOT_FOLLOW_TARGET);
    packet.data.target.input_epoch = 0;
    deliver(&packet);
    assert(stereo_board_take_target(&target, &stationary, &wrist_fresh, &epoch) && epoch > last_epoch);
    last_epoch = epoch;
    reference = send_coordinate(103);
    gripper = derived(reference, 91, 3, ROBOT_FOLLOW_GRIPPER);
    deliver(&gripper);
    packet = derived(reference, 92, 1, ROBOT_FOLLOW_TARGET);
    deliver(&packet);
    assert(!stereo_board_take_gripper(&pose, &new_session));
    assert(stereo_board_take_target(&target, &stationary, &wrist_fresh, &epoch) && epoch > last_epoch);
    last_epoch = epoch;
    gripper.session = 92;
    gripper.sequence = 2;
    deliver(&gripper);
    assert(stereo_board_take_gripper(&pose, &new_session) && new_session);
    packet.session = 91;
    packet.sequence = 100;
    deliver(&packet);
    assert(!stereo_board_take_target(&target, &stationary, &wrist_fresh, &epoch));
    previous = reference;
    assert(stereo_board_set_remote_follow(0));
    assert(!stereo_board_remote_follow_enabled());
    assert(collect(frames, 16) == 1 && !frames[0].data.requested);
    now += 300000;
    flush_tx();
    assert(!collect(frames, 16));
    assert(stereo_board_set_remote_follow(1));
    assert(collect(frames, 16) == 1 && frames[0].data.requested);
    packet = derived(previous, 92, 3, ROBOT_FOLLOW_TARGET);
    deliver(&packet);
    assert(!stereo_board_take_target(&target, &stationary, &wrist_fresh, &epoch));
    reference = send_coordinate(104);
    packet = derived(reference, 92, 3, ROBOT_FOLLOW_TARGET);
    deliver(&packet);
    now += 250000;
    assert(!stereo_board_take_target(&target, &stationary, &wrist_fresh, &epoch));
    packet.sequence = 4;
    deliver(&packet);
    assert(!stereo_board_take_target(&target, &stationary, &wrist_fresh, &epoch));
    reference = send_coordinate(105);
    packet = derived(reference, 92, 4, ROBOT_FOLLOW_TARGET);
    packet.data.target.human.frame_id++;
    deliver(&packet);
    assert(!stereo_board_take_target(&target, &stationary, &wrist_fresh, &epoch));
    packet.data.target.human.frame_id--;
    assert(robot_follow_encode(&packet, bytes, sizeof(bytes)) == sizeof(bytes));
    inject(bytes, 12);
    stereo_board_service();
    now += 250000;
    inject(bytes + 12, sizeof(bytes) - 12);
    stereo_board_service();
    assert(!stereo_board_take_target(&target, &stationary, &wrist_fresh, &epoch));
    reference = send_coordinate(106);
    packet = derived(reference, 92, 4, ROBOT_FOLLOW_TARGET);
    deliver(&packet);
    assert(stereo_board_take_target(&target, &stationary, &wrist_fresh, &epoch) && epoch > last_epoch);
    reference = send_coordinate(107);
    packet = derived(reference, 92, 5, ROBOT_FOLLOW_TARGET);
    gripper = derived(reference, 92, 6, ROBOT_FOLLOW_GRIPPER);
    deliver(&packet);
    deliver(&gripper);
    assert(stereo_board_set_remote_follow(0));
    assert(!stereo_board_take_target(&target, &stationary, &wrist_fresh, &epoch));
    assert(!stereo_board_take_gripper(&pose, &new_session));
    count = collect(frames, 16);
    assert(count == 1 && !frames[0].data.requested);
    assert(!stereo_board_send_target(&target, 1, 1, 1));
    assert(!stereo_board_send_gripper(&pose));
    {
        cnn_result_t result = local_result(108);
        tx_budget = 5;
        assert(stereo_board_on_result(&result, NULL));
        tx_budget = 0;
        assert(stereo_board_set_remote_follow(1));
        flush_tx();
        count = collect(frames, 16);
        assert(count == 2 && frames[0].type == ROBOT_FOLLOW_COORDINATES &&
            frames[1].type == ROBOT_FOLLOW_REQUEST && frames[1].data.requested);
        packet = derived(frames[0].data.coordinates, 92, 7, ROBOT_FOLLOW_TARGET);
        deliver(&packet);
        assert(!stereo_board_take_target(&target, &stationary, &wrist_fresh, &epoch));
        reference = send_coordinate(109);
        gripper = derived(reference, 92, 7, ROBOT_FOLLOW_GRIPPER);
        deliver(&gripper);
        assert(stereo_board_take_gripper(&pose, &new_session) && new_session);
    }
    {
        cnn_result_t result = local_result(110);
        tx_budget = 7;
        assert(stereo_board_on_result(&result, NULL));
        result.frame_id = 111;
        assert(stereo_board_on_result(&result, NULL));
        result.frame_id = 112;
        assert(stereo_board_on_result(&result, NULL));
        now += ROBOT_FOLLOW_HEARTBEAT_US;
        flush_tx();
        count = collect(frames, 16);
        assert(count == 3 && frames[0].type == ROBOT_FOLLOW_COORDINATES &&
            frames[0].data.coordinates.frame_id == 110 && frames[1].type == ROBOT_FOLLOW_REQUEST &&
            frames[1].data.requested && frames[2].type == ROBOT_FOLLOW_COORDINATES &&
            frames[2].data.coordinates.frame_id == 112);
        packet = derived(frames[2].data.coordinates, 92, 8, ROBOT_FOLLOW_TARGET);
        deliver(&packet);
        assert(stereo_board_take_target(&target, &stationary, &wrist_fresh, &epoch));
    }
    {
        cnn_result_t result = local_result(113);
        tx_budget = 0;
        assert(stereo_board_on_result(&result, NULL));
        now += 240000;
        flush_tx();
        count = collect(frames, 16);
        assert(count == 2 && frames[0].type == ROBOT_FOLLOW_REQUEST &&
            frames[1].type == ROBOT_FOLLOW_COORDINATES);
        now += 10000;
        packet = derived(frames[1].data.coordinates, 92, 9, ROBOT_FOLLOW_TARGET);
        deliver(&packet);
        assert(!stereo_board_take_target(&target, &stationary, &wrist_fresh, &epoch));
        reference = send_coordinate(114);
        gripper = derived(reference, 92, 9, ROBOT_FOLLOW_GRIPPER);
        deliver(&gripper);
        now += 250000;
        assert(!stereo_board_take_gripper(&pose, &new_session));
    }
}

#endif
#endif

int main(void)
{
#if defined(ROBOT_STEREO_LEFT) || defined(ROBOT_STEREO_RIGHT)
    assert(stereo_board_init() == 0 && irq_handler != NULL);
#ifdef ROBOT_STEREO_RIGHT
    test_right();
#else
    test_left();
#endif
#else
    HumanForearmTarget target = {0};
    HumanPose2D pose = {0};
    int stationary, wrist_fresh, new_session;
    uint32_t epoch;
    assert(stereo_board_init() == 0);
    assert(!stereo_board_set_remote_follow(1));
    assert(!stereo_board_remote_follow_enabled() && !stereo_board_remote_requested());
    assert(!stereo_board_send_target(&target, 1, 1, 0));
    assert(!stereo_board_take_target(&target, &stationary, &wrist_fresh, &epoch));
    assert(!stereo_board_send_gripper(&pose));
    assert(!stereo_board_take_gripper(&pose, &new_session));
#endif
    puts("test_robot_follow_board: PASS");
    return 0;
}
