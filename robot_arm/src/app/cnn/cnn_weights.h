#ifndef CNN_WEIGHTS_H
#define CNN_WEIGHTS_H

#include "cnn_types.h"

#define CNN_WEIGHT_PATH "0:/WGT_V4.BIN"
#define CNN_WEIGHT_SHA_PATH "0:/WGT_V4.SHA"
#define CNN_WEIGHT_ADDRESS 0x10000000U
#define CNN_WEIGHT_BYTES 1287680U

cnn_error_t cnn_weights_load_from_sd(void);
cnn_error_t cnn_sd_mount(void);
int cnn_weights_are_loaded(void);

#endif
