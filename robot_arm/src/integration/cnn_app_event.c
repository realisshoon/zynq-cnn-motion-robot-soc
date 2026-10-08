#include "integration/cnn_app.h"

#include <stddef.h>

#include "cnn_app_event.h"

/* UART 입력과 main consumer는 같은 foreground loop에서 순서대로 실행된다. */
static CnnAppEvent pending_event;

int cnn_app_uart_guard_expire(CnnAppUartGuard *guard, uint32_t now_us, int partial_active)
{
    if (guard == NULL || !guard->have_byte || (!partial_active && !guard->escape_pending) ||
        now_us - guard->last_byte_us <= CNN_APP_UART_FRAME_TIMEOUT_US) return 0;
    guard->escape_pending = guard->have_byte = 0U;
    guard->draining = 1U;
    return 1;
}

CnnAppUartAction cnn_app_uart_guard_feed(CnnAppUartGuard *guard, uint8_t byte, uint32_t now_us)
{
    if (guard == NULL) return CNN_APP_UART_DROP;
    guard->last_byte_us = now_us;
    guard->have_byte = 1U;
    if (byte == 27U) {
        guard->escape_pending = 1U;
        guard->draining = 0U;
        return CNN_APP_UART_ABORT;
    }
    if (guard->escape_pending) {
        guard->escape_pending = 0U;
        if (byte == CNN_APP_UART_CMD_PWM_DISABLE || byte == CNN_APP_UART_CMD_ASYNC_DISABLE)
            return CNN_APP_UART_STOP;
        guard->draining = byte != '\r' && byte != '\n';
        return CNN_APP_UART_DROP;
    }
    if (guard->draining) {
        if (byte == '\r' || byte == '\n') {
            guard->draining = 0U;
            return CNN_APP_UART_DROP;
        }
        if (byte == CNN_APP_UART_CMD_PWM_DISABLE || byte == CNN_APP_UART_CMD_ASYNC_DISABLE) {
            guard->draining = 0U;
            return CNN_APP_UART_STOP;
        }
        if (byte != '!' && byte != '~' && byte != '@') return CNN_APP_UART_DROP;
        guard->draining = 0U;
    }
    return CNN_APP_UART_FORWARD;
}

int cnn_app_control_event_post_stop_uart(char command)
{
    if (command == CNN_APP_UART_CMD_PWM_DISABLE) pending_event = CNN_APP_EVENT_PWM_DISABLE;
    else if (command == CNN_APP_UART_CMD_ASYNC_DISABLE) {
        if (pending_event != CNN_APP_EVENT_PWM_DISABLE) pending_event = CNN_APP_EVENT_ASYNC_DISABLE;
    } else return 0;
    return 1;
}

void cnn_app_control_event_reset(void)
{
    pending_event = CNN_APP_EVENT_NONE;
}

int cnn_app_control_event_post_uart(char command)
{
    CnnAppEvent next;

    if (command == CNN_APP_UART_CMD_RECORD) {
        next = CNN_APP_EVENT_RECORD_TOGGLE;
    } else if (command == CNN_APP_UART_CMD_PLAY) {
        next = CNN_APP_EVENT_PLAY_TOGGLE;
    } else if (command == CNN_APP_UART_CMD_PWM_ENABLE) {
        next = CNN_APP_EVENT_PWM_ENABLE;
    } else if (command == CNN_APP_UART_CMD_PWM_DISABLE) {
        next = CNN_APP_EVENT_PWM_DISABLE;
    } else if (command == CNN_APP_UART_CMD_PWM_STATUS) {
        next = CNN_APP_EVENT_PWM_STATUS;
    } else if (command == CNN_APP_UART_CMD_ASYNC_ENABLE) {
        next = CNN_APP_EVENT_ASYNC_ENABLE;
    } else if (command == CNN_APP_UART_CMD_ASYNC_DISABLE) {
        next = CNN_APP_EVENT_ASYNC_DISABLE;
    } else if (command == CNN_APP_UART_CMD_ASYNC_STATUS) {
        next = CNN_APP_EVENT_ASYNC_STATUS;
    } else if (command == CNN_APP_UART_CMD_GRIP_OPEN) {
        next = CNN_APP_EVENT_GRIP_OPEN;
    } else if (command == CNN_APP_UART_CMD_GRIP_AUTO) {
        next = CNN_APP_EVENT_GRIP_AUTO;
    } else {
        return 0;
    }

    /* 정상 main loop에서는 service 직후 consume한다. 혹시 pending이 남아 있으면
     * 먼저 받은 명령을 덮어쓰지 않는다. */
    if (pending_event != CNN_APP_EVENT_NONE) return 0;
    pending_event = next;
    return 1;
}

int cnn_app_take_control_event(CnnAppEvent *event)
{
    if (event == NULL || pending_event == CNN_APP_EVENT_NONE) return 0;
    *event = pending_event;
    pending_event = CNN_APP_EVENT_NONE;
    return 1;
}
