#ifndef HARNESS_FF_H
#define HARNESS_FF_H
#include <stdint.h>
typedef unsigned int UINT;
typedef int FRESULT;
typedef struct { unsigned slot, position; uint32_t length; unsigned char mode; } FIL;
typedef struct { uint32_t fsize; } FILINFO;
#define FR_OK 0
#define FR_DISK_ERR 1
#define FR_NO_FILE 4
#define FR_NO_PATH 5
#define FR_EXIST 8
#define FA_READ 1
#define FA_WRITE 2
#define FA_CREATE_NEW 4
#define f_size(file) ((file)->length)
FRESULT f_open(FIL *file, const char *path, unsigned char mode);
FRESULT f_read(FIL *file, void *buffer, UINT length, UINT *received);
FRESULT f_write(FIL *file, const void *buffer, UINT length, UINT *written);
FRESULT f_close(FIL *file);
FRESULT f_sync(FIL *file);
FRESULT f_stat(const char *path, FILINFO *info);
FRESULT f_mkdir(const char *path);
FRESULT f_unlink(const char *path);
FRESULT f_rename(const char *source, const char *destination);
void fatfs_mock_reset(void);
int fatfs_mock_exists(const char *path);
const unsigned char *fatfs_mock_data(const char *path, unsigned *length);
extern int fail_write, fail_sync, fail_rename, fail_read, corrupt_write;
extern int fail_rename_after;
extern unsigned reads, writes, unsafe_storage_writes, invalid_storage_paths;
#endif
