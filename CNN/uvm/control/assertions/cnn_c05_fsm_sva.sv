// Short local rules for C05. The event checker handles long progression.
module cnn_c05_fsm_sva (
    input wire clk,
    input wire rst_n,
    input wire [4:0] state,
    input wire [3:0] stage,
    input wire [6:0] cfg_seen,
    input wire [6:0] cfg_accept,
    input wire [6:0] cfg_required,
    input wire stage_complete,
    input wire fault_accept,
    input wire busy_r,
    input wire done_pending_r,
    input wire error_pending_r
);
    localparam [4:0] S_SNAPSHOT = 1, S_MODULE_CFG = 9,
                     S_ARM_SOURCE = 10, S_RUN = 11,
                     S_IMAGE_STOP = 12, S_NEXT = 14,
                     S_PUBLISH = 16, S_IDLE = 0;

    // A missing configuration acceptance holds MODULE_CFG on the next edge.
    ap_cfg_barrier: assert property (@(posedge clk) disable iff (!rst_n)
        (state == S_MODULE_CFG && !fault_accept &&
         ((cfg_seen | cfg_accept) & cfg_required) != cfg_required)
        |=> state == S_MODULE_CFG)
        else $error("C05_SVA_CFG_BARRIER");

    // RUN cannot take its stage-exit path until all completion inputs agree.
    ap_run_complete: assert property (@(posedge clk) disable iff (!rst_n)
        (state == S_RUN && !stage_complete && !fault_accept)
        |=> state == S_RUN)
        else $error("C05_SVA_RUN_COMPLETE");

    // Stage advances only out of NEXT; SNAPSHOT legitimately restarts at 0.
    ap_stage_step: assert property (@(posedge clk) disable iff (!rst_n)
        (stage != $past(stage) &&
         !($past(state) == S_SNAPSHOT && stage == 0))
        |-> (stage == $past(stage) + 4'd1 && $past(state) == S_NEXT))
        else $error("C05_SVA_STAGE_STEP");

    // On the edge after leaving RUN, the prior cycle had completion true.
    ap_run_exit: assert property (@(posedge clk) disable iff (!rst_n)
        ($past(state) == S_RUN &&
         (state == S_NEXT || state == S_IMAGE_STOP))
        |-> $past(stage_complete))
        else $error("C05_SVA_RUN_EXIT");

    // PUBLISH commits done and clears busy on the same sequential edge.
    ap_publish: assert property (@(posedge clk) disable iff (!rst_n)
        ($past(state) == S_PUBLISH && state == S_IDLE)
        |-> (done_pending_r && !busy_r && !error_pending_r))
        else $error("C05_SVA_PUBLISH");
endmodule

bind top_level_fsm cnn_c05_fsm_sva c05_fsm_sva_inst (
    .clk(clk), .rst_n(rst_n), .state(state), .stage(stage),
    .cfg_seen(cfg_seen), .cfg_accept(cfg_accept), .cfg_required(cfg_required),
    .stage_complete(stage_complete), .fault_accept(fault_accept),
    .busy_r(busy_r), .done_pending_r(done_pending_r),
    .error_pending_r(error_pending_r)
);
