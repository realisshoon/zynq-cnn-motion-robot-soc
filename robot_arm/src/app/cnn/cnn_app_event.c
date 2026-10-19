#include "app/cnn/cnn_app.h"

#include <stddef.h>
#include <string.h>

#include "cnn_app_event.h"

/* UART 입력과 main consumer는 같은 foreground loop에서 순서대로 실행된다. */
static CnnAppEvent pending_event;
static uint32_t pending_virtual_session, consumed_virtual_session;
static char virtual_frame[32];
static unsigned virtual_length;
static int virtual_active, virtual_bad;

int cnn_app_virtual_frame_active(void) { return virtual_active; }
void cnn_app_virtual_frame_abort(void) { virtual_active = virtual_bad = 0; virtual_length = 0; }
uint32_t cnn_app_virtual_request_session(void) { return consumed_virtual_session; }

int cnn_app_virtual_frame_feed(uint8_t byte, int menu_active)
{
    unsigned op, i;
    uint32_t nonce = 0;
    if (byte == '^') {
        virtual_active = 1; virtual_bad = menu_active != 0; virtual_length = 0;
        return 1;
    }
    if (!virtual_active) return 0;
    if (byte != '\r' && byte != '\n') {
        if (virtual_length + 1U >= sizeof(virtual_frame)) virtual_bad = 1;
        else virtual_frame[virtual_length++] = (char)byte;
        return 1;
    }
    virtual_frame[virtual_length] = 0;
    virtual_active = 0;
    if (virtual_bad || virtual_length < 6U || memcmp(virtual_frame,"VC,",3) ||
        virtual_frame[3] < '0' || virtual_frame[3] > '3' || virtual_frame[4] != ',') return 1;
    op = (unsigned)(virtual_frame[3] - '0');
    for (i=5; i<virtual_length; ++i) {
        unsigned digit = (unsigned)(virtual_frame[i] - '0');
        if (digit > 9U || nonce > (UINT32_MAX - digit)/10U) return 1;
        nonce = nonce*10U + digit;
    }
    if ((!nonce && op != 3U) || pending_event != CNN_APP_EVENT_NONE) return 1;
    pending_virtual_session = nonce;
    pending_event = op == 1U ? CNN_APP_EVENT_VIRTUAL_START : op == 2U ? CNN_APP_EVENT_VIRTUAL_RENEW :
        op == 0U ? CNN_APP_EVENT_VIRTUAL_STOP : CNN_APP_EVENT_VIRTUAL_STATUS;
    return 1;
}

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
        if (byte != '!' && byte != '~' && byte != '@' && byte != '^') return CNN_APP_UART_DROP;
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
    pending_virtual_session = consumed_virtual_session = 0;
    cnn_app_virtual_frame_abort();
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
    consumed_virtual_session = pending_virtual_session;
    pending_virtual_session = 0;
    pending_event = CNN_APP_EVENT_NONE;
    return 1;
}
