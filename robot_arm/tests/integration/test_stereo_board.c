#include "integration/stereo_board.h"
#include "integration/platform_vitis.h"
#include "integration/input_pose.h"
#include "xuartps.h"
#include "xuartps_hw.h"
#include "xtime_l.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static XScuGic fake_gic;
static XUartPs_Config configs[2] = {{0, 0xE0000000U}, {1, 0xE0001000U}};
static Xil_InterruptHandler irq_handler;
static void *irq_reference;
static uint8_t rx_bytes[10000], tx_bytes[10000];
static unsigned rx_count, rx_read, tx_count, tx_budget;
static uint32_t irq_status;
static XTime now = 1000000;
static char last_report[2048];

void XTime_GetTime(XTime *time) { *time = now; }
int xil_printf(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    if (strncmp(format, "[ST]", 4) == 0)
        vsnprintf(last_report, sizeof(last_report), format, arguments);
    va_end(arguments);
    return 0;
}
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
    assert(format->BaudRate == 115200);
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
    (void)gic;
    assert(interrupt == 59 && priority == 0xA0 && trigger == 1);
}
void XScuGic_Enable(XScuGic *gic, u32 interrupt) { (void)gic; assert(interrupt == 59); }
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

#ifdef ROBOT_STEREO_RIGHT
static void inject(const uint8_t *bytes, unsigned length)
{
    assert(length < sizeof(rx_bytes));
    memcpy(rx_bytes, bytes, length);
    rx_count = length;
    rx_read = 0;
    irq_status = XUARTPS_IXR_RXOVR | XUARTPS_IXR_TOUT;
    while (rx_read < rx_count) irq_handler(irq_reference);
}
#endif

int main(void)
{
    cnn_result_t local;
    StereoBoardStats stats;
    unsigned index;
    memset(&local, 0, sizeof(local));
    local.frame_id = 1;
    local.result_seq = 20;
    for (index = 0; index < CNN_JOINT_COUNT; ++index)
        local.joint[index] = (cnn_joint_t){750, 350, 50, 1};
    input_pose_init();
    assert(stereo_board_init() == 0 && irq_handler != NULL);
#ifdef ROBOT_STEREO_LEFT
    {
        assert(!stereo_board_set_async_test(1));
        assert(!stereo_board_async_test_enabled());
        StereoUartParser parser;
        StereoCoordinateFrame decoded;
        unsigned count = 0;
        tx_budget = 7;
        assert(stereo_board_on_result(&local, NULL));
        local.frame_id = 2;
        assert(stereo_board_on_result(&local, NULL));
        local.frame_id = 3;
        assert(stereo_board_on_result(&local, NULL));
        for (index = 0; index < 100; ++index) {
            tx_budget = 7;
            stereo_board_service();
        }
        assert(tx_count == 2 * STEREO_UART_PACKET_BYTES);
        stereo_uart_parser_init(&parser);
        for (index = 0; index < tx_count; ++index)
            if (stereo_uart_parser_push(&parser, tx_bytes[index], &decoded)) {
                ++count;
                assert(decoded.frame_id == (count == 1 ? 1U : 3U));
                assert(decoded.joint[16].x == 750 && decoded.result_seq == 20);
                assert(!decoded.metadata.exposure_time_verified);
            }
        assert(count == 2 && !input_pose_ready());
        stereo_board_stats(&stats);
        assert(stats.tx_frames == 2 && stats.tx_dropped == 1);
    }
#else
    {
        StereoCoordinateFrame remote;
        StereoDepthResult depth;
        uint8_t packet[STEREO_UART_PACKET_BYTES], noise[5000];
        memset(&remote, 0, sizeof(remote));
        remote.session_id = 99;
        remote.sequence = 1;
        remote.frame_id = 555;
        for (index = 0; index < STEREO_UART_JOINTS; ++index)
            remote.joint[index] = (StereoCoordinateJoint){900, 350, 50, 1};
        assert(stereo_uart_encode(&remote, packet));
        inject(packet, sizeof(packet));
        assert(stereo_board_on_result(&local, NULL));
        stereo_board_service();
        assert(stereo_board_take_depth(&depth));
        assert(depth.left_frame_id == 555 && depth.right_frame_id == 1);
        assert(!depth.time_verified && depth.control_status == STEREO_POSE_UNVERIFIED);
        assert(!input_pose_ready());
        assert(stereo_board_set_async_test(1));
        assert(stereo_board_async_test_enabled());
        assert(!stereo_board_take_depth(&depth));
        assert(stereo_board_set_async_test(0));
        assert(!stereo_board_async_test_enabled());
        packet[80] ^= 1;
        inject(packet, sizeof(packet));
        stereo_board_service();
        stereo_board_stats(&stats);
        assert(stats.crc_errors == 1 && stats.pairs == 1);
        memset(noise, 0x77, sizeof(noise));
        inject(noise, sizeof(noise));
        stereo_board_service();
        stereo_board_stats(&stats);
        assert(stats.rx_overflows > 0);
        remote.sequence = 2;
        remote.frame_id = 556;
        assert(stereo_uart_encode(&remote, packet));
        inject(packet, sizeof(packet));
        local.frame_id = 2;
        assert(stereo_board_on_result(&local, NULL));
        stereo_board_service();
        assert(stereo_board_take_depth(&depth) && depth.left_frame_id == 556);
        remote.sequence = 3;
        assert(stereo_uart_encode(&remote, packet));
        inject(packet, sizeof(packet));
        now += 300000;
        stereo_board_service();
        stereo_board_stats(&stats);
        assert(stats.expired_frames > 0 && stats.pairs == 2);
        assert(!input_pose_ready());
        assert(stereo_board_set_async_test(1));
        remote.sequence = 4;
        remote.frame_id = 557;
        assert(stereo_uart_encode(&remote, packet));
        inject(packet, sizeof(packet));
        local.frame_id = 3;
        assert(stereo_board_on_result(&local, NULL));
        now += 1000000;
        stereo_board_service();
        now += 1000000;
        stereo_board_service();
        assert(!stereo_board_take_depth(&depth));
        stereo_board_stats(&stats);
        assert(stats.pairs == 2 && stats.expired_frames >= 3);
        assert(strstr(last_report, "time_gate=RECEIPT_LATEST") != NULL);
        assert(strstr(last_report, "fresh=0 sync=0") != NULL);
        assert(strstr(last_report, "stale=") != NULL && strstr(last_report, "gap=") != NULL);
        assert(strstr(last_report, "skip=") != NULL && strstr(last_report, "marker_bad=") != NULL);
        for (index = 0; index < 3; ++index) {
            now += 20000;
            remote.sequence = 5 + index;
            remote.frame_id = 558 + index;
            assert(stereo_uart_encode(&remote, packet));
            inject(packet, sizeof(packet));
            local.frame_id = 4 + index;
            assert(stereo_board_on_result(&local, NULL));
        }
        stereo_board_service();
        assert(stereo_board_take_depth(&depth));
        assert(depth.left_frame_id == 560 && depth.right_frame_id == 6);
        assert(depth.async_test && !depth.time_verified);
        stereo_board_stats(&stats);
        assert(stats.pairs == 3);
        now += 20000;
        remote.sequence = 8;
        remote.frame_id = 561;
        assert(stereo_uart_encode(&remote, packet));
        inject(packet, sizeof(packet));
        local.frame_id = 7;
        assert(stereo_board_on_result(&local, NULL));
        stereo_board_service();
        now += STEREO_LINK_MAX_AGE_US + 1U;
        assert(!stereo_board_take_depth(&depth));
        stereo_board_service();
        assert(!input_pose_ready());
        remote.sequence = 9;
        remote.frame_id = 562;
        assert(stereo_uart_encode(&remote, packet));
        inject(packet, sizeof(packet));
        now += STEREO_LINK_MAX_RECEIVE_GAP_US + 1U;
        local.frame_id = 8;
        assert(stereo_board_on_result(&local, NULL));
        stereo_board_service();
        assert(!stereo_board_take_depth(&depth));
        stereo_board_stats(&stats);
        assert(stats.pairs == 4);
        assert(stereo_board_set_async_test(0));
        assert(!stereo_board_take_depth(&depth));
    }
#endif
    puts("test_stereo_board: PASS");
    return 0;
}
