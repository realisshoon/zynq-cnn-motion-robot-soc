#include "integration/stereo_board.h"

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

typedef struct {
    uint64_t received_us;
    uint8_t byte;
} ReceivedByte;

static XUartPs s_uart;
static ReceivedByte s_rx[RX_RING_SIZE];
static volatile uint32_t s_head, s_tail, s_rx_overflows, s_rx_hw_errors;
static uint32_t s_seen_overflows, s_seen_hw_errors;
static StereoUartParser s_parser;
static StereoLink s_link;
static StereoCoordinateFrame s_decoded;
static StereoDepthResult s_depth;
static uint8_t s_tx[STEREO_UART_PACKET_BYTES], s_pending[STEREO_UART_PACKET_BYTES];
static unsigned s_tx_offset;
static int s_pending_ready, s_ready, s_depth_ready;
static uint32_t s_session, s_sequence;
static StereoBoardStats s_stats;
static uint64_t s_report_time;
static uint64_t s_depth_time;

int stereo_board_set_async_test(int enabled)
{
#ifdef ROBOT_STEREO_RIGHT
    if (!s_ready) return 0;
    enabled = enabled ? 1 : 0;
    if ((int)s_link.async_test_enabled == enabled) return 1;
    stereo_link_set_async_test(&s_link, enabled);
    input_pose_cnn_set_async_test(enabled);
    s_depth_ready = 0;
    s_depth_time = 0;
    memset(&s_depth, 0, sizeof(s_depth));
    return 1;
#else
    (void)enabled;
    return 0;
#endif
}

int stereo_board_async_test_enabled(void)
{
    return s_ready && s_link.async_test_enabled;
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
    stereo_uart_parser_init(&s_parser);
    if (!stereo_link_init(&s_link, &stereo_calibration_current)) return -1;
    memset(&s_stats, 0, sizeof(s_stats));
    s_head = s_tail = s_rx_overflows = s_rx_hw_errors = 0;
    s_seen_overflows = s_seen_hw_errors = 0;
    s_tx_offset = STEREO_UART_PACKET_BYTES;
    s_pending_ready = s_depth_ready = 0;
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
    while (budget && !XUartPs_IsTransmitFull(base)) {
        if (s_tx_offset == STEREO_UART_PACKET_BYTES) {
            if (!s_pending_ready) break;
            memcpy(s_tx, s_pending, sizeof(s_tx));
            s_pending_ready = 0;
            s_tx_offset = 0;
        }
        XUartPs_WriteReg(base, XUARTPS_FIFO_OFFSET, s_tx[s_tx_offset++]);
        --budget;
        if (s_tx_offset == STEREO_UART_PACKET_BYTES) ++s_stats.tx_frames;
    }
}

void stereo_board_service(void)
{
    unsigned budget = 512;
    uint64_t now;
    if (!s_ready) return;
    if (s_seen_overflows != s_rx_overflows || s_seen_hw_errors != s_rx_hw_errors) {
        s_seen_overflows = s_rx_overflows;
        s_seen_hw_errors = s_rx_hw_errors;
        s_tail = s_head;
        s_parser.used = 0;
        if (s_link.async_test_enabled) {
            stereo_link_set_async_test(&s_link, 1);
            input_pose_cnn_discard_stereo_pending();
        }
    }
    while (budget-- && s_tail != s_head) {
        ReceivedByte received;
        __sync_synchronize();
        received = s_rx[s_tail & (RX_RING_SIZE - 1U)];
        __sync_synchronize();
        ++s_tail;
        if (stereo_uart_parser_push(&s_parser, received.byte, &s_decoded)) {
            ++s_stats.rx_frames;
#ifdef ROBOT_STEREO_RIGHT
            if (local_time_us() >= received.received_us &&
                (s_link.async_test_enabled ||
                 local_time_us() - received.received_us <= STEREO_LINK_MAX_AGE_US))
                (void)stereo_link_push(&s_link, 0, &s_decoded, received.received_us);
            else ++s_stats.expired_frames;
#endif
        }
    }
    service_tx();
#ifdef ROBOT_STEREO_RIGHT
    if (stereo_link_take(&s_link, &s_depth)) {
        s_depth_ready = 1;
        s_depth_time = local_time_us();
        if (input_pose_cnn_publish_stereo(&s_depth)) {
            if (!s_depth.time_verified) ++s_stats.async_accepted;
        } else if (s_link.async_test_enabled && !s_depth.time_verified) {
            ++s_stats.async_rejected;
            input_pose_cnn_discard_stereo_pending();
        }
    }
#endif
    now = local_time_us();
    if (!s_link.async_test_enabled && s_link.pairs &&
        now - s_depth_time > STEREO_LINK_MAX_AGE_US) {
        s_depth_ready = 0;
        input_pose_cnn_discard_stereo_pending();
    }
    if (now - s_report_time >= 1000000U) {
        unsigned index;
        int fresh = s_link.pairs && now >= s_depth_time &&
            now - s_depth_time <= STEREO_LINK_MAX_AGE_US;
        int available = s_link.pairs && (s_link.async_test_enabled || fresh);
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
                   "qL=%u qR=%u qdrop=%lu time_gate=%s; receipt timing NOT exposure sync\r\n",
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
            s_link.async_test_enabled ? "OFF_FIFO" : "STRICT");
    }
}

int stereo_board_on_result(const cnn_result_t *result, const StereoFrameMetadata *metadata)
{
    StereoCoordinateFrame frame;
    unsigned index;
    if (!s_ready || result == NULL) return 0;
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
#ifdef ROBOT_STEREO_LEFT
    if (s_pending_ready) ++s_stats.tx_dropped;
    if (!stereo_uart_encode(&frame, s_pending)) return 0;
    s_pending_ready = 1;
    service_tx();
    return 1;
#else
    return stereo_link_push(&s_link, 1, &frame, local_time_us());
#endif
}

int stereo_board_take_depth(StereoDepthResult *result)
{
    if (result == NULL || !s_depth_ready) return 0;
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
    stats->rejected_frames = s_link.rejected_frames;
    stats->expired_frames += s_link.expired_frames;
    stats->pairs = s_link.pairs;
    stats->unsynchronized_pairs = s_link.unsynchronized_pairs;
}

#else

int stereo_board_init(void) { return 0; }
int stereo_board_set_async_test(int enabled) { (void)enabled; return 0; }
int stereo_board_async_test_enabled(void) { return 0; }
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
