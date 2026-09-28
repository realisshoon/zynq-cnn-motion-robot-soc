#ifndef INTEGRATION_PLATFORM_VITIS_H
#define INTEGRATION_PLATFORM_VITIS_H

#include "xscugic.h"

/* The timer and CNN completion interrupt register on this one GIC. */
XScuGic *platform_vitis_gic(void);
/* Mute only UART TX; RX commands, CNN, and robot control keep running. */
void platform_uart_set_output_enabled(int enabled);
int platform_uart_output_enabled(void);

#endif
