#ifndef CNN_FRAME_SG_H
#define CNN_FRAME_SG_H

#include "cnn_types.h"

#define CNN_SG_ADDRESS       0x11200000U
#define CNN_SG_BD_COUNT      144U
#define CNN_SG_BD_BYTES      64U
#define CNN_IMAGE_WIDTH      1280U
#define CNN_IMAGE_HEIGHT     720U
#define CNN_PIXEL_BYTES      3U
#define CNN_FRAME_STRIDE     3840U
#define CNN_FRAME_BYTES      2764800U
#define CNN_ROW_STEP         5U

cnn_error_t cnn_sg_build(u32 descriptor_base, u32 frame_base);
cnn_error_t cnn_sg_validate(u32 descriptor_base, u32 frame_base);
void cnn_sg_clear_status(u32 descriptor_base);

#endif
