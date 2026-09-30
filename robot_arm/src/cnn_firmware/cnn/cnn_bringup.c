#include "cnn_bringup.h"
#include "cnn_hw.h"
#include "cnn_weights.h"
#include "cnn_frame_sg.h"
#include "cnn_diag.h"
#include "../keypoint_overlay/keypoint_overlay.h"
#include "xil_io.h"
#include "xil_printf.h"
#include "xparameters.h"
#include "xscugic.h"
#include "xtime_l.h"

#define CNN_FM_A_ADDRESS 0x11000000U
#define CNN_FM_B_ADDRESS 0x11100000U
#define CNN_IRQ_TIMEOUT_SECONDS 2U
#define CNN_IRQ_PRIORITY        0xA0U
#define CNN_IRQ_TRIGGER_HIGH    0x01U

#define CNN_DISPLAY_WIDTH  1280U
#define CNN_DISPLAY_HEIGHT 720U

static volatile int cnn_irq_event;
static volatile u32 cnn_irq_status;
static volatile XTime cnn_irq_end_time;
static volatile u32 cnn_irq_count;
static XTime cnn_irq_start_time;
static int cnn_irq_initialized;
static u32 skeleton_visible_mask = CNN_ROBOT_SKELETON_MASK;

void cnn_bringup_set_skeleton_mask(u32 mask)
{
    skeleton_visible_mask = mask & CNN_ROBOT_SKELETON_MASK;
}

u32 cnn_bringup_get_skeleton_mask(void)
{
    return skeleton_visible_mask;
}

cnn_error_t cnn_bringup_set_skeleton_joint(unsigned int index, int enable)
{
    if (index < CNN_JOINT_LEFT_SHOULDER || index >= CNN_JOINT_COUNT)
        return CNN_ERR_ARGUMENT;
    if (enable)
        skeleton_visible_mask |= (1U << index);
    else
        skeleton_visible_mask &= ~(1U << index);
    return CNN_OK;
}

static void cnn_interrupt_handler(void *callback)
{
    XTime end_time;

    (void)callback;
    cnn_irq_status=cnn_hw_status();
    XTime_GetTime(&end_time);
    cnn_irq_end_time=end_time;

    /* Deassert the level IRQ while preserving DONE/ERROR for foreground
     * result handling and diagnostics.  The next run explicitly rearms it.
     */
    cnn_hw_enable_irq(0);
    cnn_irq_count++;
    cnn_irq_event=1;
}

cnn_error_t cnn_bringup_interrupt_init(XScuGic *interrupt_controller)
{
    int status;

    if(cnn_irq_initialized)
        return CNN_OK;
    if(interrupt_controller==0)
        return CNN_ERR_INTERRUPT;

    XScuGic_SetPriorityTriggerType(interrupt_controller,
        XPAR_FABRIC_CNN_ACCELERATOR_TOP_0_IRQ_INTR,
        CNN_IRQ_PRIORITY,CNN_IRQ_TRIGGER_HIGH);
    status=XScuGic_Connect(interrupt_controller,
        XPAR_FABRIC_CNN_ACCELERATOR_TOP_0_IRQ_INTR,
        (Xil_InterruptHandler)cnn_interrupt_handler,0);
    if(status!=XST_SUCCESS)
        return CNN_ERR_INTERRUPT;

    XScuGic_Enable(interrupt_controller,
        XPAR_FABRIC_CNN_ACCELERATOR_TOP_0_IRQ_INTR);

    cnn_hw_enable_irq(0);
    cnn_irq_event=0;
    cnn_irq_status=0U;
    cnn_irq_end_time=0U;
    cnn_irq_count=0U;
    cnn_irq_initialized=1;
    xil_printf("CNN interrupt: GIC ID %lu ready\r\n",
               (unsigned long)XPAR_FABRIC_CNN_ACCELERATOR_TOP_0_IRQ_INTR);
    return CNN_OK;
}

static void cnn_interrupt_arm(void)
{
    cnn_irq_event=0;
    cnn_irq_status=0U;
    cnn_irq_end_time=0U;
    cnn_hw_enable_irq(1);
    XTime_GetTime(&cnn_irq_start_time);
}

static cnn_error_t cnn_interrupt_wait(cnn_bringup_t *ctx,
                                      u32 *status, u32 *elapsed_us)
{
    XTime now;
    XTime timeout_ticks=(XTime)COUNTS_PER_SECOND*CNN_IRQ_TIMEOUT_SECONDS;
    XTime elapsed_ticks;

    if(!cnn_irq_initialized)
        return CNN_ERR_INTERRUPT;

    while(!cnn_irq_event) {
        if(ctx!=0 && ctx->idle_hook!=0)
            ctx->idle_hook(ctx->idle_hook_context);
        XTime_GetTime(&now);
        if((now-cnn_irq_start_time)>timeout_ticks) {
            cnn_hw_enable_irq(0);
            return CNN_ERR_TIMEOUT;
        }
    }

    *status=cnn_irq_status;
    elapsed_ticks=cnn_irq_end_time-cnn_irq_start_time;
    *elapsed_us=(u32)((elapsed_ticks*1000000ULL)/COUNTS_PER_SECOND);
    return CNN_OK;
}

/*
 * Publish one coherent CNN result to the overlay shadow bank. The overlay
 * consumes the bank on a video SOF, so coordinates and valid bits never tear
 * across displayed frames.  Recheck the display bounds here even though the
 * CNN result contract already produces 1280x720 coordinates: if a corrupted
 * result ever escapes, its valid bit is suppressed instead of exposing a
 * stale joint left in the overlay register bank.
 */
static int s_overlay_robot_only = 1;

void cnn_bringup_set_overlay_robot_only(int enabled)
{
    s_overlay_robot_only = enabled ? 1 : 0;
}

int cnn_bringup_overlay_robot_only(void)
{
    return s_overlay_robot_only;
}

static void cnn_overlay_publish(const cnn_result_t *result, int verbose)
{
    u32 visible_flags = 0U;
    u32 display_mask;
    unsigned int i;

    if (result == 0)
        return;

    display_mask = s_overlay_robot_only ? CNN_ROBOT_INPUT_OVERLAY_MASK :
                                          CNN_ROBOT_SKELETON_MASK;
    kpo_set_enable(1);
    /* Joint visibility affects HDMI only; CNN results and robot input remain intact. */
    kpo_set_body_arm_colors(KPO_COLOR_YELLOW, KPO_COLOR_CYAN);
    kpo_set_radius(5U);
    kpo_set_source_frame_id(result->frame_id);

    for (i = 0U; i < CNN_JOINT_COUNT; ++i) {
        const cnn_joint_t *joint = &result->joint[i];

        if ((display_mask & skeleton_visible_mask & (1U << i)) && joint->valid &&
            joint->x < CNN_DISPLAY_WIDTH &&
            joint->y < CNN_DISPLAY_HEIGHT) {
            kpo_set_joint((int)i, joint->x, joint->y, (u8)joint->score);
            visible_flags |= (1U << i);
        }
        else {
            kpo_set_joint((int)i, 0U, 0U, 0U);
        }
    }

    /* Show the green detection result in either joint-display mode. */
    kpo_set_color_results(result->red_marker, result->blue_marker,
                          result->green_marker, result->yellow_marker);
    kpo_set_valid_flags(visible_flags);
    kpo_commit();

    if (verbose) {
        xil_printf("CNN overlay: frame=%lu view=%s joints=0x%05x red=%08x blue=%08x green=%08x yellow=%08x commit=%s\r\n",
                   (unsigned long)result->frame_id,
                   s_overlay_robot_only ? "robot" : "body-joints",
                   (unsigned int)visible_flags,
                   (unsigned int)result->red_marker,
                   (unsigned int)result->blue_marker,
                   (unsigned int)result->green_marker,
                   (unsigned int)result->yellow_marker,
                   kpo_commit_pending() ? "pending" : "accepted");
        /* One manual run produces one complete overlay/color diagnostic. */
        kpo_debug_dump();
    }
}

void cnn_bringup_init(cnn_bringup_t *ctx, XAxiVdma *vdma, u32 frame_buffer_base)
{
    if(ctx==0) return;
    ctx->vdma=vdma; ctx->frame_buffer_base=frame_buffer_base;
    ctx->selected_frame_base=frame_buffer_base; ctx->next_frame_id=1U;
    ctx->initialized=1; ctx->frame_prepared=0; ctx->running=0;
    ctx->last_elapsed_us=0U;
    ctx->idle_hook=0; ctx->idle_hook_context=0;
    cnn_hw_enable_irq(0);
}

void cnn_bringup_set_idle_hook(cnn_bringup_t *ctx,
                               void (*hook)(void *context), void *context)
{
    if(ctx==0)
        return;
    ctx->idle_hook=hook;
    ctx->idle_hook_context=context;
}

cnn_error_t cnn_bringup_load_weights(cnn_bringup_t *ctx)
{
    cnn_error_t e;
    if(ctx==0 || !ctx->initialized) return CNN_ERR_NOT_INITIALIZED;
    /* The running CNN may still be reading the DDR weight region. */
    if(ctx->running || (cnn_hw_status()&CNN_STATUS_BUSY)) return CNN_ERR_BUSY;
    xil_printf("CNN: loading %s to 0x%08x\r\n",CNN_WEIGHT_PATH,CNN_WEIGHT_ADDRESS);
    e=cnn_weights_load_from_sd();
    xil_printf("CNN: weight load %s\r\n",cnn_error_string(e));
    return e;
}

static cnn_error_t cnn_bringup_prepare_frame_internal(cnn_bringup_t *ctx,
                                                       int verbose)
{
    u32 current,selected,current_after;
    unsigned int retry;
    cnn_error_t e;
    if(ctx==0 || !ctx->initialized || ctx->vdma==0) return CNN_ERR_NOT_INITIALIZED;
    /* Check before cnn_sg_build writes descriptors consumed by the CNN. */
    if(ctx->running || (cnn_hw_status()&CNN_STATUS_BUSY)) return CNN_ERR_BUSY;
    e=CNN_ERR_BUSY;
    current=0U; selected=0U;
    for(retry=0;retry<3U;++retry) {
        current=XAxiVdma_CurrFrameStore(ctx->vdma,XAXIVDMA_WRITE)%3U;
        selected=(current+2U)%3U;
        ctx->selected_frame_base=ctx->frame_buffer_base+selected*CNN_FRAME_BYTES;
        e=cnn_sg_build(CNN_SG_ADDRESS,ctx->selected_frame_base);
        if(e!=CNN_OK) break;
        current_after=XAxiVdma_CurrFrameStore(ctx->vdma,XAXIVDMA_WRITE)%3U;
        if(current_after==current) break;
        e=CNN_ERR_BUSY;
    }
    ctx->frame_prepared=(e==CNN_OK);
    if (verbose) {
        xil_printf("CNN: VDMA writing frame %lu, selected completed frame %lu at 0x%08x\r\n",
                   (unsigned long)current,(unsigned long)selected,ctx->selected_frame_base);
        xil_printf("CNN: SG build %s\r\n",cnn_error_string(e));
        if(e==CNN_OK) cnn_diag_print_sg(CNN_SG_ADDRESS);
    }
    return e;
}

cnn_error_t cnn_bringup_prepare_frame(cnn_bringup_t *ctx)
{
    if(ctx==0 || !ctx->initialized) return CNN_ERR_NOT_INITIALIZED;
    if(ctx->running) return CNN_ERR_BUSY;
    return cnn_bringup_prepare_frame_internal(ctx,1);
}

static cnn_error_t cnn_bringup_run_internal(cnn_bringup_t *ctx, int verbose)
{
    cnn_error_t e;
    u32 status;

    if(ctx==0 || !ctx->initialized) return CNN_ERR_NOT_INITIALIZED;
    if(!cnn_irq_initialized) return CNN_ERR_INTERRUPT;
    if(!cnn_weights_are_loaded()) return CNN_ERR_WEIGHTS_NOT_LOADED;
    e=cnn_bringup_prepare_frame_internal(ctx,verbose); if(e!=CNN_OK) return e;
    if(verbose) xil_printf("CNN run: configure begin\r\n");
    status=cnn_hw_status();
    if(verbose) {
        xil_printf("CNN run: pre-start status/error = %08x/%08x\r\n",
                   status,cnn_hw_error());
    }
    if(status&CNN_STATUS_DONE) cnn_hw_clear_done();
    if(status&CNN_STATUS_ERROR) {
        cnn_diag_dump();
        return CNN_ERR_HW_FAULT;
    }
    e=cnn_hw_configure(CNN_WEIGHT_ADDRESS,CNN_FM_A_ADDRESS,CNN_FM_B_ADDRESS,
                       CNN_SG_ADDRESS,ctx->selected_frame_base,ctx->next_frame_id);
    if(e!=CNN_OK) return e;
    if(verbose) xil_printf("CNN run: configure PASS\r\n");
    cnn_interrupt_arm();
    e=cnn_hw_start();
    if(e!=CNN_OK) {
        cnn_hw_enable_irq(0);
        return e;
    }
    if(verbose) xil_printf("CNN run: start accepted; waiting for IRQ\r\n");

    e=cnn_interrupt_wait(ctx,&status,&ctx->last_elapsed_us);
    if(e!=CNN_OK) {
        xil_printf("CNN run: interrupt wait failed; diagnostic begin\r\n");
        cnn_diag_dump();
        return e;
    }
    if(status&CNN_STATUS_ERROR) {
        xil_printf("CNN run: hardware error IRQ; diagnostic begin\r\n");
        cnn_diag_dump();
        return CNN_ERR_HW_FAULT;
    }
    if((status&CNN_STATUS_DONE)==0U) {
        cnn_diag_dump();
        return CNN_ERR_INTERRUPT;
    }

    e=cnn_hw_read_result(&ctx->last_result);
    if(e==CNN_OK) {
        if(verbose) {
            xil_printf("CNN timing: %lu.%03lu ms by completion IRQ; hardware cycles=%lu\r\n",
                       (unsigned long)(ctx->last_elapsed_us/1000U),
                       (unsigned long)(ctx->last_elapsed_us%1000U),
                       (unsigned long)ctx->last_result.cycle_count);
            cnn_diag_print_result(&ctx->last_result);
        }
        cnn_overlay_publish(&ctx->last_result,verbose);
        cnn_hw_clear_done();
        ctx->next_frame_id++;
    }
    return e;
}

cnn_error_t cnn_bringup_start(cnn_bringup_t *ctx, int verbose)
{
    cnn_error_t e;
    u32 status;

    if(ctx==0 || !ctx->initialized) return CNN_ERR_NOT_INITIALIZED;
    if(ctx->running) return CNN_ERR_BUSY;
    if(!cnn_irq_initialized) return CNN_ERR_INTERRUPT;
    if(!cnn_weights_are_loaded()) return CNN_ERR_WEIGHTS_NOT_LOADED;
    e=cnn_bringup_prepare_frame_internal(ctx,verbose);
    if(e!=CNN_OK) return e;
    status=cnn_hw_status();
    if(status&CNN_STATUS_DONE) cnn_hw_clear_done();
    if(status&CNN_STATUS_ERROR) return CNN_ERR_HW_FAULT;
    e=cnn_hw_configure(CNN_WEIGHT_ADDRESS,CNN_FM_A_ADDRESS,CNN_FM_B_ADDRESS,
                       CNN_SG_ADDRESS,ctx->selected_frame_base,ctx->next_frame_id);
    if(e!=CNN_OK) return e;
    cnn_interrupt_arm();
    e=cnn_hw_start();
    if(e!=CNN_OK) {
        cnn_hw_enable_irq(0);
        return e;
    }
    ctx->running=1;
    return CNN_OK;
}

cnn_error_t cnn_bringup_service(cnn_bringup_t *ctx)
{
    u32 status;
    XTime now;
    XTime elapsed_ticks;
    cnn_error_t e;

    if(ctx==0 || !ctx->initialized) return CNN_ERR_NOT_INITIALIZED;
    if(!ctx->running) return CNN_ERR_ARGUMENT;
    if(!cnn_irq_event) {
        XTime_GetTime(&now);
        if(now-cnn_irq_start_time <=
           (XTime)COUNTS_PER_SECOND*CNN_IRQ_TIMEOUT_SECONDS)
            return CNN_PENDING;
        cnn_hw_enable_irq(0);
        ctx->running=0;
        return CNN_ERR_TIMEOUT;
    }
    ctx->running=0;
    status=cnn_irq_status;
    elapsed_ticks=cnn_irq_end_time-cnn_irq_start_time;
    ctx->last_elapsed_us=(u32)((elapsed_ticks*1000000ULL)/COUNTS_PER_SECOND);
    if(status&CNN_STATUS_ERROR) return CNN_ERR_HW_FAULT;
    if((status&CNN_STATUS_DONE)==0U) return CNN_ERR_INTERRUPT;
    e=cnn_hw_read_result(&ctx->last_result);
    if(e!=CNN_OK) return e;
    cnn_overlay_publish(&ctx->last_result,0);
    cnn_hw_clear_done();
    ctx->next_frame_id++;
    return CNN_OK;
}

u32 cnn_bringup_irq_count(void)
{
    return cnn_irq_count;
}

cnn_error_t cnn_bringup_run_once(cnn_bringup_t *ctx)
{
    return cnn_bringup_run_internal(ctx,1);
}

cnn_error_t cnn_bringup_run_continuous_frame(cnn_bringup_t *ctx)
{
    return cnn_bringup_run_internal(ctx,0);
}

cnn_error_t cnn_bringup_recover(cnn_bringup_t *ctx)
{
    if(ctx==0 || !ctx->initialized) return CNN_ERR_NOT_INITIALIZED;
    cnn_hw_enable_irq(0);
    ctx->running=0;
    cnn_irq_event=0;
    cnn_hw_soft_reset();
    cnn_sg_clear_status(CNN_SG_ADDRESS);
    kpo_clear();
    ctx->frame_prepared=0;
    xil_printf("CNN: core soft reset complete; DMA control is private to CNN hardware\r\n");
    return CNN_OK;
}

void cnn_bringup_print_status(cnn_bringup_t *ctx)
{
    cnn_diag_dump();
    if(ctx!=0) {
        xil_printf(" last interrupt elapsed     : %lu.%03lu ms\r\n",
                   (unsigned long)(ctx->last_elapsed_us/1000U),
                   (unsigned long)(ctx->last_elapsed_us%1000U));
        xil_printf(" CNN completion IRQ count   : %lu\r\n",
                   (unsigned long)cnn_irq_count);
    }
}

void cnn_bringup_print_last_result(cnn_bringup_t *ctx)
{
    if(ctx!=0) cnn_diag_print_result(&ctx->last_result);
}

