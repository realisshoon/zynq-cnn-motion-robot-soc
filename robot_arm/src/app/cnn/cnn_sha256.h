#ifndef CNN_SHA256_H
#define CNN_SHA256_H

#include "xil_types.h"

void cnn_sha256(const void *data, u32 length, u8 digest[32]);
int cnn_sha256_equal(const u8 left[32], const u8 right[32]);

#endif
