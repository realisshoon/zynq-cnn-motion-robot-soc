#ifndef DRIVERS_BOARD_UART_H
#define DRIVERS_BOARD_UART_H

#include <stdint.h>

typedef struct {
    uint64_t received_us;
    uint8_t byte;
} BoardUartByte;

int board_uart_configure(void);
int board_uart_connect(void);
void board_uart_start(void);
uint64_t board_uart_time_us(void);
int board_uart_read(BoardUartByte *received);
void board_uart_discard_rx(void);
uint32_t board_uart_rx_overflow_count(void);
uint32_t board_uart_rx_error_count(void);
int board_uart_tx_ready(void);
void board_uart_write(uint8_t byte);

#endif
