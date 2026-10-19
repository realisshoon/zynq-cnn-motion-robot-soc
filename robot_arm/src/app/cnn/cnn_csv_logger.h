#ifndef CNN_CSV_LOGGER_H
#define CNN_CSV_LOGGER_H

#include "cnn_types.h"
#include "ff.h"
#include "xil_types.h"

#define CNN_CSV_SYNC_INTERVAL 10U
#define CNN_CSV_MAX_FILE_BYTES (32U * 1024U * 1024U)

typedef struct {
    FIL file;
    u64 start_ticks;
    u32 rows_written;
    u32 file_index;
    u32 last_error;
    u8 active;
    char path[20];
} cnn_csv_logger_t;

void cnn_csv_logger_init(cnn_csv_logger_t *logger);
int cnn_csv_logger_start(cnn_csv_logger_t *logger);
int cnn_csv_logger_write(cnn_csv_logger_t *logger,
                         const cnn_result_t *result);
int cnn_csv_logger_sync(cnn_csv_logger_t *logger);
void cnn_csv_logger_stop(cnn_csv_logger_t *logger);
void cnn_csv_logger_print_status(const cnn_csv_logger_t *logger);

#endif
