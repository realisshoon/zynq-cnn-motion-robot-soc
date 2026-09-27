#ifndef CNN_HW_H
#define CNN_HW_H

#include "cnn_types.h"

#define CNN_BASE_ADDRESS       0x43C60000U

#define CNN_REG_CONTROL        0x000U
#define CNN_REG_STATUS         0x004U
#define CNN_REG_THRESHOLD      0x008U
#define CNN_REG_JOINT0         0x018U
#define CNN_REG_JOINT_FLAGS    0x05CU
#define CNN_REG_RED_THRESHOLD  0x060U
#define CNN_REG_BLUE_THRESHOLD 0x064U
#define CNN_REG_RED_RESULT     0x068U
#define CNN_REG_BLUE_RESULT    0x06CU
#define CNN_REG_FRAME_ID       0x070U
#define CNN_REG_RESULT_SEQ     0x074U
#define CNN_REG_ERROR          0x078U
#define CNN_REG_IRQ_ENABLE     0x07CU
#define CNN_REG_CYCLE_COUNT    0x080U
#define CNN_REG_RESULT_FRAME   0x084U
#define CNN_REG_MIN_COUNT      0x088U
#define CNN_REG_WEIGHT_BASE    0x08CU
#define CNN_REG_FM_A_BASE      0x090U
#define CNN_REG_FM_B_BASE      0x094U
#define CNN_REG_SG_BASE        0x098U
#define CNN_REG_FRAME_BASE     0x09CU
#define CNN_REG_TIMEOUT        0x0A0U
#define CNN_REG_VERSION        0x0A4U
#define CNN_REG_FEATURES       0x0A8U
#define CNN_REG_PACK_ID        0x0ACU
#define CNN_REG_DEBUG_FAULT_LATCH 0x0B0U
#define CNN_REG_DEBUG_CONTEXT     0x0B4U
#define CNN_REG_DEBUG_TXN_ADDR    0x0B8U
#define CNN_REG_DEBUG_TXN_DATA    0x0BCU
#define CNN_REG_DEBUG_FAULT_LIVE  0x0C0U
#define CNN_REG_FM_FAULT_REASON    0x0C4U
#define CNN_REG_FM_READ_INPUT      0x0C8U
#define CNN_REG_FM_READ_PARSED     0x0CCU
#define CNN_REG_FM_WRITE_INPUT     0x0D0U
#define CNN_REG_FM_WRITE_PACKED    0x0D4U
#define CNN_REG_FM_WRITE_OUTPUT    0x0D8U
#define CNN_REG_FM_STREAM_STATUS   0x0DCU
#define CNN_REG_FM_BODY_TAG_LO     0x0E0U
#define CNN_REG_FM_BODY_TAG_HI     0x0E4U
#define CNN_REG_FM_PROTOCOL_MISC   0x0E8U
#define CNN_REG_FM_EXPECTED_SRC    0x0ECU
#define CNN_REG_FM_EXPECTED_DST    0x0F0U
#define CNN_REG_GREEN_THRESHOLD    0x0F4U
#define CNN_REG_GREEN_RESULT       0x0F8U
#define CNN_REG_COLOR_ENABLE       0x0FCU
#define CNN_REG_COLOR_MARGIN       0x100U

/* Firmware power-up defaults. Change these values to tune color detection. */
#define CNN_DEFAULT_RED_MARGIN       50U
#define CNN_DEFAULT_GREEN_MARGIN     50U
#define CNN_DEFAULT_BLUE_MARGIN      50U

#define CNN_COLOR_ENABLE_RED       0x1U
#define CNN_COLOR_ENABLE_BLUE      0x2U
#define CNN_COLOR_ENABLE_GREEN     0x4U

#define CNN_DEBUG_CONTEXT_MARKER  0xD1000000U
#define CNN_DEBUG_CONTEXT_MASK    0xFF000000U

#define CNN_STATUS_DONE        0x00000001U
#define CNN_STATUS_BUSY        0x00000002U
#define CNN_STATUS_ERROR       0x00000004U
#define CNN_STATUS_IMAGE_DONE  0x00000008U

#define CNN_CONTROL_START      0x00000001U
#define CNN_CONTROL_CLEAR_DONE 0x00000002U
#define CNN_CONTROL_SOFT_RESET 0x00000004U

#define CNN_EXPECTED_VERSION   0x00040003U
#define CNN_EXPECTED_FEATURES  0x0000000DU
#define CNN_EXPECTED_PACK_ID   0xC9854BB2U

u32 cnn_hw_read(u32 offset);
void cnn_hw_write(u32 offset, u32 value);
cnn_error_t cnn_hw_probe(void);
u32 cnn_hw_status(void);
u32 cnn_hw_error(void);
void cnn_hw_clear_done(void);
void cnn_hw_clear_error(void);
void cnn_hw_soft_reset(void);
void cnn_hw_enable_irq(int enable);
cnn_error_t cnn_hw_set_green_detection(int enable);
int cnn_hw_green_detection_enabled(void);
cnn_error_t cnn_hw_set_color_margins(u8 red_margin, u8 green_margin,
                                     u8 blue_margin);
void cnn_hw_get_color_margins(u8 *red_margin, u8 *green_margin,
                              u8 *blue_margin);
cnn_error_t cnn_hw_initialize_color_defaults(void);
cnn_error_t cnn_hw_configure(u32 weight_base, u32 fm_a_base,
                             u32 fm_b_base, u32 sg_base,
                             u32 frame_base, u32 frame_id);
cnn_error_t cnn_hw_start(void);
cnn_error_t cnn_hw_read_result(cnn_result_t *result);

#endif
