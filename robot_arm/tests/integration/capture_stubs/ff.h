#ifndef TEST_CAPTURE_FF_H
#define TEST_CAPTURE_FF_H
typedef unsigned int UINT;
typedef struct { int unused; } FIL;
typedef struct { unsigned int cursor; } DIR;
typedef struct { unsigned char fattrib; char fname[32]; } FILINFO;
typedef enum {
    FR_OK, FR_DISK_ERR, FR_NO_FILE, FR_EXIST, FR_INVALID_NAME, FR_DENIED, FR_NO_PATH
} FRESULT;
#define AM_DIR 0x10U
#define FA_WRITE 0x02U
#define FA_CREATE_NEW 0x04U
FRESULT f_mkdir(const char *path);
FRESULT f_stat(const char *path, FILINFO *info);
FRESULT f_open(FIL *file, const char *path, unsigned char mode);
FRESULT f_write(FIL *file, const void *bytes, UINT size, UINT *written);
FRESULT f_sync(FIL *file);
FRESULT f_close(FIL *file);
FRESULT f_unlink(const char *path);
FRESULT f_opendir(DIR *directory, const char *path);
FRESULT f_readdir(DIR *directory, FILINFO *info);
FRESULT f_closedir(DIR *directory);
#endif
