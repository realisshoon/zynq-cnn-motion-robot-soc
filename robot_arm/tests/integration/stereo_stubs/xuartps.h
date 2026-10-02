#ifndef TEST_STEREO_XUARTPS_H
#define TEST_STEREO_XUARTPS_H
#include "xil_types.h"
typedef struct { u16 DeviceId; UINTPTR BaseAddress; } XUartPs_Config;
typedef struct { XUartPs_Config Config; } XUartPs;
typedef struct { u32 BaudRate, DataBits, Parity; u8 StopBits; } XUartPsFormat;
#define XUARTPS_FORMAT_8_BITS 0U
#define XUARTPS_FORMAT_NO_PARITY 4U
#define XUARTPS_FORMAT_1_STOP_BIT 0U
#define XUARTPS_OPER_MODE_NORMAL 0U
XUartPs_Config *XUartPs_LookupConfig(u16 device);
int XUartPs_CfgInitialize(XUartPs *uart, XUartPs_Config *config, UINTPTR address);
int XUartPs_SetBaudRate(XUartPs *uart, u32 baud);
int XUartPs_SetDataFormat(XUartPs *uart, XUartPsFormat *format);
void XUartPs_SetInterruptMask(XUartPs *uart, u32 mask);
void XUartPs_SetOperMode(XUartPs *uart, u8 mode);
void XUartPs_SetFifoThreshold(XUartPs *uart, u8 threshold);
void XUartPs_SetRecvTimeout(XUartPs *uart, u8 timeout);
#endif
