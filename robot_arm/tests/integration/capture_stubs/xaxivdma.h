#ifndef TEST_CAPTURE_XAXIVDMA_H
#define TEST_CAPTURE_XAXIVDMA_H
#include "xil_types.h"
typedef struct {
    unsigned int MaxNumFrames;
    struct { UINTPTR ChanBase; int IsValid; int FlushonFsync; } WriteChannel;
    UINTPTR BaseAddr;
} XAxiVdma;
#define XAXIVDMA_WRITE 1U
#define XST_SUCCESS 0
u32 XAxiVdma_CurrFrameStore(XAxiVdma *vdma, unsigned int direction);
void XAxiVdma_DmaStop(XAxiVdma *vdma, unsigned int direction);
int XAxiVdma_DmaStart(XAxiVdma *vdma, unsigned int direction);
u32 XAxiVdma_GetDmaChannelErrors(XAxiVdma *vdma, unsigned int direction);
int XAxiVdma_StartParking(XAxiVdma *vdma, int frame, unsigned int direction);
void XAxiVdma_StopParking(XAxiVdma *vdma, unsigned int direction);
#endif
