#ifndef INTEGRATION_CNN_APP_H
#define INTEGRATION_CNN_APP_H

/* Initialize all CNN camera, HDMI, overlay, tracker, logging and console paths. */
int cnn_app_init(void);
/* Foreground service: never waits for CNN completion. */
void cnn_app_service(void);

#endif
