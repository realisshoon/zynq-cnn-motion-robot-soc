#ifndef CAMERA_TRACKING_APP_H
#define CAMERA_TRACKING_APP_H

#include "camera_gimbal_pwm.h"
#include "torso_tracker.h"

typedef struct {
    camera_gimbal_pwm_t gimbal;
    torso_tracker_t tracker;
    u32 last_reported_frame;
    u8 initialized;
} camera_tracking_app_t;

int camera_tracking_app_init(camera_tracking_app_t *app);
void camera_tracking_app_on_result(camera_tracking_app_t *app,
                                   const cnn_result_t *result);
void camera_tracking_app_service(camera_tracking_app_t *app);
int camera_tracking_app_handle_key(camera_tracking_app_t *app, char key);
void camera_tracking_app_print_help(void);
void camera_tracking_app_print_status(const camera_tracking_app_t *app);

#endif
