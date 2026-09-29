#include "integration/cnn_app.h"

#include <stddef.h>

#include "cnn_app_event.h"

/* UART 입력과 main consumer는 같은 foreground loop에서 순서대로 실행된다. */
static CnnAppEvent pending_event;

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
