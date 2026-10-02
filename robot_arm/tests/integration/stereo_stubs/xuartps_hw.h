#ifndef TEST_STEREO_XUARTPS_HW_H
#define TEST_STEREO_XUARTPS_HW_H
#include "xil_types.h"
#define XUARTPS_ISR_OFFSET 0x14U
#define XUARTPS_CR_OFFSET 0U
#define XUARTPS_FIFO_OFFSET 0x30U
#define XUARTPS_IXR_RXOVR 1U
#define XUARTPS_IXR_TOUT 0x100U
#define XUARTPS_IXR_OVER 0x20U
#define XUARTPS_IXR_FRAMING 0x40U
#define XUARTPS_IXR_PARITY 0x80U
#define XUARTPS_IXR_MASK 0x3FFFU
#define XUARTPS_CR_TORST 0x40U
u32 XUartPs_ReadReg(UINTPTR base, u32 offset);
void XUartPs_WriteReg(UINTPTR base, u32 offset, u32 value);
int XUartPs_IsReceiveData(UINTPTR base);
int XUartPs_IsTransmitFull(UINTPTR base);
u8 XUartPs_RecvByte(UINTPTR base);
#endif
