#include "cnn_hw.h"
#include "xil_io.h"

u32 cnn_hw_read(u32 offset)
{
    return Xil_In32((UINTPTR)(CNN_BASE_ADDRESS + offset));
}

void cnn_hw_write(u32 offset, u32 value)
{
    Xil_Out32((UINTPTR)(CNN_BASE_ADDRESS + offset), value);
}

cnn_error_t cnn_hw_probe(void)
{
    if (cnn_hw_read(CNN_REG_VERSION) != CNN_EXPECTED_VERSION)
        return CNN_ERR_BAD_VERSION;
    if (cnn_hw_read(CNN_REG_FEATURES) != CNN_EXPECTED_FEATURES)
        return CNN_ERR_BAD_VERSION;
    if (cnn_hw_read(CNN_REG_PACK_ID) != CNN_EXPECTED_PACK_ID)
        return CNN_ERR_BAD_PACK_ID;
    return CNN_OK;
}

u32 cnn_hw_status(void)
{
    return cnn_hw_read(CNN_REG_STATUS);
}

u32 cnn_hw_error(void)
{
    return cnn_hw_read(CNN_REG_ERROR);
}

void cnn_hw_clear_done(void)
{
    cnn_hw_write(CNN_REG_CONTROL, CNN_CONTROL_CLEAR_DONE);
}

void cnn_hw_clear_error(void)
{
    cnn_hw_write(CNN_REG_ERROR, 1U);
}

void cnn_hw_soft_reset(void)
{
    cnn_hw_write(CNN_REG_CONTROL, CNN_CONTROL_SOFT_RESET);
}

void cnn_hw_enable_irq(int enable)
{
    cnn_hw_write(CNN_REG_IRQ_ENABLE, enable ? 1U : 0U);
}

cnn_error_t cnn_hw_set_green_detection(int enable)
{
    u32 control = cnn_hw_get_color_enable();
    if (enable)
        control |= CNN_COLOR_ENABLE_GREEN;
    else
        control &= ~CNN_COLOR_ENABLE_GREEN;
    return cnn_hw_set_color_enable(control);
}

int cnn_hw_green_detection_enabled(void)
{
    return (cnn_hw_read(CNN_REG_COLOR_ENABLE) &
            CNN_COLOR_ENABLE_GREEN) ? 1 : 0;
}

cnn_error_t cnn_hw_set_color_enable(u32 mask)
{
    if (mask & ~CNN_COLOR_ENABLE_ALL)
        return CNN_ERR_ARGUMENT;
    if (cnn_hw_status() & (CNN_STATUS_BUSY | CNN_STATUS_ERROR))
        return CNN_ERR_BUSY;
    cnn_hw_write(CNN_REG_COLOR_ENABLE, mask);
    return CNN_OK;
}

u32 cnn_hw_get_color_enable(void)
{
    return cnn_hw_read(CNN_REG_COLOR_ENABLE) & CNN_COLOR_ENABLE_ALL;
}

cnn_error_t cnn_hw_set_color_margins(u8 red_margin, u8 green_margin,
                                     u8 blue_margin)
{
    u32 status = cnn_hw_status();
    u32 packed;

    if (status & (CNN_STATUS_BUSY | CNN_STATUS_ERROR))
        return CNN_ERR_BUSY;

    packed = ((u32)blue_margin << 16) |
             ((u32)green_margin << 8) |
             (u32)red_margin;
    cnn_hw_write(CNN_REG_COLOR_MARGIN, packed);
    return CNN_OK;
}

void cnn_hw_get_color_margins(u8 *red_margin, u8 *green_margin,
                              u8 *blue_margin)
{
    u32 packed = cnn_hw_read(CNN_REG_COLOR_MARGIN);

    if (red_margin != 0)
        *red_margin = (u8)(packed & 0xffU);
    if (green_margin != 0)
        *green_margin = (u8)((packed >> 8) & 0xffU);
    if (blue_margin != 0)
        *blue_margin = (u8)((packed >> 16) & 0xffU);
}

cnn_error_t cnn_hw_set_yellow_margins(u8 rg_delta_max, u8 blue_gap_min)
{
    if (cnn_hw_status() & (CNN_STATUS_BUSY | CNN_STATUS_ERROR))
        return CNN_ERR_BUSY;
    cnn_hw_write(CNN_REG_YELLOW_MARGIN,
                 ((u32)rg_delta_max << 8) | (u32)blue_gap_min);
    return CNN_OK;
}

cnn_error_t cnn_hw_set_color_thresholds(u8 red_min, u8 green_min,
                                        u8 blue_min, u8 yellow_r_min,
                                        u8 yellow_g_min, u8 yellow_b_max)
{
    u32 red, green, blue, yellow;
    if (cnn_hw_status() & (CNN_STATUS_BUSY | CNN_STATUS_ERROR))
        return CNN_ERR_BUSY;
    red = (cnn_hw_read(CNN_REG_RED_THRESHOLD) & 0x00FFFF00U) | red_min;
    green = (cnn_hw_read(CNN_REG_GREEN_THRESHOLD) & 0x00FF00FFU) |
            ((u32)green_min << 8);
    blue = (cnn_hw_read(CNN_REG_BLUE_THRESHOLD) & 0x0000FFFFU) |
           ((u32)blue_min << 16);
    yellow = ((u32)yellow_b_max << 16) |
             ((u32)yellow_g_min << 8) | (u32)yellow_r_min;
    cnn_hw_write(CNN_REG_RED_THRESHOLD, red);
    cnn_hw_write(CNN_REG_GREEN_THRESHOLD, green);
    cnn_hw_write(CNN_REG_BLUE_THRESHOLD, blue);
    cnn_hw_write(CNN_REG_YELLOW_THRESHOLD, yellow);
    return CNN_OK;
}

cnn_error_t cnn_hw_set_color_min_count(u32 count)
{
    if (count == 0U || count > 0x3FFFFU)
        return CNN_ERR_ARGUMENT;
    if (cnn_hw_status() & (CNN_STATUS_BUSY | CNN_STATUS_ERROR))
        return CNN_ERR_BUSY;
    cnn_hw_write(CNN_REG_MIN_COUNT, count);
    return CNN_OK;
}

cnn_error_t cnn_hw_initialize_color_defaults(void)
{
    cnn_error_t e;
    e = cnn_hw_set_color_thresholds(CNN_DEFAULT_RED_MIN,
            CNN_DEFAULT_GREEN_MIN, CNN_DEFAULT_BLUE_MIN,
            CNN_DEFAULT_YELLOW_R_MIN, CNN_DEFAULT_YELLOW_G_MIN,
            CNN_DEFAULT_YELLOW_B_MAX);
    if (e != CNN_OK) return e;
    e = cnn_hw_set_color_margins(CNN_DEFAULT_RED_MARGIN,
                                 CNN_DEFAULT_GREEN_MARGIN,
                                 CNN_DEFAULT_BLUE_MARGIN);
    if (e != CNN_OK) return e;
    e = cnn_hw_set_yellow_margins(CNN_DEFAULT_YELLOW_RG_DELTA,
                                   CNN_DEFAULT_YELLOW_BGAP);
    if (e != CNN_OK) return e;
    e = cnn_hw_set_color_min_count(CNN_DEFAULT_COLOR_MIN_COUNT);
    if (e != CNN_OK) return e;
    return cnn_hw_set_color_enable(CNN_COLOR_ENABLE_ALL);
}

cnn_error_t cnn_hw_configure(u32 weight_base, u32 fm_a_base,
                             u32 fm_b_base, u32 sg_base,
                             u32 frame_base, u32 frame_id)
{
    u32 status = cnn_hw_status();

    if ((weight_base | fm_a_base | fm_b_base | sg_base | frame_base) & 0x3FU)
        return CNN_ERR_BAD_ALIGNMENT;
    if (status & (CNN_STATUS_BUSY | CNN_STATUS_ERROR))
        return CNN_ERR_BUSY;

    cnn_hw_write(CNN_REG_WEIGHT_BASE, weight_base);
    cnn_hw_write(CNN_REG_FM_A_BASE, fm_a_base);
    cnn_hw_write(CNN_REG_FM_B_BASE, fm_b_base);
    cnn_hw_write(CNN_REG_SG_BASE, sg_base);
    cnn_hw_write(CNN_REG_FRAME_BASE, frame_base);
    cnn_hw_write(CNN_REG_FRAME_ID, frame_id);
    return CNN_OK;
}

cnn_error_t cnn_hw_start(void)
{
    u32 status = cnn_hw_status();
    if (status & (CNN_STATUS_BUSY | CNN_STATUS_ERROR | CNN_STATUS_DONE))
        return CNN_ERR_BUSY;
    cnn_hw_write(CNN_REG_CONTROL, CNN_CONTROL_START);
    return CNN_OK;
}

cnn_error_t cnn_hw_read_result(cnn_result_t *result)
{
    unsigned int retry;
    unsigned int i;

    if (result == 0)
        return CNN_ERR_ARGUMENT;

    for (retry = 0; retry < 3U; ++retry) {
        u32 seq_before = cnn_hw_read(CNN_REG_RESULT_SEQ);
        u32 flags = cnn_hw_read(CNN_REG_JOINT_FLAGS) & 0x1FFFFU;

        result->result_seq = seq_before;
        result->joint_flags = flags;
        for (i = 0; i < CNN_JOINT_COUNT; ++i) {
            u32 word = cnn_hw_read(CNN_REG_JOINT0 + (i * 4U));
            result->joint[i].x = (u16)(word & 0x0FFFU);
            result->joint[i].y = (u16)((word >> 12) & 0x0FFFU);
            result->joint[i].score = (s8)(word >> 24);
            result->joint[i].valid = (u8)((flags >> i) & 1U);
        }
        result->red_marker = cnn_hw_read(CNN_REG_RED_RESULT);
        result->blue_marker = cnn_hw_read(CNN_REG_BLUE_RESULT);
        result->green_marker = cnn_hw_read(CNN_REG_GREEN_RESULT);
        result->yellow_marker = cnn_hw_read(CNN_REG_YELLOW_RESULT);
        result->frame_id = cnn_hw_read(CNN_REG_RESULT_FRAME);
        result->cycle_count = cnn_hw_read(CNN_REG_CYCLE_COUNT);

        if (cnn_hw_read(CNN_REG_RESULT_SEQ) == seq_before)
            return CNN_OK;
    }
    return CNN_ERR_RESULT_UNSTABLE;
}
