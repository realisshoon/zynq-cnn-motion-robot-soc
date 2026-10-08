#ifndef STEREO_FILTER_COMMAND_H
#define STEREO_FILTER_COMMAND_H

#include <stdint.h>

typedef enum {
    STEREO_FILTER_SHOW, STEREO_FILTER_DEFAULT, STEREO_FILTER_EMA,
    STEREO_FILTER_MIN, STEREO_FILTER_BETA, STEREO_FILTER_DERIVATIVE,
    STEREO_FILTER_KALMAN_XY, STEREO_FILTER_KALMAN_Z,
    STEREO_FILTER_KALMAN_ACCEL, STEREO_FILTER_KALMAN_VELOCITY
} StereoFilterAction;

typedef struct {
    StereoFilterAction action;
    uint32_t value;
} StereoFilterCommand;

typedef struct {
    char text[48];
    unsigned length;
    uint8_t active, blocked, invalid, skip_lf;
} StereoFilterCommandParser;

typedef enum {
    STEREO_FILTER_NOT_HANDLED, STEREO_FILTER_BUFFERED,
    STEREO_FILTER_COMPLETE, STEREO_FILTER_MALFORMED, STEREO_FILTER_MENU_ACTIVE
} StereoFilterCommandStatus;

StereoFilterCommandStatus stereo_filter_command_feed(StereoFilterCommandParser *parser,
    uint8_t byte, int menu_active, StereoFilterCommand *command);

#endif
