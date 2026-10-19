#include "cnn_weights.h"
#include "cnn_sha256.h"
#include "ff.h"
#include "xil_cache.h"
#include "xil_printf.h"
#include <string.h>

static FATFS weight_fs;
static int weight_fs_mounted;
static int weight_loaded;

static int hex_value(u8 character)
{
    if(character >= '0' && character <= '9') return character - '0';
    if(character >= 'a' && character <= 'f') return character - 'a' + 10;
    if(character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
}

static cnn_error_t read_expected_digest(u8 expected_digest[32])
{
    FIL file;
    FRESULT fr;
    FRESULT close_result;
    u8 text[78];
    UINT length;
    UINT got=0;
    unsigned int index;

    fr=f_open(&file,CNN_WEIGHT_SHA_PATH,FA_READ);
    if(fr!=FR_OK) {
        xil_printf("CNN: open %s failed (%d)\r\n",CNN_WEIGHT_SHA_PATH,(int)fr);
        return CNN_ERR_WEIGHT_SHA;
    }
    if(f_size(&file)<76U || f_size(&file)>sizeof(text)) {
        xil_printf("CNN: invalid %s size %lu\r\n",CNN_WEIGHT_SHA_PATH,
                   (unsigned long)f_size(&file));
        f_close(&file);
        return CNN_ERR_WEIGHT_SHA;
    }
    length=(UINT)f_size(&file);
    fr=f_read(&file,text,length,&got);
    close_result=f_close(&file);
    if(fr!=FR_OK || got!=length || close_result!=FR_OK) {
        xil_printf("CNN: invalid %s read (%d, %u bytes, close %d)\r\n",
                   CNN_WEIGHT_SHA_PATH,(int)fr,(unsigned int)got,(int)close_result);
        return CNN_ERR_WEIGHT_SHA;
    }
    if(text[64]!=' ' || text[65]!=' ' ||
       memcmp(&text[66],"WGT_V4.BIN",10U)!=0 ||
       (length==77U && text[76]!='\n') ||
       (length==78U && (text[76]!='\r' || text[77]!='\n'))) {
        xil_printf("CNN: invalid %s format\r\n",CNN_WEIGHT_SHA_PATH);
        return CNN_ERR_WEIGHT_SHA;
    }
    for(index=0;index<32U;++index) {
        int high=hex_value(text[index*2U]);
        int low=hex_value(text[index*2U+1U]);
        if(high<0 || low<0) {
            xil_printf("CNN: invalid %s hex at column %u\r\n",
                       CNN_WEIGHT_SHA_PATH,index*2U+1U);
            return CNN_ERR_WEIGHT_SHA;
        }
        expected_digest[index]=(u8)((high<<4)|low);
    }
    xil_printf("CNN: loaded expected SHA256 from %s\r\n",CNN_WEIGHT_SHA_PATH);
    return CNN_OK;
}

cnn_error_t cnn_sd_mount(void)
{
    FRESULT fr;

    if(weight_fs_mounted) return CNN_OK;
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
    cnn_error_t result;
    UINT got=0;
    u32 total=0;
    u8 digest[32];
    u8 expected_digest[32];

    weight_loaded=0;
    result=cnn_sd_mount();
    if(result!=CNN_OK) return result;
    result=read_expected_digest(expected_digest);
    if(result!=CNN_OK) return result;
    fr=f_open(&file,CNN_WEIGHT_PATH,FA_READ);
    if(fr!=FR_OK) {
        xil_printf("CNN: open %s failed (%d)\r\n",CNN_WEIGHT_PATH,(int)fr);
        return CNN_ERR_WEIGHT_OPEN;
    }
    xil_printf("CNN: opened %s\r\n",CNN_WEIGHT_PATH);
    if(f_size(&file)!=CNN_WEIGHT_BYTES) {
        xil_printf("CNN: weight size %lu, expected %lu\r\n",
                   (unsigned long)f_size(&file),(unsigned long)CNN_WEIGHT_BYTES);
        f_close(&file);
        return CNN_ERR_WEIGHT_SIZE;
    }
    while(total<CNN_WEIGHT_BYTES) {
        UINT chunk=(UINT)((CNN_WEIGHT_BYTES-total)>65536U?65536U:(CNN_WEIGHT_BYTES-total));
        fr=f_read(&file,(void *)(UINTPTR)(CNN_WEIGHT_ADDRESS+total),chunk,&got);
        if(fr!=FR_OK || got!=chunk) {
            xil_printf("CNN: weight read failed at %lu (%d, %u/%u bytes)\r\n",
                       (unsigned long)total,(int)fr,(unsigned int)got,(unsigned int)chunk);
            f_close(&file);
            return CNN_ERR_WEIGHT_READ;
        }
        total+=(u32)got;
    }
    fr=f_close(&file);
    if(fr!=FR_OK) {
        xil_printf("CNN: weight close failed (%d)\r\n",(int)fr);
        return CNN_ERR_WEIGHT_READ;
    }
    cnn_sha256((const void *)(UINTPTR)CNN_WEIGHT_ADDRESS,CNN_WEIGHT_BYTES,digest);
    if(!cnn_sha256_equal(digest,expected_digest)) {
        xil_printf("CNN: weight SHA256 mismatch with %s\r\n",CNN_WEIGHT_SHA_PATH);
        return CNN_ERR_WEIGHT_SHA;
    }
    Xil_DCacheFlushRange((INTPTR)CNN_WEIGHT_ADDRESS,CNN_WEIGHT_BYTES);
    weight_loaded=1;
    return CNN_OK;
}

int cnn_weights_are_loaded(void)
{
    return weight_loaded;
}
