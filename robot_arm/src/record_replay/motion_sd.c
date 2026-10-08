#include "record_replay/motion_sd.h"

#include <stdint.h>
#include <string.h>

#include "ff.h"

static MotionSample sd_samples[MOTION_RECORD_REPLAY_MAX_SAMPLES];

static uint32_t little_u32(const unsigned char *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint32_t payload_crc(const unsigned char *bytes, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFU;
    uint32_t index;
    unsigned bit;
    for (index = 0U; index < length; ++index) {
        crc ^= bytes[index];
        for (bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
    }
    return crc ^ 0xFFFFFFFFU;
}

MotionSdResult motion_sd_load_path(MotionRecordReplay *controller, const char *path)
{
    FIL file;
    unsigned char header[24];
    UINT received = 0U;
    uint32_t count, length;
    MotionSdResult result = MOTION_SD_IO;
    if (controller == NULL || path == NULL || controller->mode != MOTION_RR_LIVE)
        return MOTION_SD_BUSY;
    controller->replay_count = 0U;
    controller->replay_index = 0U;
    if (f_open(&file, path, FA_READ) != FR_OK)
        return MOTION_SD_IO;
    if (f_read(&file, header, sizeof(header), &received) != FR_OK ||
        received != sizeof(header)) goto close_file;
    count = little_u32(header + 12);
    if (memcmp(header, "MRP1", 4U) != 0 ||
        little_u32(header + 4) != 1U || little_u32(header + 8) != 20000U ||
        little_u32(header + 16) != 0x77D4E3BBU || count == 0U ||
        count > MOTION_RECORD_REPLAY_MAX_SAMPLES) {
        result = MOTION_SD_FORMAT;
        goto close_file;
    }
    length = count * (uint32_t)sizeof(MotionSample);
    if (f_size(&file) != sizeof(header) + length) {
        result = MOTION_SD_FORMAT;
        goto close_file;
    }
    if (f_read(&file, sd_samples, length, &received) != FR_OK ||
        received != length) goto close_file;
    if (payload_crc((const unsigned char *)sd_samples, length) !=
        little_u32(header + 20)) {
        result = MOTION_SD_CRC;
        goto close_file;
    }
    result = MOTION_SD_OK;
close_file:
    if (f_close(&file) != FR_OK) result = MOTION_SD_IO;
    if (result != MOTION_SD_OK) return result;
    if (!motion_record_replay_load_replay(controller, sd_samples, count))
        return MOTION_SD_BUSY;
    controller->reason = motion_record_replay_validate_replay(controller);
    if (controller->reason != MOTION_RR_REASON_NONE) {
        controller->replay_count = 0U;
        return MOTION_SD_UNSAFE;
    }
    return MOTION_SD_OK;
}

MotionSdResult motion_sd_load(MotionRecordReplay *controller)
{
    return motion_sd_load_path(controller, MOTION_SD_PATH);
}

const char *motion_sd_result_name(MotionSdResult result)
{
    switch (result) {
    case MOTION_SD_OK: return "READY";
    case MOTION_SD_IO: return "IO_ERROR";
    case MOTION_SD_FORMAT: return "FORMAT_ERROR";
    case MOTION_SD_CRC: return "CRC_ERROR";
    case MOTION_SD_UNSAFE: return "UNSAFE";
    default: return "BUSY";
    }
}
