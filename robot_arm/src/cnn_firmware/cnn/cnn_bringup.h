#ifndef CNN_BRINGUP_H
#define CNN_BRINGUP_H

#include "cnn_types.h"
#include "xaxivdma.h"
#include "xscugic.h"

typedef struct {
    XAxiVdma *vdma;
    u32 frame_buffer_base;
    u32 selected_frame_base;
    u32 next_frame_id;
    int initialized;
    int frame_prepared;
    int running;
    u32 last_elapsed_us;
    cnn_result_t last_result;
    void (*idle_hook)(void *context);
    void *idle_hook_context;
} cnn_bringup_t;

void cnn_bringup_init(cnn_bringup_t *ctx, XAxiVdma *vdma, u32 frame_buffer_base);
void cnn_bringup_set_idle_hook(cnn_bringup_t *ctx,
                               void (*hook)(void *context), void *context);
cnn_error_t cnn_bringup_interrupt_init(XScuGic *interrupt_controller);
cnn_error_t cnn_bringup_load_weights(cnn_bringup_t *ctx);
cnn_error_t cnn_bringup_prepare_frame(cnn_bringup_t *ctx);
cnn_error_t cnn_bringup_run_once(cnn_bringup_t *ctx);
cnn_error_t cnn_bringup_run_continuous_frame(cnn_bringup_t *ctx);
/* IRQ records completion; service never waits and returns CNN_PENDING while busy. */
cnn_error_t cnn_bringup_start(cnn_bringup_t *ctx, int verbose);
cnn_error_t cnn_bringup_service(cnn_bringup_t *ctx);
u32 cnn_bringup_irq_count(void);
cnn_error_t cnn_bringup_recover(cnn_bringup_t *ctx);
void cnn_bringup_print_status(cnn_bringup_t *ctx);
void cnn_bringup_print_last_result(cnn_bringup_t *ctx);
/* Display-only switch; takes effect on the next published CNN frame. */
void cnn_bringup_set_overlay_robot_only(int enabled);
int cnn_bringup_overlay_robot_only(void);

#endif
