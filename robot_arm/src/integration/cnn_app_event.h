#ifndef INTEGRATION_CNN_APP_EVENT_H
#define INTEGRATION_CNN_APP_EVENT_H

#include <stdint.h>

#define CNN_APP_UART_FRAME_TIMEOUT_US 250000U

typedef struct {
    uint32_t last_byte_us;
    uint8_t have_byte, escape_pending, draining;
} CnnAppUartGuard;

typedef enum {
    CNN_APP_UART_FORWARD = 0,
    CNN_APP_UART_ABORT,
    CNN_APP_UART_STOP,
    CNN_APP_UART_DROP
} CnnAppUartAction;

int cnn_app_uart_guard_expire(CnnAppUartGuard *guard, uint32_t now_us, int partial_active);
CnnAppUartAction cnn_app_uart_guard_feed(CnnAppUartGuard *guard, uint8_t byte, uint32_t now_us);
int cnn_app_control_event_post_stop_uart(char command);

/* cnn_app.c 내부 생산자 경계. Record/Replay controller에는 의존하지 않는다. */
void cnn_app_control_event_reset(void);
int cnn_app_control_event_post_uart(char command);

#endif
