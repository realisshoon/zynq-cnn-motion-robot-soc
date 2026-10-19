#ifndef CNN_DIAG_H
#define CNN_DIAG_H

#include "cnn_types.h"

void cnn_diag_probe_report(void);
void cnn_diag_capture(cnn_debug_snapshot_t *snapshot);
void cnn_diag_dump(void);
void cnn_diag_print_result(const cnn_result_t *result);
void cnn_diag_print_sg(u32 descriptor_base);

#endif
