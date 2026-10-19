module cnn_c07_irq_sva (
    input wire clk,
    input wire rst_n,
    input wire irq,
    input wire irq_enable,
    input wire done_pending,
    input wire error_pending,
    input wire clear_done,
    input wire clear_error
);
    // The top-level continuous assignment defines a level, not a pulse.
    ap_irq_level: assert property (@(posedge clk) disable iff (!rst_n)
        irq == (irq_enable && (done_pending || error_pending)))
        else $error("C07_SVA_IRQ_LEVEL");

    // Command pulses are registered; pending clears at the next sample.
    ap_clear_done: assert property (@(posedge clk) disable iff (!rst_n)
        clear_done |=> !done_pending)
        else $error("C07_SVA_CLEAR_DONE");
    ap_clear_error: assert property (@(posedge clk) disable iff (!rst_n)
        clear_error |=> !error_pending)
        else $error("C07_SVA_CLEAR_ERROR");
    ap_reset_irq: assert property (@(posedge clk)
        !rst_n |=> !irq)
        else $error("C07_SVA_RESET_IRQ");
endmodule

bind cnn_accelerator_top cnn_c07_irq_sva c07_irq_sva_inst (
    .clk(clk), .rst_n(rst_n), .irq(irq), .irq_enable(irq_enable),
    .done_pending(done_pending), .error_pending(error_pending),
    .clear_done(clear_done), .clear_error(clear_error)
);
