#include "integration/stereo_board.h"
#include "integration/trace.h"
#include "stereo_vision/robot_follow_protocol.h"

#include <string.h>

#if defined(ROBOT_STEREO_LEFT) || defined(ROBOT_STEREO_RIGHT)

#include "integration/platform_vitis.h"
#include "input_pose_cnn.h"
#include "xparameters.h"
#include "xparameters_ps.h"
#include "xil_printf.h"
#include "xstatus.h"
#include "xtime_l.h"
#include "xuartps.h"
#include "xuartps_hw.h"

#ifndef ROBOT_STEREO_UART_BAUD
#define ROBOT_STEREO_UART_BAUD 115200U
#endif

#define RX_RING_SIZE 4096U
#define RX_IRQ_MASK (XUARTPS_IXR_RXOVR | XUARTPS_IXR_TOUT | XUARTPS_IXR_OVER | \
                     XUARTPS_IXR_FRAMING | XUARTPS_IXR_PARITY)
#define RX_ERROR_MASK (XUARTPS_IXR_OVER | XUARTPS_IXR_FRAMING | XUARTPS_IXR_PARITY)
#define COORDINATE_HISTORY 64U
#define GRIPPER_REFERENCES 8U

typedef struct {
    uint64_t received_us;
    uint8_t byte;
} ReceivedByte;

typedef struct {
    RobotFollowPacket frame;
    uint64_t observed_us;
    uint32_t generation;
    int ready;
} PendingPacket;

typedef struct {
    uint32_t session, sequence, frame_id;
    uint64_t observed_us, sent_us;
} SentCoordinate;

typedef struct {
    uint32_t frame_id, session, sequence;
    uint64_t left_us, right_us;
    HumanGripper2D gripper;
} SourceReference;

static XUartPs s_uart;
static ReceivedByte s_rx[RX_RING_SIZE];
static volatile uint32_t s_head, s_tail, s_rx_overflows, s_rx_hw_errors;
static uint32_t s_seen_overflows, s_seen_hw_errors;
static RobotFollowParser s_parser;
static RobotFollowPacket s_decoded;
static RobotFollowOrder s_follow_order, s_source_order[2];
static StereoLink s_link;
static StereoDepthResult s_depth;
static uint8_t s_tx[STEREO_UART_PACKET_BYTES];
static unsigned s_tx_offset, s_tx_length, s_next_data;
static PendingPacket s_pending[3], s_active;
static int s_ready, s_depth_ready, s_local_intent, s_remote_follow, s_remote_request;
static uint32_t s_session, s_sequence, s_follow_sequence, s_generation;
static uint64_t s_remote_request_us, s_last_request_us;
static int s_have_request_tx;
#ifdef ROBOT_STEREO_RIGHT
static RobotFollowOrder s_coordinate_order;
static SourceReference s_arm_source, s_gripper_sources[GRIPPER_REFERENCES];
static unsigned s_gripper_source_next;
#else
static SentCoordinate s_sent[COORDINATE_HISTORY];
static unsigned s_sent_next;
static RobotFollowPacket s_target, s_gripper;
static int s_target_ready, s_gripper_ready, s_gripper_new_session, s_have_target_epoch;
static uint64_t s_target_coordinate_us, s_gripper_coordinate_us;
static uint32_t s_received_epoch, s_received_input_epoch, s_target_session, s_gripper_session;
#endif
static StereoBoardStats s_stats;
static uint64_t s_report_time;
static uint64_t s_depth_time;
static uint32_t s_request_tx, s_target_tx, s_gripper_tx, s_target_rx, s_gripper_rx;
static uint32_t s_follow_expired, s_follow_rejected;

static uint64_t local_time_us(void);
static void service_tx(void);

static void update_gate(uint64_t now)
{
#ifdef ROBOT_STEREO_RIGHT
    int enabled;
    if (s_remote_request && !robot_follow_fresh(now, s_remote_request_us)) {
        s_remote_request = 0;
        s_pending[1].ready = s_pending[2].ready = 0;
    }
    enabled = s_local_intent || s_remote_request;
    if ((int)s_link.async_test_enabled == enabled) return;
    stereo_link_set_async_test(&s_link, enabled);
    input_pose_cnn_set_async_test(enabled);
    s_depth_ready = 0;
    s_depth_time = 0;
    memset(&s_depth, 0, sizeof(s_depth));
    memset(&s_arm_source, 0, sizeof(s_arm_source));
    memset(s_gripper_sources, 0, sizeof(s_gripper_sources));
#else
    (void)now;
#endif
}

int stereo_board_set_async_test(int enabled)
{
#ifdef ROBOT_STEREO_RIGHT
    if (!s_ready) return 0;
    enabled = enabled ? 1 : 0;
    s_local_intent = enabled;
    update_gate(local_time_us());
    return 1;
#else
    (void)enabled;
    return 0;
#endif
}

int stereo_board_async_test_enabled(void)
{
    return s_ready && s_local_intent;
}

int stereo_board_set_remote_follow(int enabled)
{
#ifdef ROBOT_STEREO_LEFT
    PendingPacket *pending = &s_pending[0];
    if (!s_ready) return 0;
    enabled = enabled ? 1 : 0;
    if (s_remote_follow == enabled) return 1;
    s_remote_follow = enabled;
    ++s_generation;
    s_target_ready = s_gripper_ready = s_have_target_epoch = 0;
    s_gripper_new_session = 1;
    memset(s_sent, 0, sizeof(s_sent));
    memset(s_source_order, 0, sizeof(s_source_order));
    memset(pending, 0, sizeof(*pending));
    pending->frame.type = ROBOT_FOLLOW_REQUEST;
    pending->frame.data.requested = (uint8_t)enabled;
    pending->ready = 1;
    s_have_request_tx = 0;
    service_tx();
    return 1;
#else
    (void)enabled;
    return 0;
#endif
}

int stereo_board_remote_follow_enabled(void)
{
    return s_ready && s_remote_follow;
}

int stereo_board_remote_requested(void)
{
    if (!s_ready) return 0;
    update_gate(local_time_us());
    return s_remote_request;
}

int stereo_board_set_pixel_tau_us(uint32_t tau_us)
{
#ifdef ROBOT_STEREO_RIGHT
    return s_ready && stereo_link_set_pixel_tau_us(&s_link, tau_us);
#else
    (void)tau_us;
    return 0;
#endif
}

uint32_t stereo_board_pixel_tau_us(void)
{
    return s_ready ? s_link.pixel_tau_us : STEREO_LINK_PIXEL_TAU_US;
}

static uint64_t local_time_us(void)
{
    XTime now;
    XTime_GetTime(&now);
    return (now / COUNTS_PER_SECOND) * 1000000U +
           (now % COUNTS_PER_SECOND) * 1000000U / COUNTS_PER_SECOND;
}

static void receive_isr(void *reference)
{
    unsigned budget = 64;
    UINTPTR base = s_uart.Config.BaseAddress;
    uint32_t status = XUartPs_ReadReg(base, XUARTPS_ISR_OFFSET);
    uint64_t received = local_time_us();
    (void)reference;
    if (status & RX_ERROR_MASK) ++s_rx_hw_errors;
    XUartPs_WriteReg(base, XUARTPS_ISR_OFFSET, status & RX_IRQ_MASK);
    while (budget-- && XUartPs_IsReceiveData(base)) {
        uint8_t byte = (uint8_t)XUartPs_ReadReg(base, XUARTPS_FIFO_OFFSET);
        if (s_head - s_tail == RX_RING_SIZE) {
            ++s_rx_overflows;
        } else {
            s_rx[s_head & (RX_RING_SIZE - 1U)].byte = byte;
            s_rx[s_head & (RX_RING_SIZE - 1U)].received_us = received;
            __sync_synchronize();
            ++s_head;
        }
    }
    if (status & XUARTPS_IXR_TOUT)
        XUartPs_WriteReg(base, XUARTPS_CR_OFFSET,
            XUartPs_ReadReg(base, XUARTPS_CR_OFFSET) | XUARTPS_CR_TORST);
}

int stereo_board_init(void)
{
    unsigned index;
    XUartPs_Config *config = NULL;
    XUartPsFormat format = {
        .BaudRate = ROBOT_STEREO_UART_BAUD,
        .DataBits = XUARTPS_FORMAT_8_BITS,
        .Parity = XUARTPS_FORMAT_NO_PARITY,
        .StopBits = XUARTPS_FORMAT_1_STOP_BIT
    };
    if (STDIN_BASEADDRESS != 0xE0001000U || STDOUT_BASEADDRESS != 0xE0001000U) return -1;
    for (index = 0; index < XPAR_XUARTPS_NUM_INSTANCES; ++index) {
        XUartPs_Config *candidate = XUartPs_LookupConfig((u16)index);
        if (candidate != NULL && candidate->BaseAddress == 0xE0000000U) config = candidate;
    }
    if (config == NULL || XUartPs_CfgInitialize(&s_uart, config, config->BaseAddress) != XST_SUCCESS ||
        XUartPs_SetBaudRate(&s_uart, ROBOT_STEREO_UART_BAUD) != XST_SUCCESS ||
        XUartPs_SetDataFormat(&s_uart, &format) != XST_SUCCESS) return -1;
    XUartPs_SetInterruptMask(&s_uart, 0);
    XUartPs_SetOperMode(&s_uart, XUARTPS_OPER_MODE_NORMAL);
    XUartPs_SetFifoThreshold(&s_uart, 16);
    XUartPs_SetRecvTimeout(&s_uart, 8);
    s_ready = 0;
    robot_follow_parser_init(&s_parser);
    memset(&s_follow_order, 0, sizeof(s_follow_order));
    memset(s_source_order, 0, sizeof(s_source_order));
#ifdef ROBOT_STEREO_RIGHT
    memset(&s_coordinate_order, 0, sizeof(s_coordinate_order));
    memset(&s_arm_source, 0, sizeof(s_arm_source));
    memset(s_gripper_sources, 0, sizeof(s_gripper_sources));
    s_gripper_source_next = 0;
#else
    memset(s_sent, 0, sizeof(s_sent));
    s_sent_next = 0;
    s_target_ready = s_gripper_ready = s_have_target_epoch = 0;
    s_gripper_new_session = 1;
    s_received_epoch = s_received_input_epoch = s_target_session = s_gripper_session = 0;
#endif
    if (!stereo_link_init(&s_link, &stereo_calibration_current)) return -1;
    memset(&s_stats, 0, sizeof(s_stats));
    s_request_tx = s_target_tx = s_gripper_tx = s_target_rx = s_gripper_rx = 0;
    s_follow_expired = s_follow_rejected = 0;
    s_head = s_tail = s_rx_overflows = s_rx_hw_errors = 0;
    s_seen_overflows = s_seen_hw_errors = 0;
    s_tx_offset = s_tx_length = s_next_data = 0;
    memset(s_pending, 0, sizeof(s_pending));
    memset(&s_active, 0, sizeof(s_active));
    s_depth_ready = 0;
    s_local_intent = s_remote_follow = s_remote_request = s_have_request_tx = 0;
    s_remote_request_us = s_last_request_us = 0;
    s_follow_sequence = s_generation = 0;
    s_session = (uint32_t)local_time_us();
    if (!s_session) s_session = 1;
    s_sequence = 0;
    s_depth_time = 0;
    memset(&s_depth, 0, sizeof(s_depth));
    s_report_time = local_time_us();
    if (XScuGic_Connect(platform_vitis_gic(), XPAR_PS7_UART_0_INTR,
        (Xil_InterruptHandler)receive_isr, NULL) != XST_SUCCESS) return -1;
    XScuGic_SetPriorityTriggerType(platform_vitis_gic(), XPAR_PS7_UART_0_INTR, 0xA0, 1);
    s_ready = 1;
    input_pose_cnn_set_stereo(1);
    XUartPs_WriteReg(config->BaseAddress, XUARTPS_ISR_OFFSET, XUARTPS_IXR_MASK);
    XScuGic_Enable(platform_vitis_gic(), XPAR_PS7_UART_0_INTR);
    XUartPs_SetInterruptMask(&s_uart, RX_IRQ_MASK);
    return 0;
}

static void service_tx(void)
{
    unsigned budget = 64;
    UINTPTR base = s_uart.Config.BaseAddress;
    uint64_t now = local_time_us();
    update_gate(now);
    while (budget && !XUartPs_IsTransmitFull(base)) {
        if (s_tx_offset == s_tx_length) {
            unsigned slot;
#ifdef ROBOT_STEREO_LEFT
            slot = s_pending[0].ready ? 0U : 1U;
#else
            slot = s_next_data ? 2U : 1U;
            if (!s_pending[slot].ready) slot = slot == 1U ? 2U : 1U;
#endif
            if (!s_pending[slot].ready) break;
            s_active = s_pending[slot];
            s_pending[slot].ready = 0;
            if (s_active.frame.type != ROBOT_FOLLOW_REQUEST &&
                (!robot_follow_fresh(now, s_active.observed_us)
#ifdef ROBOT_STEREO_RIGHT
                 || !s_remote_request || s_active.frame.coordinate_session != s_follow_order.session
#endif
                 )) {
                ++s_stats.tx_dropped;
                continue;
            }
            if (s_active.frame.type == ROBOT_FOLLOW_COORDINATES) {
                if (!stereo_uart_encode(&s_active.frame.data.coordinates, s_tx)) continue;
                s_tx_length = STEREO_UART_PACKET_BYTES;
            } else {
                s_active.frame.session = s_session;
                s_active.frame.sequence = ++s_follow_sequence;
                s_tx_length = (unsigned)robot_follow_encode(&s_active.frame, s_tx, sizeof(s_tx));
                if (!s_tx_length) {
                    s_tx_offset = 0;
                    ++s_stats.tx_dropped;
                    continue;
                }
            }
            s_active.frame.received_us = now;
#ifdef ROBOT_STEREO_LEFT
            if (slot == 0U) {
                s_last_request_us = now;
                s_have_request_tx = 1;
            }
#else
            s_next_data = slot == 1U ? 1U : 0U;
#endif
            s_tx_offset = 0;
        }
        XUartPs_WriteReg(base, XUARTPS_FIFO_OFFSET, s_tx[s_tx_offset++]);
        --budget;
        if (s_tx_offset == s_tx_length) {
            ++s_stats.tx_frames;
            if (s_active.frame.type == ROBOT_FOLLOW_REQUEST) ++s_request_tx;
            else if (s_active.frame.type == ROBOT_FOLLOW_TARGET) ++s_target_tx;
            else if (s_active.frame.type == ROBOT_FOLLOW_GRIPPER) ++s_gripper_tx;
#ifdef ROBOT_STEREO_LEFT
            if (s_active.frame.type == ROBOT_FOLLOW_COORDINATES && s_remote_follow &&
                s_active.generation == s_generation) {
                const StereoCoordinateFrame *frame = &s_active.frame.data.coordinates;
                SentCoordinate *sent = &s_sent[s_sent_next++ % COORDINATE_HISTORY];
                *sent = (SentCoordinate){frame->session_id, frame->sequence, frame->frame_id,
                    s_active.observed_us, s_active.frame.received_us};
            }
#endif
        }
    }
}

#ifdef ROBOT_STEREO_LEFT
static int coordinate_time(const RobotFollowPacket *packet, uint64_t now, uint64_t *observed)
{
    unsigned index;
    uint32_t frame_id = packet->type == ROBOT_FOLLOW_TARGET
        ? packet->data.target.human.frame_id : packet->data.gripper.frame_id;
    if (packet->coordinate_session != s_session) return 0;
    for (index = 0; index < COORDINATE_HISTORY; ++index) {
        const SentCoordinate *sent = &s_sent[index];
        if (sent->session == packet->coordinate_session && sent->sequence == packet->coordinate_sequence &&
            sent->frame_id == frame_id && robot_follow_fresh(now, sent->sent_us) &&
            robot_follow_fresh(now, sent->observed_us)) {
            *observed = sent->observed_us;
            return 1;
        }
    }
    return 0;
}
#endif

static void receive_packet(const RobotFollowPacket *packet, uint64_t now)
{
    if (!robot_follow_fresh(now, packet->received_us)) {
        ++s_stats.expired_frames;
        if (packet->type != ROBOT_FOLLOW_COORDINATES) ++s_follow_expired;
        return;
    }
#ifdef ROBOT_STEREO_RIGHT
    if (packet->type == ROBOT_FOLLOW_COORDINATES) {
        const StereoCoordinateFrame *frame = &packet->data.coordinates;
        if (!robot_follow_order_accept(&s_coordinate_order, frame->session_id, frame->sequence)) {
            ++s_stats.rejected_frames;
            return;
        }
        ++s_stats.rx_frames;
        (void)stereo_link_push(&s_link, 0, frame, packet->received_us);
    } else if (packet->type == ROBOT_FOLLOW_REQUEST) {
        int new_session = s_follow_order.initialized && s_follow_order.session != packet->session;
        if (!robot_follow_order_accept(&s_follow_order, packet->session, packet->sequence)) {
            ++s_stats.rejected_frames;
            ++s_follow_rejected;
            return;
        }
        if (new_session || !packet->data.requested) {
            s_pending[1].ready = s_pending[2].ready = 0;
            if (new_session) {
                memset(&s_arm_source, 0, sizeof(s_arm_source));
                memset(s_gripper_sources, 0, sizeof(s_gripper_sources));
            }
        }
        s_remote_request = packet->data.requested;
        s_remote_request_us = packet->received_us;
        update_gate(now);
    } else {
        ++s_stats.rejected_frames;
        ++s_follow_rejected;
    }
#else
    uint64_t observed;
    unsigned stream;
    RobotFollowOrder *source;
    if (packet->type != ROBOT_FOLLOW_TARGET && packet->type != ROBOT_FOLLOW_GRIPPER) {
        ++s_stats.rejected_frames;
        return;
    }
    if (!s_remote_follow || !coordinate_time(packet, now, &observed)) {
        ++s_stats.expired_frames;
        ++s_follow_expired;
        return;
    }
    if (!robot_follow_order_accept(&s_follow_order, packet->session, packet->sequence)) {
        ++s_stats.rejected_frames;
        ++s_follow_rejected;
        return;
    }
    stream = packet->type == ROBOT_FOLLOW_TARGET ? 0U : 1U;
    source = &s_source_order[stream];
    if (source->initialized && source->session != packet->session) memset(source, 0, sizeof(*source));
    if (!robot_follow_order_accept(source, packet->session, packet->coordinate_sequence)) {
        ++s_stats.rejected_frames;
        ++s_follow_rejected;
        return;
    }
    if (s_target_session && s_target_session != packet->session) s_target_ready = 0;
    if (s_gripper_session && s_gripper_session != packet->session) {
        s_gripper_ready = 0;
        s_gripper_new_session = 1;
    }
    if (packet->type == ROBOT_FOLLOW_TARGET) {
        if (!s_have_target_epoch || s_target_session != packet->session ||
            s_received_input_epoch != packet->data.target.input_epoch) {
            ++s_received_epoch;
            if (!s_received_epoch) ++s_received_epoch;
        }
        s_have_target_epoch = 1;
        s_target_session = packet->session;
        s_received_input_epoch = packet->data.target.input_epoch;
        s_target = *packet;
        s_target_coordinate_us = observed;
        s_target_ready = 1;
        ++s_target_rx;
    } else {
        if (s_gripper_session != packet->session) s_gripper_new_session = 1;
        s_gripper_session = packet->session;
        s_gripper = *packet;
        s_gripper_coordinate_us = observed;
        s_gripper_ready = 1;
        ++s_gripper_rx;
    }
#endif
}

void stereo_board_service(void)
{
    unsigned budget = 512;
    uint64_t now;
    if (!s_ready) return;
    now = local_time_us();
    update_gate(now);
    if (s_seen_overflows != s_rx_overflows || s_seen_hw_errors != s_rx_hw_errors) {
        s_seen_overflows = s_rx_overflows;
        s_seen_hw_errors = s_rx_hw_errors;
        s_tail = s_head;
        s_parser.used = 0;
        s_link.count[0] = s_link.count[1] = 0;
        s_link.ready = s_depth_ready = 0;
        s_pending[1].ready = s_pending[2].ready = 0;
#ifdef ROBOT_STEREO_RIGHT
        memset(&s_arm_source, 0, sizeof(s_arm_source));
        memset(s_gripper_sources, 0, sizeof(s_gripper_sources));
#else
        s_target_ready = s_gripper_ready = 0;
#endif
        input_pose_cnn_discard_stereo_pending();
    }
    while (budget-- && s_tail != s_head) {
        ReceivedByte received;
        __sync_synchronize();
        received = s_rx[s_tail & (RX_RING_SIZE - 1U)];
        __sync_synchronize();
        ++s_tail;
        if (robot_follow_parser_push(&s_parser, received.byte, received.received_us, &s_decoded))
            receive_packet(&s_decoded, local_time_us());
    }
#ifdef ROBOT_STEREO_LEFT
    now = local_time_us();
    if (s_remote_follow && !s_pending[0].ready &&
        (!s_have_request_tx || now < s_last_request_us || now - s_last_request_us >= ROBOT_FOLLOW_HEARTBEAT_US)) {
        memset(&s_pending[0], 0, sizeof(s_pending[0]));
        s_pending[0].frame.type = ROBOT_FOLLOW_REQUEST;
        s_pending[0].frame.data.requested = 1;
        s_pending[0].ready = 1;
    }
#endif
    service_tx();
#ifdef ROBOT_STEREO_RIGHT
    if (stereo_link_take_at(&s_link, &s_depth, local_time_us())) {
        int accepted;
        SourceReference reference = {s_depth.image_pose.frame_id, s_depth.left_session_id,
            s_depth.left_sequence, s_depth.left_received_us, s_depth.right_received_us,
            s_depth.image_pose.gripper_2d};
        s_depth_ready = 1;
        s_depth_time = s_depth.left_received_us < s_depth.right_received_us
            ? s_depth.left_received_us : s_depth.right_received_us;
        accepted = input_pose_cnn_publish_stereo(&s_depth);
        s_gripper_sources[s_gripper_source_next++ % GRIPPER_REFERENCES] = reference;
        TRACE_PG(s_link.pairs, &s_depth, accepted, input_pose_cnn_admission_reason());
        TRACE_RQ(s_link.pairs, &s_depth, input_pose_cnn_tracking_state(),
            input_pose_cnn_reacquire_count(), input_pose_cnn_filter_epoch(), local_time_us());
        if (accepted) {
            s_arm_source = reference;
            if (!s_depth.time_verified) ++s_stats.async_accepted;
        } else if (s_link.async_test_enabled && !s_depth.time_verified) {
            ++s_stats.async_rejected;
            input_pose_cnn_discard_stereo_arm_pending();
        }
    }
#endif
    now = local_time_us();
    if (s_link.pairs && (now < s_depth_time ||
        now - s_depth_time > STEREO_LINK_MAX_AGE_US)) {
        s_depth_ready = 0;
        input_pose_cnn_discard_stereo_pending();
    }
    if (now - s_report_time >= 1000000U) {
        unsigned index;
        int fresh = s_link.pairs && now >= s_depth_time &&
            now - s_depth_time <= STEREO_LINK_MAX_AGE_US;
        int available = fresh;
        long depth_mm[3] = {0, 0, 0};
        const unsigned points[3] = {10, 17, 19};
        s_report_time = now;
        for (index = 0; index < 3; ++index) {
            double depth = s_depth.point[points[index]].z_mm;
            if (available && s_depth.point_status[points[index]] == STEREO_OK &&
                depth > 0.0 && depth < 2147483647.0) depth_mm[index] = (long)depth;
        }
        xil_printf("[ST] tx=%lu drop=%lu rx=%lu crc=%lu overflow=%lu hw=%lu pairs=%lu "
                   "L=%lu R=%lu fresh=%u sync=%u control=%d Zmm(wrist,red,green)=%ld,%ld,%ld "
                   "async_test=%u async_status=%d accept=%lu reject=%lu "
                   "qL=%u qR=%u qdrop=%lu stale=%lu gap=%lu skip=%lu "
                   "marker_bad=%lu jump_bad=%lu reacquire=%lu "
                   "time_gate=%s; receipt timing NOT exposure sync\r\n",
            (unsigned long)s_stats.tx_frames, (unsigned long)s_stats.tx_dropped,
            (unsigned long)s_stats.rx_frames, (unsigned long)s_parser.crc_errors,
            (unsigned long)s_rx_overflows, (unsigned long)s_rx_hw_errors,
            (unsigned long)s_link.pairs, (unsigned long)s_depth.left_frame_id,
            (unsigned long)s_depth.right_frame_id, (unsigned)fresh,
            available ? (unsigned)s_depth.time_verified : 0U,
            available ? (int)s_depth.control_status : (int)STEREO_POSE_UNVERIFIED,
            depth_mm[0], depth_mm[1], depth_mm[2], (unsigned)s_link.async_test_enabled,
            available ? (int)s_depth.async_status : (int)STEREO_POSE_UNVERIFIED,
            (unsigned long)s_stats.async_accepted, (unsigned long)s_stats.async_rejected,
            s_link.count[0], s_link.count[1], (unsigned long)s_link.queue_overflows,
            (unsigned long)s_link.stale_frames, (unsigned long)s_link.receive_gap_rejects,
            (unsigned long)s_link.skipped_frames, (unsigned long)s_link.marker_rejects,
            (unsigned long)s_link.jump_rejects, (unsigned long)s_link.reacquired_points,
            s_link.async_test_enabled ? "RECEIPT_LATEST" : "STRICT");
        xil_printf("[RF] role=%s local=%u requested=%u remote=%u gate=%u "
                   "request_tx=%lu target_tx=%lu target_rx=%lu gripper_tx=%lu gripper_rx=%lu "
                   "stale=%lu reject=%lu; receipt timing NOT exposure sync\r\n",
#ifdef ROBOT_STEREO_RIGHT
            "RIGHT",
#else
            "LEFT",
#endif
            (unsigned)s_local_intent, (unsigned)s_remote_follow, (unsigned)s_remote_request,
            (unsigned)s_link.async_test_enabled, (unsigned long)s_request_tx,
            (unsigned long)s_target_tx, (unsigned long)s_target_rx,
            (unsigned long)s_gripper_tx, (unsigned long)s_gripper_rx,
            (unsigned long)s_follow_expired, (unsigned long)s_follow_rejected);
    }
}

int stereo_board_on_result(const cnn_result_t *result, const StereoFrameMetadata *metadata)
{
    StereoCoordinateFrame frame;
    unsigned index;
    if (!s_ready || result == NULL) return 0;
    update_gate(local_time_us());
    memset(&frame, 0, sizeof(frame));
    frame.session_id = s_session;
    frame.sequence = ++s_sequence;
    frame.frame_id = result->frame_id;
    frame.result_seq = result->result_seq;
    frame.cycle_count = result->cycle_count;
    frame.joint_flags = result->joint_flags & 0x1FFFFU;
    frame.markers[0] = result->red_marker;
    frame.markers[1] = result->blue_marker;
    frame.markers[2] = result->green_marker;
    frame.markers[3] = result->yellow_marker;
    if (metadata != NULL) frame.metadata = *metadata;
    for (index = 0; index < STEREO_UART_JOINTS; ++index) {
        frame.joint[index] = (StereoCoordinateJoint){result->joint[index].x, result->joint[index].y,
            result->joint[index].score, result->joint[index].valid &&
            result->joint[index].x < 1280 && result->joint[index].y < 720};
    }
    for (index = 0; index < STEREO_UART_MARKERS; ++index)
        if ((frame.markers[index] & 0x7FFU) >= 1280 || ((frame.markers[index] >> 11) & 0x3FFU) >= 720)
            frame.markers[index] &= ~0x80000000U;
    TRACE_RAW(&frame);
#ifdef ROBOT_STEREO_LEFT
    uint8_t validation[STEREO_UART_PACKET_BYTES];
    if (!stereo_uart_encode(&frame, validation)) return 0;
    if (s_pending[1].ready) ++s_stats.tx_dropped;
    s_pending[1].frame.type = ROBOT_FOLLOW_COORDINATES;
    s_pending[1].frame.data.coordinates = frame;
    s_pending[1].observed_us = local_time_us();
    s_pending[1].generation = s_generation;
    s_pending[1].ready = 1;
    service_tx();
    return 1;
#else
    return stereo_link_push(&s_link, 1, &frame, local_time_us());
#endif
}

int stereo_board_take_depth(StereoDepthResult *result)
{
    uint64_t now;
    if (result == NULL || !s_depth_ready) return 0;
    now = local_time_us();
    if (now < s_depth_time || now - s_depth_time > STEREO_LINK_MAX_AGE_US) {
        s_depth_ready = 0;
        return 0;
    }
    *result = s_depth;
    s_depth_ready = 0;
    return 1;
}

void stereo_board_stats(StereoBoardStats *stats)
{
    if (stats == NULL) return;
    *stats = s_stats;
    stats->rx_overflows = s_rx_overflows;
    stats->rx_hw_errors = s_rx_hw_errors;
    stats->crc_errors = s_parser.crc_errors;
    stats->format_errors = s_parser.format_errors;
    stats->range_errors = s_parser.range_errors;
    stats->rejected_frames += s_link.rejected_frames;
    stats->expired_frames += s_link.expired_frames;
    stats->pairs = s_link.pairs;
    stats->unsynchronized_pairs = s_link.unsynchronized_pairs;
}

#ifdef ROBOT_STEREO_RIGHT
static int prepare_data(RobotFollowPacket *packet, const SourceReference *source)
{
    uint64_t now = local_time_us();
    update_gate(now);
    if (!s_ready || !s_remote_request || !source->session ||
        source->session != s_follow_order.session ||
        !robot_follow_fresh(now, source->left_us) ||
        !robot_follow_fresh(now, source->right_us)) return 0;
    packet->session = s_session;
    packet->coordinate_session = source->session;
    packet->coordinate_sequence = source->sequence;
    return 1;
}

static int queue_data(RobotFollowPacket *packet, unsigned slot, const SourceReference *reference)
{
    uint8_t bytes[ROBOT_FOLLOW_GRIPPER_BYTES];
    RobotFollowOrder *source = &s_source_order[slot - 1U];
    if (!robot_follow_encode(packet, bytes, sizeof(bytes)) ||
        !robot_follow_order_accept(source, packet->coordinate_session, packet->coordinate_sequence)) return 0;
    if (s_pending[slot].ready) ++s_stats.tx_dropped;
    s_pending[slot].frame = *packet;
    s_pending[slot].observed_us = reference->left_us < reference->right_us
        ? reference->left_us : reference->right_us;
    s_pending[slot].ready = 1;
    service_tx();
    return 1;
}

static int same_point(Point2D first, Point2D second)
{
    return first.valid == second.valid && first.x == second.x && first.y == second.y;
}

static int same_gripper(const HumanGripper2D *first, const HumanGripper2D *second)
{
    return first->source == second->source && first->reference_span_px == second->reference_span_px &&
        same_point(first->wrist, second->wrist) && same_point(first->finger1, second->finger1) &&
        same_point(first->finger2, second->finger2);
}
#endif

int stereo_board_send_target(const HumanForearmTarget *target, int stationary,
                             int wrist_fresh, uint32_t epoch)
{
#ifdef ROBOT_STEREO_RIGHT
    RobotFollowPacket packet;
    if (target == NULL || !target->valid || stationary < 0 || stationary > 1 ||
        wrist_fresh < 0 || wrist_fresh > 1) return 0;
    memset(&packet, 0, sizeof(packet));
    packet.type = ROBOT_FOLLOW_TARGET;
    if (!prepare_data(&packet, &s_arm_source) || target->frame_id != s_arm_source.frame_id) return 0;
    packet.data.target.human = *target;
    packet.data.target.stationary = (uint8_t)stationary;
    packet.data.target.wrist_fresh = (uint8_t)wrist_fresh;
    packet.data.target.input_epoch = epoch;
    return queue_data(&packet, 1, &s_arm_source);
#else
    (void)target; (void)stationary; (void)wrist_fresh; (void)epoch;
    return 0;
#endif
}

int stereo_board_take_target(HumanForearmTarget *target, int *stationary,
                             int *wrist_fresh, uint32_t *epoch)
{
#ifdef ROBOT_STEREO_LEFT
    uint64_t now;
    if (target == NULL || stationary == NULL || wrist_fresh == NULL || epoch == NULL ||
        !s_ready || !s_target_ready) return 0;
    s_target_ready = 0;
    now = local_time_us();
    if (!s_remote_follow || !robot_follow_fresh(now, s_target.received_us) ||
        !robot_follow_fresh(now, s_target_coordinate_us)) {
        ++s_follow_expired;
        return 0;
    }
    *target = s_target.data.target.human;
    *stationary = s_target.data.target.stationary;
    *wrist_fresh = s_target.data.target.wrist_fresh;
    *epoch = s_received_epoch;
    return 1;
#else
    (void)target; (void)stationary; (void)wrist_fresh; (void)epoch;
    return 0;
#endif
}

int stereo_board_send_gripper(const HumanPose2D *pose)
{
#ifdef ROBOT_STEREO_RIGHT
    RobotFollowPacket packet;
    unsigned index;
    if (pose == NULL) return 0;
    for (index = 0; index < GRIPPER_REFERENCES; ++index) {
        const SourceReference *source = &s_gripper_sources[index];
        if (source->frame_id != pose->frame_id || !same_gripper(&source->gripper, &pose->gripper_2d)) continue;
        memset(&packet, 0, sizeof(packet));
        packet.type = ROBOT_FOLLOW_GRIPPER;
        if (!prepare_data(&packet, source)) continue;
        packet.data.gripper = *pose;
        return queue_data(&packet, 2, source);
    }
    return 0;
#else
    (void)pose;
    return 0;
#endif
}

int stereo_board_take_gripper(HumanPose2D *pose, int *new_session)
{
#ifdef ROBOT_STEREO_LEFT
    uint64_t now;
    if (pose == NULL || new_session == NULL || !s_ready || !s_gripper_ready) return 0;
    s_gripper_ready = 0;
    now = local_time_us();
    if (!s_remote_follow || !robot_follow_fresh(now, s_gripper.received_us) ||
        !robot_follow_fresh(now, s_gripper_coordinate_us)) {
        ++s_follow_expired;
        return 0;
    }
    *pose = s_gripper.data.gripper;
    *new_session = s_gripper_new_session;
    s_gripper_new_session = 0;
    return 1;
#else
    (void)pose; (void)new_session;
    return 0;
#endif
}

#else

int stereo_board_init(void) { return 0; }
int stereo_board_set_async_test(int enabled) { (void)enabled; return 0; }
int stereo_board_async_test_enabled(void) { return 0; }
int stereo_board_set_remote_follow(int enabled) { (void)enabled; return 0; }
int stereo_board_remote_follow_enabled(void) { return 0; }
int stereo_board_remote_requested(void) { return 0; }
int stereo_board_send_target(const HumanForearmTarget *target, int stationary,
                             int wrist_fresh, uint32_t epoch)
{
    (void)target; (void)stationary; (void)wrist_fresh; (void)epoch;
    return 0;
}
int stereo_board_take_target(HumanForearmTarget *target, int *stationary,
                             int *wrist_fresh, uint32_t *epoch)
{
    (void)target; (void)stationary; (void)wrist_fresh; (void)epoch;
    return 0;
}
int stereo_board_send_gripper(const HumanPose2D *pose) { (void)pose; return 0; }
int stereo_board_take_gripper(HumanPose2D *pose, int *new_session)
{
    (void)pose; (void)new_session;
    return 0;
}
int stereo_board_set_pixel_tau_us(uint32_t tau_us) { (void)tau_us; return 0; }
uint32_t stereo_board_pixel_tau_us(void) { return STEREO_LINK_PIXEL_TAU_US; }
void stereo_board_service(void) { }
int stereo_board_on_result(const cnn_result_t *result, const StereoFrameMetadata *metadata)
{
    (void)result;
    (void)metadata;
    return 0;
}
int stereo_board_take_depth(StereoDepthResult *result) { (void)result; return 0; }
void stereo_board_stats(StereoBoardStats *stats)
{
    if (stats != NULL) memset(stats, 0, sizeof(*stats));
}

#endif
