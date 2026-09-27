#include "cnn_weights.h"
#include "cnn_sha256.h"
#include "ff.h"
#include "xil_cache.h"
#include "xil_printf.h"
#include <string.h>

static FATFS weight_fs;
static int weight_fs_mounted;
static int weight_loaded;

static const u8 expected_digest[32]={
    0xc9,0x85,0x4b,0xb2,0xaa,0xd1,0x5b,0xad,
    0xde,0x4e,0xad,0x61,0x11,0x73,0x91,0x80,
    0x25,0xbf,0xf9,0x2e,0xe9,0x71,0xec,0xe3,
    0x0a,0x05,0xaf,0x8a,0xb0,0xa4,0xcf,0xee
};

static FRESULT open_weight_file(FIL *file,char *opened_path)
{
    FRESULT fr;
    DIR directory;
    FILINFO info;

    strcpy(opened_path,CNN_WEIGHT_PATH);
    fr=f_open(file,CNN_WEIGHT_PATH,FA_READ);
    if(fr==FR_OK) return FR_OK;
    xil_printf("CNN: open %s failed (%d)\r\n",CNN_WEIGHT_PATH,(int)fr);

#if FF_USE_LFN
    strcpy(opened_path,CNN_WEIGHT_LONG_PATH);
    fr=f_open(file,CNN_WEIGHT_LONG_PATH,FA_READ);
    if(fr==FR_OK) return FR_OK;
    xil_printf("CNN: open %s failed (%d)\r\n",CNN_WEIGHT_LONG_PATH,(int)fr);
#endif

    fr=f_opendir(&directory,"0:/");
    if(fr!=FR_OK) return fr;
    for(;;) {
        fr=f_readdir(&directory,&info);
        if(fr!=FR_OK || info.fname[0]=='\0') break;
        if((info.fattrib&AM_DIR)!=0U) continue;
        xil_printf("CNN: SD file %-12s %lu bytes\r\n",info.fname,(unsigned long)info.fsize);
        if((u32)info.fsize!=CNN_WEIGHT_BYTES) continue;
        opened_path[0]='0'; opened_path[1]=':'; opened_path[2]='/';
        strcpy(&opened_path[3],info.fname);
        fr=f_open(file,opened_path,FA_READ);
        if(fr==FR_OK) {
            f_closedir(&directory);
            xil_printf("CNN: discovered weight candidate by exact size\r\n");
            return FR_OK;
        }
    }
    f_closedir(&directory);
    return fr==FR_OK?FR_NO_FILE:fr;
}

cnn_error_t cnn_sd_mount(void)
{
    FRESULT fr;

    if(weight_fs_mounted)
        return CNN_OK;
    fr=f_mount(&weight_fs,"0:/",1);
    if(fr!=FR_OK) {
        xil_printf("CNN: SD mount failed (%d)\r\n",(int)fr);
        return CNN_ERR_SD_MOUNT;
    }
    weight_fs_mounted=1;
    return CNN_OK;
}

cnn_error_t cnn_weights_load_from_sd(void)
{
    FIL file;
    FRESULT fr;
    char opened_path[20];
    UINT got;
    u32 total=0;
    u8 digest[32];

    weight_loaded=0;
    if(cnn_sd_mount()!=CNN_OK)
        return CNN_ERR_SD_MOUNT;
    fr=open_weight_file(&file,opened_path);
    if(fr!=FR_OK) {
        xil_printf("CNN: no %lu-byte weight file found in SD root (%d)\r\n",
                   (unsigned long)CNN_WEIGHT_BYTES,(int)fr);
        return CNN_ERR_WEIGHT_OPEN;
    }
    xil_printf("CNN: opened %s\r\n",opened_path);
    if((u32)f_size(&file)!=CNN_WEIGHT_BYTES) {
        xil_printf("CNN: weight size %lu, expected %lu\r\n",
                   (unsigned long)f_size(&file),(unsigned long)CNN_WEIGHT_BYTES);
        f_close(&file); return CNN_ERR_WEIGHT_SIZE;
    }
    while(total<CNN_WEIGHT_BYTES) {
        UINT chunk=(UINT)((CNN_WEIGHT_BYTES-total)>65536U?65536U:(CNN_WEIGHT_BYTES-total));
        fr=f_read(&file,(void *)(UINTPTR)(CNN_WEIGHT_ADDRESS+total),chunk,&got);
        if(fr!=FR_OK || got!=chunk) { f_close(&file); return CNN_ERR_WEIGHT_READ; }
        total+=(u32)got;
    }
    f_close(&file);
    cnn_sha256((const void *)(UINTPTR)CNN_WEIGHT_ADDRESS,CNN_WEIGHT_BYTES,digest);
    if(!cnn_sha256_equal(digest,expected_digest)) return CNN_ERR_WEIGHT_SHA;
    Xil_DCacheFlushRange((INTPTR)CNN_WEIGHT_ADDRESS,CNN_WEIGHT_BYTES);
    weight_loaded=1;
    return CNN_OK;
}

int cnn_weights_are_loaded(void)
{
    return weight_loaded;
}
