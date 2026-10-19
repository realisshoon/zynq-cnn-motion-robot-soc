#ifndef DRIVERS_CONSOLE_UART_H
#define DRIVERS_CONSOLE_UART_H

#include <stdint.h>

int console_uart_init(void);
int console_uart_available(void);
uint8_t console_uart_read(void);
uint8_t console_uart_read_ready(void);
uint32_t console_uart_enqueue(const uint8_t *data, uint32_t len);
void platform_uart_service(void);
void platform_uart_set_output_enabled(int enabled);
int platform_uart_output_enabled(void);

#endif
