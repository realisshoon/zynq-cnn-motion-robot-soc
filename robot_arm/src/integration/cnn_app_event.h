#ifndef INTEGRATION_CNN_APP_EVENT_H
#define INTEGRATION_CNN_APP_EVENT_H

/* cnn_app.c 내부 생산자 경계. Record/Replay controller에는 의존하지 않는다. */
void cnn_app_control_event_reset(void);
int cnn_app_control_event_post_uart(char command);

#endif
