#include "cnn_frame_sg.h"
#include "xil_cache.h"

#define BD_SOF 0x08000000U
#define BD_EOF 0x04000000U

static volatile u32 *bd_word(u32 descriptor_base, u32 index)
{
    return (volatile u32 *)(UINTPTR)(descriptor_base+index*CNN_SG_BD_BYTES);
}

cnn_error_t cnn_sg_build(u32 descriptor_base, u32 frame_base)
{
    u32 i,j;
    if((descriptor_base&0x3FU)||(frame_base&0x3FU)) return CNN_ERR_BAD_ALIGNMENT;
    for(i=0;i<CNN_SG_BD_COUNT;++i) {
        volatile u32 *bd=bd_word(descriptor_base,i);
        u32 next=(i+1U<CNN_SG_BD_COUNT)?descriptor_base+(i+1U)*CNN_SG_BD_BYTES:descriptor_base;
        u32 buffer=frame_base+i*CNN_ROW_STEP*CNN_FRAME_STRIDE;
        for(j=0;j<16U;++j) bd[j]=0;
        bd[0]=next;
        bd[2]=buffer;
        bd[6]=CNN_FRAME_STRIDE|BD_SOF|BD_EOF;
    }
    Xil_DCacheFlushRange((INTPTR)descriptor_base,CNN_SG_BD_COUNT*CNN_SG_BD_BYTES);
    return cnn_sg_validate(descriptor_base,frame_base);
}

cnn_error_t cnn_sg_validate(u32 descriptor_base, u32 frame_base)
{
    u32 i;
    for(i=0;i<CNN_SG_BD_COUNT;++i) {
        volatile u32 *bd=bd_word(descriptor_base,i);
        u32 expected_next=(i+1U<CNN_SG_BD_COUNT)?descriptor_base+(i+1U)*CNN_SG_BD_BYTES:descriptor_base;
        u32 expected_buffer=frame_base+i*CNN_ROW_STEP*CNN_FRAME_STRIDE;
        if(bd[0]!=expected_next || bd[1]!=0U || bd[2]!=expected_buffer || bd[3]!=0U ||
           bd[6]!=(CNN_FRAME_STRIDE|BD_SOF|BD_EOF) || bd[7]!=0U)
            return CNN_ERR_SG_DESCRIPTOR;
    }
    return CNN_OK;
}

void cnn_sg_clear_status(u32 descriptor_base)
{
    u32 i;
    for(i=0;i<CNN_SG_BD_COUNT;++i) bd_word(descriptor_base,i)[7]=0U;
    Xil_DCacheFlushRange((INTPTR)descriptor_base,CNN_SG_BD_COUNT*CNN_SG_BD_BYTES);
}
