#include "drivers/uart/console_uart.h"
#include "firmware_config.h"
#include "xparameters.h"
#include "xstatus.h"
#include "xuartps.h"
#include "xuartps_hw.h"

#include <stddef.h>

#define PLATFORM_UART_DEVICE_ID   XPAR_PS7_UART_1_DEVICE_ID              /* PS UART1 (USB-UART) */
/* [TRACE] trace를 켠 빌드는 921600, 끈 빌드는 115200이다. PC 스크립트도 같은 속도로 열어야 한다(--baud). */
#define PLATFORM_UART_BAUD        ROBOT_CONSOLE_BAUD

static XUartPs s_uart;
#define PLATFORM_TX_RING_SIZE 32768U
static uint8_t s_uart_tx_ring[PLATFORM_TX_RING_SIZE];
static uint32_t s_uart_tx_head;
static uint32_t s_uart_tx_tail;
static int s_uart_ready;
static int s_uart_output_enabled = 1;

int console_uart_init(void)
{
    XUartPs_Config *uart_cfg;
    uart_cfg = XUartPs_LookupConfig(PLATFORM_UART_DEVICE_ID);
    if (uart_cfg == NULL ||
        uart_cfg->BaseAddress != 0xE0001000U ||
        STDIN_BASEADDRESS != 0xE0001000U || STDOUT_BASEADDRESS != 0xE0001000U ||
        XUartPs_CfgInitialize(&s_uart, uart_cfg, uart_cfg->BaseAddress) != XST_SUCCESS ||
        XUartPs_SetBaudRate(&s_uart, PLATFORM_UART_BAUD) != XST_SUCCESS) {
        return -1;
    }
    s_uart_tx_head = 0U;
    s_uart_tx_tail = 0U;
    s_uart_ready = 1;
    s_uart_output_enabled = 1;
    return 0;
}

void platform_uart_service(void)
{
    UINTPTR base;
    if (!s_uart_ready || !s_uart_output_enabled) return;
    base = s_uart.Config.BaseAddress;
    while (s_uart_tx_head != s_uart_tx_tail && !XUartPs_IsTransmitFull(base)) {
        XUartPs_WriteReg(base, XUARTPS_FIFO_OFFSET,
                        s_uart_tx_ring[s_uart_tx_tail & (PLATFORM_TX_RING_SIZE - 1U)]);
        ++s_uart_tx_tail;
    }
}

void platform_uart_set_output_enabled(int enabled)
{
    s_uart_output_enabled = enabled ? 1 : 0;
    if (!s_uart_output_enabled) {
        /* Drop queued text and TRACE bytes; hardware FIFO may finish sending
         * a few bytes already accepted before the mute command. */
        s_uart_tx_tail = s_uart_tx_head;
    }
}

int platform_uart_output_enabled(void)
{
    return s_uart_output_enabled;
}

void outbyte(char c)
{
    if (!s_uart_output_enabled) return;
    if (!s_uart_ready) {
        XUartPs_SendByte(STDOUT_BASEADDRESS, (u8)c);
        return;
    }
    while (s_uart_tx_head - s_uart_tx_tail == PLATFORM_TX_RING_SIZE)
        platform_uart_service();
    s_uart_tx_ring[s_uart_tx_head & (PLATFORM_TX_RING_SIZE - 1U)] = (uint8_t)c;
    ++s_uart_tx_head;
}

uint32_t console_uart_enqueue(const uint8_t *data, uint32_t len)
{
    uint32_t i;
    if (data == NULL) return 0U;
    if (!s_uart_output_enabled) return len;
    if (len > PLATFORM_TX_RING_SIZE -
                               (s_uart_tx_head - s_uart_tx_tail)) return 0U;
    for (i = 0U; i < len; ++i)
        s_uart_tx_ring[(s_uart_tx_head + i) & (PLATFORM_TX_RING_SIZE - 1U)] = data[i];
    s_uart_tx_head += len;
    return len;
}

int console_uart_available(void)
{
    return XUartPs_IsReceiveData(STDIN_BASEADDRESS);
}

uint8_t console_uart_read(void)
{
    return (uint8_t)XUartPs_RecvByte(STDIN_BASEADDRESS);
}

uint8_t console_uart_read_ready(void)
{
    return (uint8_t)XUartPs_ReadReg(STDIN_BASEADDRESS, XUARTPS_FIFO_OFFSET);
}
