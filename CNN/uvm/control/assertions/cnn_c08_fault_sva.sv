module cnn_c08_fault_sva (
    input wire clk,
    input wire rst_n,
    input wire [4:0] state,
    input wire fault_accept,
    input wire fault_lock,
    input wire [31:0] fault_code_next,
    input wire [31:0] error_code_r,
    input wire busy_r,
    input wire done_pending_r,
    input wire error_pending_r
);
    // The accepted source is registered on the next sampled clock edge.
    ap_fault_boundary: assert property (@(posedge clk) disable iff (!rst_n)
        fault_accept |=> (state == 17 && !busy_r && error_pending_r &&
                           fault_lock && error_code_r == $past(fault_code_next)))
        else $error("C08_SVA_FAULT_BOUNDARY");

    // While lock persists, the first code is retained. A reset can clear
    // lock and code on the same edge, so that boundary is permitted.
    ap_code_retention: assert property (@(posedge clk) disable iff (!rst_n)
        fault_lock |=> (!fault_lock || $stable(error_code_r)))
        else $error("C08_SVA_CODE_RETENTION");

    // A fault during an otherwise pending-free operation cannot publish done.
    ap_no_done_on_fault: assert property (@(posedge clk) disable iff (!rst_n)
        (fault_accept && !done_pending_r) |=> !done_pending_r)
        else $error("C08_SVA_NO_DONE");

    ap_reset_fault: assert property (@(posedge clk)
        !rst_n |=> (!fault_lock && !error_pending_r && error_code_r == 0))
        else $error("C08_SVA_RESET_FAULT");
endmodule

bind top_level_fsm cnn_c08_fault_sva c08_fault_sva_inst (
    .clk(clk), .rst_n(rst_n), .state(state),
    .fault_accept(fault_accept), .fault_lock(fault_lock),
    .fault_code_next(fault_code_next), .error_code_r(error_code_r),
    .busy_r(busy_r), .done_pending_r(done_pending_r),
    .error_pending_r(error_pending_r)
);
