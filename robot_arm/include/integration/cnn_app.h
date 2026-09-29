#ifndef INTEGRATION_CNN_APP_H
#define INTEGRATION_CNN_APP_H

/* 임시 UART 기본값. 메뉴와 테스트가 같은 정의를 사용하므로 나중에 이 두
 * 상수만 바꾸면 된다. 기존 메뉴는 소문자를 사용하며 대문자 R/P와 충돌하지 않는다. */
#define CNN_APP_UART_CMD_RECORD 'R'
#define CNN_APP_UART_CMD_PLAY   'P'

typedef enum {
    CNN_APP_EVENT_NONE = 0,
    CNN_APP_EVENT_RECORD_TOGGLE,
    CNN_APP_EVENT_PLAY_TOGGLE
} CnnAppEvent;

/* Initialize all CNN camera, HDMI, overlay, tracker, logging and console paths. */
int cnn_app_init(void);
/* Foreground service: never waits for CNN completion. */
void cnn_app_service(void);

/* UART 메뉴가 만든 software event를 한 번 가져오고 pending 값을 비운다. */
int cnn_app_take_control_event(CnnAppEvent *event);

/* main이 처리한 Record/Replay event 결과를 기존 UART TX 경로로 알린다.
 * 상태 전이는 main/RecordReplay 모듈이 담당하며 cnn_app은 문자열만 출력한다. */
void cnn_app_report_control_result(CnnAppEvent event,
                                   int accepted,
                                   const char *mode,
                                   const char *reason,
                                   unsigned long record_count,
                                   unsigned long replay_count,
                                   const char *record_source,
                                   const char *replay_source);

#endif
