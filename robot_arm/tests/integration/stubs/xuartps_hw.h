#ifndef TEST_XUARTPS_HW_H
#define TEST_XUARTPS_HW_H
#include "xil_types.h"
#define XUARTPS_FIFO_OFFSET 0U
int XUartPs_IsReceiveData(UINTPTR base);
u8 XUartPs_ReadReg(UINTPTR base, u32 offset);
u8 XUartPs_RecvByte(UINTPTR base);
#endif
