#ifndef RECORD_REPLAY_MOTION_SD_H
#define RECORD_REPLAY_MOTION_SD_H

#include "record_replay/motion_record_replay.h"

#define MOTION_SD_PATH "0:/MOTION.BIN"

typedef enum {
    MOTION_SD_OK = 0,
    MOTION_SD_IO,
    MOTION_SD_FORMAT,
    MOTION_SD_CRC,
    MOTION_SD_UNSAFE,
    MOTION_SD_BUSY
} MotionSdResult;

MotionSdResult motion_sd_load(MotionRecordReplay *controller);
MotionSdResult motion_sd_load_path(MotionRecordReplay *controller, const char *path);
const char *motion_sd_result_name(MotionSdResult result);

#endif
