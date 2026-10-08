#ifndef HARNESS_XIL_PRINTF_H
#define HARNESS_XIL_PRINTF_H
int regression_printf(const char *format, ...);
#define xil_printf regression_printf
#endif
