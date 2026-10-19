module cnn_c06_reset_sva (
    input wire clk,
    input wire rst_n,
    input wire [4:0] state,
    input wire busy_r,
    input wire done_pending_r,
    input wire error_pending_r,
    input wire fault_lock,
    input wire [31:0] cycle_count_r
);
    // The FSM uses synchronous active-low reset. Nonblocking assignments
    // become visible at the following sampled rising edge.
    ap_reset_state: assert property (@(posedge clk)
        (!rst_n) |=> (state == 0 && !busy_r && !done_pending_r &&
                      !error_pending_r && !fault_lock && cycle_count_r == 0))
        else $error("C06_SVA_RESET_STATE");
endmodule

bind top_level_fsm cnn_c06_reset_sva c06_reset_sva_inst (
    .clk(clk), .rst_n(rst_n), .state(state), .busy_r(busy_r),
    .done_pending_r(done_pending_r), .error_pending_r(error_pending_r),
    .fault_lock(fault_lock), .cycle_count_r(cycle_count_r)
);
