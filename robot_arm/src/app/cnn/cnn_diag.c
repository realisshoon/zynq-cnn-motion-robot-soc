#include "cnn_diag.h"
#include "cnn_hw.h"
#include "cnn_frame_sg.h"
#include "xil_printf.h"

static void cnn_diag_print_fault_sources(u32 faults)
{
    static const char * const names[10] = {
        "input_conv", "line_buffer", "depthwise", "pointwise",
        "feature_map", "downsample", "argmax", "coord_restore",
        "color_marker", "joint_protocol"
    };
    u32 i;

    faults &= 0x3ffU;
    if (faults == 0U) {
        xil_printf(" none");
        return;
    }
    for (i = 0U; i < 10U; ++i) {
        if ((faults & (1U << i)) != 0U)
            xil_printf(" %s", names[i]);
    }
}

static void cnn_diag_print_fm_fault_reason(u32 reason)
{
    static const char * const names[10] = {
        "cfg_contract", "dma_error", "read_accept", "read_extra",
        "read_dma_done", "read_underflow", "read_after_frame",
        "body_tag", "write_body_count", "write_extra"
    };
    u32 i;

    reason &= 0x3ffU;
    if (reason == 0U) {
        xil_printf(" none");
        return;
    }
    for (i = 0U; i < 10U; ++i) {
        if ((reason & (1U << i)) != 0U)
            xil_printf(" %s", names[i]);
    }
}

void cnn_diag_probe_report(void)
{
    cnn_error_t e=cnn_hw_probe();
    xil_printf("\r\nCNN probe\r\n");
    xil_printf("  base     : 0x%08x\r\n",CNN_BASE_ADDRESS);
    xil_printf("  version  : 0x%08x\r\n",cnn_hw_read(CNN_REG_VERSION));
    xil_printf("  features : 0x%08x\r\n",cnn_hw_read(CNN_REG_FEATURES));
    xil_printf("  pack id  : 0x%08x\r\n",cnn_hw_read(CNN_REG_PACK_ID));
    xil_printf("  color en : 0x%08x (red/blue/green/yellow=%lu/%lu/%lu/%lu)\r\n",
               cnn_hw_read(CNN_REG_COLOR_ENABLE),
               (unsigned long)((cnn_hw_read(CNN_REG_COLOR_ENABLE) >> 0) & 1U),
               (unsigned long)((cnn_hw_read(CNN_REG_COLOR_ENABLE) >> 1) & 1U),
               (unsigned long)((cnn_hw_read(CNN_REG_COLOR_ENABLE) >> 2) & 1U),
               (unsigned long)((cnn_hw_read(CNN_REG_COLOR_ENABLE) >> 3) & 1U));
    xil_printf("  thresholds R/B/G/Y: %06x / %06x / %06x / %06x\r\n",
               cnn_hw_read(CNN_REG_RED_THRESHOLD),
               cnn_hw_read(CNN_REG_BLUE_THRESHOLD),
               cnn_hw_read(CNN_REG_GREEN_THRESHOLD),
               cnn_hw_read(CNN_REG_YELLOW_THRESHOLD));
    xil_printf("  margins B/G/R    : %06x\r\n",
               cnn_hw_read(CNN_REG_COLOR_MARGIN));
    xil_printf("  yellow margins RG/B-gap: %04x\r\n",
               cnn_hw_read(CNN_REG_YELLOW_MARGIN));
    xil_printf("  color min count  : %lu\r\n",
               (unsigned long)cnn_hw_read(CNN_REG_MIN_COUNT));
    xil_printf("  result   : %s\r\n",cnn_error_string(e));
}

void cnn_diag_capture(cnn_debug_snapshot_t *s)
{
    if(s==0) return;
    s->cnn_status=cnn_hw_status(); s->cnn_error=cnn_hw_error();
    s->result_seq=cnn_hw_read(CNN_REG_RESULT_SEQ); s->cycle_count=cnn_hw_read(CNN_REG_CYCLE_COUNT);
    /* The three CNN DMA control ports are private to the CNN m_axil master.
       They are intentionally absent from the PS address space. */
    s->image_dma_control=0xffffffffU; s->image_dma_status=0xffffffffU;
    s->weight_dma_control=0xffffffffU; s->weight_dma_status=0xffffffffU;
    s->feature_mm2s_control=0xffffffffU; s->feature_mm2s_status=0xffffffffU;
    s->feature_s2mm_control=0xffffffffU; s->feature_s2mm_status=0xffffffffU;
}

void cnn_diag_dump(void)
{
    u32 value;
    u32 fault_latched;
    u32 fault_live;
    u32 context;
    u32 fm_reason;
    u32 fm_status;
    u32 fm_misc;
    xil_printf("\r\nCNN/DMA snapshot\r\n");
    value=cnn_hw_status(); xil_printf(" CNN status                 : %08x\r\n",value);
    value=cnn_hw_error(); xil_printf(" CNN error                  : %08x\r\n",value);
    value=cnn_hw_read(CNN_REG_RESULT_SEQ); xil_printf(" CNN result sequence        : %08x\r\n",value);
    value=cnn_hw_read(CNN_REG_CYCLE_COUNT); xil_printf(" CNN cycle count            : %08x\r\n",value);
    value=cnn_hw_read(CNN_REG_FRAME_ID); xil_printf(" requested frame ID         : %08x\r\n",value);
    value=cnn_hw_read(CNN_REG_RESULT_FRAME); xil_printf(" result frame ID            : %08x\r\n",value);
    value=cnn_hw_read(CNN_REG_WEIGHT_BASE); xil_printf(" weight base                : %08x\r\n",value);
    value=cnn_hw_read(CNN_REG_FM_A_BASE); xil_printf(" feature-map A base         : %08x\r\n",value);
    value=cnn_hw_read(CNN_REG_FM_B_BASE); xil_printf(" feature-map B base         : %08x\r\n",value);
    value=cnn_hw_read(CNN_REG_SG_BASE); xil_printf(" image SG base              : %08x\r\n",value);
    value=cnn_hw_read(CNN_REG_FRAME_BASE); xil_printf(" selected frame base        : %08x\r\n",value);
    value=cnn_hw_read(CNN_REG_TIMEOUT); xil_printf(" hardware watchdog limit    : %08x\r\n",value);
    value=cnn_hw_read(CNN_REG_COLOR_ENABLE); xil_printf(" color enable R/B/G/Y       : %lu / %lu / %lu / %lu\r\n",
        (unsigned long)((value >> 0) & 1U),
        (unsigned long)((value >> 1) & 1U),
        (unsigned long)((value >> 2) & 1U),
        (unsigned long)((value >> 3) & 1U));
    xil_printf(" color thresholds R/B/G/Y   : %06x / %06x / %06x / %06x\r\n",
        cnn_hw_read(CNN_REG_RED_THRESHOLD),
        cnn_hw_read(CNN_REG_BLUE_THRESHOLD),
        cnn_hw_read(CNN_REG_GREEN_THRESHOLD),
        cnn_hw_read(CNN_REG_YELLOW_THRESHOLD));
    xil_printf(" yellow margins RG/B-gap    : %04x\r\n",
        cnn_hw_read(CNN_REG_YELLOW_MARGIN));
    value=cnn_hw_read(CNN_REG_COLOR_MARGIN); xil_printf(" color margins B/G/R        : %lu / %lu / %lu\r\n",
        (unsigned long)((value >> 16) & 0xffU),
        (unsigned long)((value >> 8) & 0xffU),
        (unsigned long)(value & 0xffU));
    value=cnn_hw_read(CNN_REG_MIN_COUNT); xil_printf(" color minimum pixel count  : %lu\r\n",
        (unsigned long)value);
    fault_latched=cnn_hw_read(CNN_REG_DEBUG_FAULT_LATCH);
    fault_live=cnn_hw_read(CNN_REG_DEBUG_FAULT_LIVE);
    context=cnn_hw_read(CNN_REG_DEBUG_CONTEXT);
    xil_printf(" debug fault latched        : %08x",fault_latched);
    cnn_diag_print_fault_sources(fault_latched);
    xil_printf("\r\n");
    xil_printf(" debug fault live           : %08x",fault_live);
    cnn_diag_print_fault_sources(fault_live);
    xil_printf("\r\n");
    if ((context & CNN_DEBUG_CONTEXT_MASK) == CNN_DEBUG_CONTEXT_MARKER) {
        xil_printf(" debug context              : %08x\r\n",context);
        xil_printf("  state/stage/step/txn      : %lu/%lu/%lu/%lu\r\n",
                   (unsigned long)(context & 0x1fU),
                   (unsigned long)((context >> 5) & 0x0fU),
                   (unsigned long)((context >> 9) & 0x0fU),
                   (unsigned long)((context >> 13) & 0x07U));
        xil_printf("  busy/error-pending        : %lu/%lu\r\n",
                   (unsigned long)((context >> 16) & 1U),
                   (unsigned long)((context >> 17) & 1U));
        value=cnn_hw_read(CNN_REG_DEBUG_TXN_ADDR);
        xil_printf(" debug last AXI-Lite address: %08x\r\n",value);
        value=cnn_hw_read(CNN_REG_DEBUG_TXN_DATA);
        xil_printf(" debug last AXI-Lite rdata  : %08x\r\n",value);
        fm_reason=cnn_hw_read(CNN_REG_FM_FAULT_REASON);
        xil_printf(" FM fault reason             : %08x",fm_reason);
        cnn_diag_print_fm_fault_reason(fm_reason);
        xil_printf("\r\n");
        xil_printf(" FM read input/parsed bytes  : %lu / %lu\r\n",
            (unsigned long)cnn_hw_read(CNN_REG_FM_READ_INPUT),
            (unsigned long)cnn_hw_read(CNN_REG_FM_READ_PARSED));
        xil_printf(" FM write in/packed/out bytes: %lu / %lu / %lu\r\n",
            (unsigned long)cnn_hw_read(CNN_REG_FM_WRITE_INPUT),
            (unsigned long)cnn_hw_read(CNN_REG_FM_WRITE_PACKED),
            (unsigned long)cnn_hw_read(CNN_REG_FM_WRITE_OUTPUT));
        xil_printf(" FM expected src/dst bytes   : %lu / %lu\r\n",
            (unsigned long)cnn_hw_read(CNN_REG_FM_EXPECTED_SRC),
            (unsigned long)cnn_hw_read(CNN_REG_FM_EXPECTED_DST));
        fm_status=cnn_hw_read(CNN_REG_FM_STREAM_STATUS);
        fm_misc=cnn_hw_read(CNN_REG_FM_PROTOCOL_MISC);
        xil_printf(" FM stream status            : %08x\r\n",fm_status);
        xil_printf(" FM protocol misc            : %08x\r\n",fm_misc);
        xil_printf(" FM last body tag            : %08x%08x\r\n",
            cnn_hw_read(CNN_REG_FM_BODY_TAG_HI),
            cnn_hw_read(CNN_REG_FM_BODY_TAG_LO));
    } else {
        xil_printf(" debug context              : %08x (debug bitstream not detected)\r\n",context);
    }
    xil_printf(" DMA control/status         : private to CNN m_axil; not PS-readable\r\n");
    xil_printf(" CNN/DMA snapshot end\r\n");
}

void cnn_diag_print_result(const cnn_result_t *r)
{
    u32 i;
    if(r==0) return;
    xil_printf("\r\nCNN result frame=%lu seq=%lu cycles=%lu flags=0x%05x\r\n",
               (unsigned long)r->frame_id,(unsigned long)r->result_seq,
               (unsigned long)r->cycle_count,r->joint_flags);
    for(i=0;i<CNN_JOINT_COUNT;++i)
        xil_printf(" joint[%02lu] x=%4u y=%3u score=%4d valid=%u\r\n",
                   (unsigned long)i,r->joint[i].x,r->joint[i].y,
                   (int)r->joint[i].score,r->joint[i].valid);
    xil_printf(" markers red=0x%08x blue=0x%08x green=0x%08x yellow=0x%08x\r\n",
               r->red_marker,r->blue_marker,r->green_marker,
               r->yellow_marker);
}

void cnn_diag_print_sg(u32 descriptor_base)
{
    const u32 index[3]={0U,1U,CNN_SG_BD_COUNT-1U};
    u32 k;
    for(k=0;k<3U;++k) {
        volatile u32 *bd=(volatile u32 *)(UINTPTR)(descriptor_base+index[k]*CNN_SG_BD_BYTES);
        xil_printf(" SG[%3lu] next=%08x buffer=%08x control=%08x status=%08x\r\n",
                   (unsigned long)index[k],bd[0],bd[2],bd[6],bd[7]);
    }
}
