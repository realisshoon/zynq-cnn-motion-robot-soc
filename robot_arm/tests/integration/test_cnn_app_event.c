#include <assert.h>
#include <stdio.h>

#include "integration/cnn_app.h"
#include "../../src/integration/cnn_app_event.h"

int main(void)
{
    CnnAppEvent event = CNN_APP_EVENT_NONE;

    cnn_app_control_event_reset();
    assert(!cnn_app_take_control_event(&event));
    assert(!cnn_app_control_event_post_uart('r'));
    assert(cnn_app_control_event_post_uart(CNN_APP_UART_CMD_RECORD));
    assert(cnn_app_take_control_event(&event));
    assert(event == CNN_APP_EVENT_RECORD_TOGGLE);
    assert(!cnn_app_take_control_event(&event));

    assert(cnn_app_control_event_post_uart(CNN_APP_UART_CMD_PLAY));
    /* Pending 명령은 다음 명령으로 덮어쓰지 않는다. */
    assert(!cnn_app_control_event_post_uart(CNN_APP_UART_CMD_RECORD));
    assert(cnn_app_take_control_event(&event));
    assert(event == CNN_APP_EVENT_PLAY_TOGGLE);
    assert(!cnn_app_take_control_event(&event));

    puts("test_cnn_app_event: PASS");
    return 0;
}
