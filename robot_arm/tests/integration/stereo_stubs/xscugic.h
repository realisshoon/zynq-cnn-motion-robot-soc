#ifndef TEST_STEREO_XSCUGIC_H
#define TEST_STEREO_XSCUGIC_H
#include "xil_types.h"
typedef struct { unsigned unused; } XScuGic;
typedef void (*Xil_InterruptHandler)(void *);
int XScuGic_Connect(XScuGic *gic, u32 interrupt, Xil_InterruptHandler handler, void *reference);
void XScuGic_SetPriorityTriggerType(XScuGic *gic, u32 interrupt, u8 priority, u8 trigger);
void XScuGic_Enable(XScuGic *gic, u32 interrupt);
#endif
