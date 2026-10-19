#include "cnn_csv_logger.h"
#include "cnn_weights.h"
#include "xtime_l.h"
#include "xil_printf.h"
#include <stdio.h>
#include <string.h>

#define CNN_CSV_FIRST_FILE_INDEX 1U
#define CNN_CSV_LAST_FILE_INDEX 9999U

static const char csv_header[] =
    "frame_id,time_sec,frame_valid,"
    "shoulder_l_x,shoulder_l_y,shoulder_l_valid,"
    "shoulder_r_x,shoulder_r_y,shoulder_r_valid,"
    "elbow_x,elbow_y,elbow_valid,"
    "wrist_x,wrist_y,wrist_valid,"
    "finger1_x,finger1_y,finger1_valid,"
    "finger2_x,finger2_y,finger2_valid\r\n";

static void unpack_marker(u32 word, u16 *x, u16 *y, u8 *valid)
{
    *x=(u16)(word&0x7ffU);
    *y=(u16)((word>>11)&0x3ffU);
    *valid=(u8)((word>>31)&1U);
    if(!*valid) {
        *x=0U;
        *y=0U;
    }
}

static u64 elapsed_microseconds(XTime start)
{
    XTime now;
    u64 ticks;

    XTime_GetTime(&now);
    ticks=(u64)(now-start);
    return (ticks/(u64)COUNTS_PER_SECOND)*1000000ULL+
           ((ticks%(u64)COUNTS_PER_SECOND)*1000000ULL)/
           (u64)COUNTS_PER_SECOND;
}

static int write_bytes(cnn_csv_logger_t *logger,
                       const void *data, UINT length)
{
    UINT written=0U;
    FRESULT fr=f_write(&logger->file,data,length,&written);

    if(fr!=FR_OK || written!=length) {
        logger->last_error=(u32)(fr!=FR_OK?fr:FR_DISK_ERR);
        return 0;
    }
    return 1;
}

static int open_next_file(cnn_csv_logger_t *logger)
{
    FILINFO info;
    FRESULT fr;
    u32 index;

    for(index=CNN_CSV_FIRST_FILE_INDEX;
        index<=CNN_CSV_LAST_FILE_INDEX;++index) {
        snprintf(logger->path,sizeof(logger->path),"0:/LOG%04lu.CSV",
                 (unsigned long)index);
        fr=f_stat(logger->path,&info);
        if(fr==FR_NO_FILE)
            break;
        if(fr!=FR_OK) {
            logger->last_error=(u32)fr;
            return 0;
        }
    }
    if(index>CNN_CSV_LAST_FILE_INDEX) {
        logger->last_error=(u32)FR_EXIST;
        return 0;
    }

    fr=f_open(&logger->file,logger->path,FA_WRITE|FA_CREATE_NEW);
    if(fr!=FR_OK) {
        logger->last_error=(u32)fr;
        return 0;
    }
    logger->file_index=index;
    logger->rows_written=0U;
    logger->active=1U;
    if(!write_bytes(logger,csv_header,(UINT)(sizeof(csv_header)-1U)) ||
       f_sync(&logger->file)!=FR_OK) {
        cnn_csv_logger_stop(logger);
        return 0;
    }
    xil_printf("CNN CSV logger: recording %s\r\n",logger->path);
    return 1;
}

void cnn_csv_logger_init(cnn_csv_logger_t *logger)
{
    if(logger==0)
        return;
    memset(logger,0,sizeof(*logger));
}

int cnn_csv_logger_start(cnn_csv_logger_t *logger)
{
    XTime now;

    if(logger==0)
        return 0;
    if(logger->active)
        return 1;
    if(cnn_sd_mount()!=CNN_OK) {
        logger->last_error=(u32)FR_NOT_READY;
        return 0;
    }
    XTime_GetTime(&now);
    logger->start_ticks=(u64)now;
    logger->last_error=0U;
    return open_next_file(logger);
}

int cnn_csv_logger_sync(cnn_csv_logger_t *logger)
{
    FRESULT fr;

    if(logger==0 || !logger->active)
        return 0;
    fr=f_sync(&logger->file);
    if(fr!=FR_OK) {
        logger->last_error=(u32)fr;
        return 0;
    }
    return 1;
}

void cnn_csv_logger_stop(cnn_csv_logger_t *logger)
{
    FRESULT sync_result;
    FRESULT close_result;

    if(logger==0 || !logger->active)
        return;
    sync_result=f_sync(&logger->file);
    close_result=f_close(&logger->file);
    if(sync_result!=FR_OK)
        logger->last_error=(u32)sync_result;
    else if(close_result!=FR_OK)
        logger->last_error=(u32)close_result;
    logger->active=0U;
    xil_printf("CNN CSV logger: closed %s after %lu row(s)\r\n",
               logger->path,(unsigned long)logger->rows_written);
}

int cnn_csv_logger_write(cnn_csv_logger_t *logger,
                         const cnn_result_t *result)
{
    const cnn_joint_t *left_shoulder;
    const cnn_joint_t *right_shoulder;
    const cnn_joint_t *elbow;
    const cnn_joint_t *wrist;
    u16 finger1_x,finger1_y,finger2_x,finger2_y;
    u8 finger1_valid,finger2_valid;
    u64 time_us;
    u32 time_s,time_fraction_us;
    char line[320];
    int length;

    if(logger==0 || result==0 || !logger->active)
        return 0;

    left_shoulder=&result->joint[CNN_JOINT_LEFT_SHOULDER];
    right_shoulder=&result->joint[CNN_JOINT_RIGHT_SHOULDER];
    /* The robot-team schema has one elbow/wrist chain. Use the right arm. */
    elbow=&result->joint[CNN_JOINT_RIGHT_ELBOW];
    wrist=&result->joint[CNN_JOINT_RIGHT_WRIST];
    unpack_marker(result->red_marker,&finger1_x,&finger1_y,&finger1_valid);
    unpack_marker(result->blue_marker,&finger2_x,&finger2_y,&finger2_valid);

    time_us=elapsed_microseconds((XTime)logger->start_ticks);
    time_s=(u32)(time_us/1000000ULL);
    time_fraction_us=(u32)(time_us%1000000ULL);
    length=snprintf(line,sizeof(line),
        "%lu,%lu.%06lu,1,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,"
        "%u,%u,%u,%u,%u,%u\r\n",
        (unsigned long)result->frame_id,
        (unsigned long)time_s,(unsigned long)time_fraction_us,
        left_shoulder->x,left_shoulder->y,left_shoulder->valid,
        right_shoulder->x,right_shoulder->y,right_shoulder->valid,
        elbow->x,elbow->y,elbow->valid,
        wrist->x,wrist->y,wrist->valid,
        finger1_x,finger1_y,finger1_valid,
        finger2_x,finger2_y,finger2_valid);
    if(length<=0 || (size_t)length>=sizeof(line)) {
        logger->last_error=(u32)FR_INVALID_PARAMETER;
        return 0;
    }

    if((u32)f_tell(&logger->file)+(u32)length>CNN_CSV_MAX_FILE_BYTES) {
        cnn_csv_logger_stop(logger);
        if(!open_next_file(logger))
            return 0;
    }
    if(!write_bytes(logger,line,(UINT)length))
        return 0;
    logger->rows_written++;
    if((logger->rows_written%CNN_CSV_SYNC_INTERVAL)==0U &&
       !cnn_csv_logger_sync(logger))
        return 0;
    return 1;
}

void cnn_csv_logger_print_status(const cnn_csv_logger_t *logger)
{
    if(logger==0)
        return;
    xil_printf("CNN CSV logger active=%u file=%s rows=%lu size=%lu error=%lu\r\n",
               logger->active,logger->path[0]?logger->path:"none",
               (unsigned long)logger->rows_written,
               (unsigned long)(logger->active?f_tell(&logger->file):0U),
               (unsigned long)logger->last_error);
}
