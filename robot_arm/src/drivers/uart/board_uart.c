#include "drivers/uart/board_uart.h"
#include "drivers/system/platform_vitis.h"
#include "firmware_config.h"
#include "xparameters.h"
#include "xparameters_ps.h"
#include "xstatus.h"
#include "xtime_l.h"
#include "xuartps.h"
#include "xuartps_hw.h"

#include <stddef.h>

#define RX_RING_SIZE 4096U
#define RX_IRQ_MASK (XUARTPS_IXR_RXOVR | XUARTPS_IXR_TOUT | XUARTPS_IXR_OVER | \
                     XUARTPS_IXR_FRAMING | XUARTPS_IXR_PARITY)
#define RX_ERROR_MASK (XUARTPS_IXR_OVER | XUARTPS_IXR_FRAMING | XUARTPS_IXR_PARITY)

static XUartPs s_uart;
static BoardUartByte s_rx[RX_RING_SIZE];
static volatile uint32_t s_head, s_tail, s_rx_overflows, s_rx_hw_errors;

uint64_t board_uart_time_us(void)
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
    uint64_t received = board_uart_time_us();
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

int board_uart_configure(void)
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
    s_head = s_tail = s_rx_overflows = s_rx_hw_errors = 0;
    return 0;
}

int board_uart_connect(void)
{
    if (XScuGic_Connect(platform_vitis_gic(), XPAR_PS7_UART_0_INTR,
        (Xil_InterruptHandler)receive_isr, NULL) != XST_SUCCESS) return -1;
    XScuGic_SetPriorityTriggerType(platform_vitis_gic(), XPAR_PS7_UART_0_INTR, 0xA0, 1);
    return 0;
}

void board_uart_start(void)
{
    XUartPs_WriteReg(s_uart.Config.BaseAddress, XUARTPS_ISR_OFFSET, XUARTPS_IXR_MASK);
    XScuGic_Enable(platform_vitis_gic(), XPAR_PS7_UART_0_INTR);
    XUartPs_SetInterruptMask(&s_uart, RX_IRQ_MASK);
}

int board_uart_read(BoardUartByte *received)
{
    if (s_tail == s_head) return 0;
    __sync_synchronize();
    *received = s_rx[s_tail & (RX_RING_SIZE - 1U)];
    __sync_synchronize();
    ++s_tail;
    return 1;
}

void board_uart_discard_rx(void)
{
    s_tail = s_head;
}

uint32_t board_uart_rx_overflow_count(void) { return s_rx_overflows; }
uint32_t board_uart_rx_error_count(void) { return s_rx_hw_errors; }

int board_uart_tx_ready(void)
{
    return !XUartPs_IsTransmitFull(s_uart.Config.BaseAddress);
}

void board_uart_write(uint8_t byte)
{
    XUartPs_WriteReg(s_uart.Config.BaseAddress, XUARTPS_FIFO_OFFSET, byte);
}
