#ifndef INTEGRATION_CNN_CONSOLE_H
#define INTEGRATION_CNN_CONSOLE_H

#include "../cnn_firmware/camera_tracking/camera_tracking_app.h"

int cnn_console_active(void);
void cnn_console_start_color(void);
void cnn_console_start_joint(void);
void cnn_console_start_camera(camera_tracking_app_t *app);
void cnn_console_poll(void);

#endif
