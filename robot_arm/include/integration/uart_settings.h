#ifndef INTEGRATION_UART_SETTINGS_H
#define INTEGRATION_UART_SETTINGS_H

#include <stdint.h>

#define UART_SETTINGS_FIELDS 18U
typedef struct { uint32_t value[UART_SETTINGS_FIELDS]; } UartSettingsValues;
typedef void (*UartSettingsCapture)(UartSettingsValues *values);
typedef int (*UartSettingsApply)(const UartSettingsValues *values);

void uart_settings_init(unsigned role, UartSettingsCapture capture, UartSettingsApply apply);
int uart_settings_valid(const UartSettingsValues *values, unsigned role);
int uart_settings_active(void);
void uart_settings_abort_input(void);
int uart_settings_feed(uint8_t byte, int blocked);
int uart_settings_service(int storage_safe);
void uart_settings_boot_restore(void);
void uart_settings_report_capability(void);

#endif
