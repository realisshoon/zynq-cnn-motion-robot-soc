#ifndef INTEGRATION_PLATFORM_VITIS_H
#define INTEGRATION_PLATFORM_VITIS_H

#include "xscugic.h"

/* The timer and CNN completion interrupt register on this one GIC. */
XScuGic *platform_vitis_gic(void);

#endif
