`timescale 1ns / 1ps
// CNN-v4.0 top-level controller. Verilog-2001.
module top_level_fsm (
    input wire clk,
    input wire rst_n,
    output wire input_cfg_valid,
    input wire input_cfg_ready,
    output wire [255:0] input_cfg_desc,
    input wire input_done,
    input wire input_fault,
    output wire line_cfg_valid,
    input wire line_cfg_ready,
    output wire [255:0] line_cfg_desc,
    input wire line_done,
    input wire line_fault,
    output wire dw_cfg_valid,
    input wire dw_cfg_ready,
    output wire [255:0] dw_cfg_desc,
    input wire dw_done,
    input wire dw_fault,
    output wire pw_cfg_valid,
    input wire pw_cfg_ready,
    output wire [255:0] pw_cfg_desc,
    input wire pw_done,
    input wire pw_fault,
    output wire fm_cfg_valid,
    input wire fm_cfg_ready,
    output wire [255:0] fm_cfg_desc,
    input wire fm_done,
    input wire fm_fault,
    output wire [31:0] fm_src_addr,
    output wire [31:0] fm_dst_addr,
    output wire [19:0] fm_src_bytes,
    output wire [19:0] fm_dst_bytes,
    output wire fm_read_en,
    output wire fm_write_en,
    output wire fm_read_dma_done,
    output wire fm_write_dma_done,
    output wire fm_dma_error,
    input wire fm_read_done,
    input wire fm_write_done,
    output wire down_cfg_valid,
    input wire down_cfg_ready,
    output wire [255:0] down_cfg_desc,
    input wire down_done,
    input wire down_fault,
    output wire arg_cfg_valid,
    input wire arg_cfg_ready,
    output wire [255:0] arg_cfg_desc,
    input wire arg_done,
    input wire arg_fault,
    output wire coord_cfg_valid,
    input wire coord_cfg_ready,
    output wire [255:0] coord_cfg_desc,
    input wire coord_done,
    input wire coord_fault,
    input wire [31:0] coord_m_joint_data,
    input wire coord_m_joint_valid,
    output wire coord_m_joint_ready,
    input wire coord_m_joint_last,
    input wire [4:0] coord_m_joint_index,
    input wire coord_m_joint_good,
    output wire [7:0] coord_threshold,
    input wire swap_fault,
    output wire swap_load_valid,
    input wire swap_load_ready,
    output wire [255:0] swap_load_desc,
    input wire swap_load_done,
    input wire color_fault,
    output wire color_frame_start,
    output wire [23:0] color_red_cfg,
    output wire [23:0] color_blue_cfg,
    output wire [23:0] color_green_cfg,
    output wire [23:0] color_margin_cfg,
    output wire [2:0] color_enable,
    output wire [17:0] color_min_count,
    input wire [31:0] color_red_word,
    input wire [31:0] color_blue_word,
    input wire [31:0] color_green_word,
    input wire color_results_valid,
    input wire rom_fault,
    output wire rom_req_valid,
    input wire rom_req_ready,
    output wire [4:0] rom_req_op,
    input wire rom_rsp_valid,
    output wire rom_rsp_ready,
    input wire [255:0] rom_rsp_desc,
    input wire [7:0] threshold_cfg,
    input wire [23:0] red_thresh_cfg,
    input wire [23:0] blue_thresh_cfg,
    input wire [23:0] green_thresh_cfg,
    input wire [23:0] color_margin_cfg_in,
    input wire [2:0] color_enable_cfg,
    input wire [17:0] min_count_cfg,
    input wire datapath_progress,
    input wire start,
    input wire clear_done,
    input wire clear_error,
    input wire [31:0] frame_id,
    input wire [31:0] sg_desc_base,
    input wire [31:0] wgt_base,
    input wire [31:0] fm_a_base,
    input wire [31:0] fm_b_base,
    input wire [31:0] timeout_cycles,
    output wire busy,
    output wire done_pending,
    output wire error_pending,
    output wire [31:0] error_code,
    output wire [31:0] result_seq,
    output wire [31:0] result_frame_id,
    output wire [543:0] joint_words,
    output wire [16:0] joint_flags,
    output wire [31:0] red_word,
    output wire [31:0] blue_word,
    output wire [31:0] green_word,
    output wire [31:0] cycle_count,
    output wire image_read_done,
    output wire [9:0] debug_fault_sources,
    output wire [4:0] debug_state,
    output wire [3:0] debug_stage,
    output wire [3:0] debug_step,
    output wire [2:0] debug_txn_state,
    output wire [31:0] debug_txn_addr,
    output wire [31:0] debug_txn_data,
    output wire [31:0] m_axil_awaddr,
    output wire m_axil_awvalid,
    input wire m_axil_awready,
    output wire [2:0] m_axil_awprot,
    output wire [31:0] m_axil_wdata,
    output wire [3:0] m_axil_wstrb,
    output wire m_axil_wvalid,
    input wire m_axil_wready,
    input wire [1:0] m_axil_bresp,
    input wire m_axil_bvalid,
    output wire m_axil_bready,
    output wire [31:0] m_axil_araddr,
    output wire m_axil_arvalid,
    input wire m_axil_arready,
    output wire [2:0] m_axil_arprot,
    input wire [31:0] m_axil_rdata,
    input wire [1:0] m_axil_rresp,
    input wire m_axil_rvalid,
    output wire m_axil_rready
);

localparam [4:0] S_IDLE=0, S_SNAPSHOT=1, S_DESC_REQ=2, S_DESC_WAIT=3,
    S_LOAD_REQ=4, S_LOAD_DMA=5, S_LOAD_WAIT=6, S_CONFIG=7,
    S_ARM_DEST=8, S_MODULE_CFG=9, S_ARM_SOURCE=10, S_RUN=11,
    S_IMAGE_STOP=12, S_IMAGE_HALT=13, S_NEXT=14, S_POST=15,
    S_PUBLISH=16, S_ERROR=17, S_PREP_HW=18, S_PREP_PART=19,
    S_PREP_SUM=20;
localparam [2:0] T_IDLE=0, T_WRITE=1, T_B=2, T_AR=3, T_R=4;
localparam [31:0] IMAGE_BASE=32'h40400000, WEIGHT_BASE=32'h40410000,
    FEATURE_BASE=32'h40420000;
localparam [31:0] DMA_ERROR_MASK=32'h00000770;

reg [4:0] state, previous_state;
reg [3:0] step, previous_step;
reg [3:0] stage;
reg load_part;
reg [255:0] dw_desc, pw_desc;
reg [31:0] snap_frame_id, snap_sg_base, snap_wgt_base;
reg [31:0] snap_a_base, snap_b_base, snap_timeout;
reg [7:0] snap_threshold;
reg [23:0] snap_red_thresh, snap_blue_thresh, snap_green_thresh;
reg [23:0] snap_color_margin;
reg [2:0] snap_color_enable;
reg [17:0] snap_min_count;
reg [6:0] cfg_seen;
reg [2:0] status_pending;
reg weight_dma_idle, image_dma_idle;
reg seen_down_done, seen_input_done, seen_line_done, seen_dw_done;
reg seen_pw_done, seen_arg_done, seen_coord_done, seen_fm_done;
reg seen_swap_load_done, seen_weight_dma_done, seen_image_dma_done;
reg seen_feature_mm2s_done, seen_feature_s2mm_done;
reg seen_color_results;
reg seen_joint_last;
reg [16:0] joint_seen, shadow_flags;
reg [543:0] shadow_joints;
reg [31:0] shadow_red, shadow_blue, shadow_green;
reg load_inflight;
reg busy_r, done_pending_r, error_pending_r, fault_lock;
reg published_bank;
reg [31:0] error_code_r, result_seq_r, result_frame_id_r;
reg [543:0] joint_words_r;
reg [16:0] joint_flags_r;
reg [31:0] red_word_r, blue_word_r, green_word_r, cycle_count_r, watchdog;
reg image_read_done_r, color_frame_start_r;
reg watchdog_timeout_pending;

reg [2:0] txn_state;
reg txn_aw_seen, txn_w_seen, txn_write, txn_status;
reg [31:0] txn_addr, txn_wdata, txn_rdata;
reg txn_done, txn_error;

wire [4:0] requested_op = (stage == 4'd0) ? 5'd0 :
    (stage == 4'd14) ? 5'd27 : (stage == 4'd15) ? 5'd28 :
    ({stage,1'b0} - 5'd1 + {4'b0,load_part});
wire [255:0] load_desc = load_part ? pw_desc : dw_desc;
wire body_stage = (stage >= 4'd1 && stage <= 4'd13);
wire head_stage = (stage >= 4'd14);
wire [31:0] selected_src = (stage == 4'd0) ? 32'd0 :
    (head_stage || stage[0]) ? snap_a_base : snap_b_base;
wire [31:0] selected_head_src = head_stage ? snap_b_base : selected_src;
wire [31:0] selected_dst = (stage == 4'd0 || !stage[0]) ?
    snap_a_base : snap_b_base;
wire [255:0] dst_geom_desc = (stage==0) ? dw_desc : pw_desc;
(* use_dsp = "no" *) reg [17:0] src_hw, dst_hw;
(* use_dsp = "no" *) reg [17:0] src_part_lo, src_part_hi;
(* use_dsp = "no" *) reg [17:0] dst_part_lo, dst_part_hi;
reg [19:0] src_tensor_bytes, dst_tensor_bytes;
reg [255:0] fm_desc_r;
always @* begin
    fm_desc_r = dw_desc;
    if (body_stage) begin
        fm_desc_r[35:27] = pw_desc[35:27];
        fm_desc_r[44:36] = pw_desc[44:36];
        fm_desc_r[62:54] = pw_desc[62:54];
    end
    fm_desc_r[255:149] = 107'd0;
end

assign input_cfg_desc=dw_desc;
assign line_cfg_desc=dw_desc;
assign dw_cfg_desc=dw_desc;
assign pw_cfg_desc=body_stage ? pw_desc : dw_desc;
assign fm_cfg_desc=fm_desc_r;
assign down_cfg_desc=dw_desc;
assign arg_cfg_desc=dw_desc;
assign coord_cfg_desc=dw_desc;
assign swap_load_desc=load_desc;
assign input_cfg_valid=(state==S_MODULE_CFG && stage==0 && !cfg_seen[0]);
assign line_cfg_valid=(state==S_MODULE_CFG && body_stage && !cfg_seen[1]);
assign dw_cfg_valid=(state==S_MODULE_CFG && body_stage && !cfg_seen[2]);
assign pw_cfg_valid=(state==S_MODULE_CFG && stage!=0 && !cfg_seen[3]);
assign down_cfg_valid=(state==S_MODULE_CFG && stage==0 && !cfg_seen[4]);
assign arg_cfg_valid=(state==S_MODULE_CFG && head_stage && !cfg_seen[5]);
assign coord_cfg_valid=(state==S_MODULE_CFG && stage==15 && !cfg_seen[6]);
assign fm_cfg_valid=(state==S_CONFIG);
assign fm_src_addr=selected_head_src;
assign fm_dst_addr=head_stage ? 32'd0 : selected_dst;
assign fm_src_bytes=(stage==0) ? 20'd0 : src_tensor_bytes;
assign fm_dst_bytes=head_stage ? 20'd0 : dst_tensor_bytes;
assign fm_read_en=(stage!=0);
assign fm_write_en=(state==S_CONFIG) && !head_stage;
assign fm_read_dma_done=seen_feature_mm2s_done;
assign fm_write_dma_done=seen_feature_s2mm_done;
assign fm_dma_error=fault_lock && error_code_r[2];
assign coord_m_joint_ready=(state==S_RUN || state==S_POST) &&
    stage==15 && !fault_lock && !(&joint_seen);
assign coord_threshold=snap_threshold;
assign color_frame_start=color_frame_start_r;
assign color_red_cfg=snap_red_thresh;
assign color_blue_cfg=snap_blue_thresh;
assign color_green_cfg=snap_green_thresh;
assign color_margin_cfg=snap_color_margin;
assign color_enable=snap_color_enable;
assign color_min_count=snap_min_count;
assign rom_req_valid=(state==S_DESC_REQ);
assign rom_req_op=requested_op;
assign rom_rsp_ready=(state==S_DESC_WAIT);
assign swap_load_valid=(state==S_LOAD_REQ);
assign busy=busy_r;
assign done_pending=done_pending_r;
assign error_pending=error_pending_r;
assign error_code=error_code_r;
assign result_seq=result_seq_r;
// The existing public and shadow stores are the two result banks.  Bank 0 is
// the reset-visible bank.  Capture always targets the inactive bank and only
// the narrow selector changes on a successful publish.
assign result_frame_id=published_bank ? snap_frame_id : result_frame_id_r;
assign joint_words=published_bank ? shadow_joints : joint_words_r;
assign joint_flags=published_bank ? shadow_flags : joint_flags_r;
assign red_word=published_bank ? shadow_red : red_word_r;
assign blue_word=published_bank ? shadow_blue : blue_word_r;
assign green_word=published_bank ? shadow_green : green_word_r;
assign cycle_count=cycle_count_r;
assign image_read_done=image_read_done_r;
assign debug_state=state;
assign debug_stage=stage;
assign debug_step=step;
assign debug_txn_state=txn_state;
assign debug_txn_addr=txn_addr;
assign debug_txn_data=txn_rdata;

assign m_axil_awaddr=txn_addr;
assign m_axil_awvalid=(txn_state==T_WRITE && !txn_aw_seen);
assign m_axil_awprot=3'b000;
assign m_axil_wdata=txn_wdata;
assign m_axil_wstrb=m_axil_wvalid ? 4'hf : 4'h0;
assign m_axil_wvalid=(txn_state==T_WRITE && !txn_w_seen);
assign m_axil_bready=(txn_state==T_B);
assign m_axil_araddr=txn_addr;
assign m_axil_arvalid=(txn_state==T_AR);
assign m_axil_arprot=3'b000;
assign m_axil_rready=(txn_state==T_R);

wire [6:0] cfg_accept = {
    coord_cfg_valid && coord_cfg_ready,
    arg_cfg_valid && arg_cfg_ready,
    down_cfg_valid && down_cfg_ready,
    pw_cfg_valid && pw_cfg_ready,
    dw_cfg_valid && dw_cfg_ready,
    line_cfg_valid && line_cfg_ready,
    input_cfg_valid && input_cfg_ready};
wire [6:0] cfg_required=(stage==0) ? 7'b0010001 :
    body_stage ? 7'b0001110 :
    (stage==14) ? 7'b0101000 : 7'b1101000;
wire joint_accept=coord_m_joint_valid && coord_m_joint_ready;
wire joint_protocol_fault=joint_accept &&
    (coord_m_joint_index > 5'd16 || joint_seen[coord_m_joint_index] ||
     (coord_m_joint_last && coord_m_joint_index != 5'd16) ||
     (coord_m_joint_index == 5'd16 && !coord_m_joint_last));
wire module_fault=input_fault || line_fault || dw_fault || pw_fault ||
    fm_fault || down_fault || arg_fault || coord_fault || color_fault ||
    joint_protocol_fault;
assign debug_fault_sources={joint_protocol_fault,color_fault,coord_fault,
    arg_fault,down_fault,fm_fault,pw_fault,dw_fault,line_fault,input_fault};
wire descriptor_fault=rom_rsp_valid && rom_rsp_ready &&
    rom_rsp_desc[4:0]!=requested_op;
wire status_fault=txn_done && txn_status &&
    ((txn_rdata & DMA_ERROR_MASK)!=32'd0);
wire active=(state!=S_IDLE && state!=S_ERROR);
wire progress_event=datapath_progress ||
    (state!=previous_state) ||
    ((step!=previous_step) && state!=S_RUN && state!=S_LOAD_WAIT &&
     state!=S_IMAGE_HALT) ||
    (|cfg_accept) || (rom_req_valid && rom_req_ready) ||
    (rom_rsp_valid && rom_rsp_ready) ||
    (swap_load_valid && swap_load_ready) || joint_accept ||
    (m_axil_awvalid && m_axil_awready) ||
    (m_axil_wvalid && m_axil_wready) ||
    (m_axil_bvalid && m_axil_bready) ||
    (m_axil_arvalid && m_axil_arready && !txn_status) ||
    (m_axil_rvalid && m_axil_rready && !txn_status);
wire [31:0] watchdog_limit=(snap_timeout==0) ? 32'd100000000 :
    snap_timeout;
wire watchdog_fault=active && watchdog_timeout_pending &&
    !progress_event;
wire [31:0] fault_code_next = (txn_done && txn_error) ? 32'h2 :
    status_fault ? 32'h4 :
    (rom_fault || swap_fault || descriptor_fault) ? 32'h10 :
    module_fault ? 32'h1 : watchdog_fault ? 32'h8 : 32'd0;
wire fault_accept=(fault_code_next!=0) && !fault_lock;
wire stage_complete=(stage==0) ?
    (seen_down_done && seen_input_done && seen_fm_done && fm_write_done &&
     seen_feature_s2mm_done && seen_image_dma_done) :
    body_stage ?
    (seen_line_done && seen_dw_done && seen_pw_done && seen_fm_done &&
     fm_read_done && fm_write_done && seen_feature_mm2s_done &&
     seen_feature_s2mm_done) :
    (stage==14) ?
    (seen_pw_done && seen_arg_done && seen_fm_done && fm_read_done &&
     seen_feature_mm2s_done) :
    (seen_pw_done && seen_arg_done && seen_coord_done && seen_fm_done &&
     fm_read_done && seen_feature_mm2s_done && (&joint_seen) &&
     seen_joint_last);

task launch_read;
    input [31:0] addr;
    input is_status;
    begin
        txn_addr <= addr;
        txn_status <= is_status;
        txn_write <= 1'b0;
        txn_state <= T_AR;
    end
endtask

always @(posedge clk) begin
    if (!rst_n) begin
        state<=S_IDLE; previous_state<=S_IDLE;
        step<=0; previous_step<=0; stage<=0; load_part<=0;
        dw_desc<=0; pw_desc<=0;
        src_hw<=0; dst_hw<=0;
        src_part_lo<=0; src_part_hi<=0;
        dst_part_lo<=0; dst_part_hi<=0;
        src_tensor_bytes<=0; dst_tensor_bytes<=0;
        snap_frame_id<=0; snap_sg_base<=0; snap_wgt_base<=0;
        snap_a_base<=0; snap_b_base<=0; snap_timeout<=0;
        snap_threshold<=8'hd2;
        snap_red_thresh<=24'h6464a0; snap_blue_thresh<=24'ha06464;
        snap_green_thresh<=24'h40a040; snap_color_margin<=24'h404010;
        snap_color_enable<=3'b011;
        snap_min_count<=18'd8; cfg_seen<=0; status_pending<=0;
        weight_dma_idle<=0; image_dma_idle<=0;
        seen_down_done<=0; seen_input_done<=0; seen_line_done<=0;
        seen_dw_done<=0; seen_pw_done<=0; seen_arg_done<=0;
        seen_coord_done<=0; seen_fm_done<=0;
        seen_swap_load_done<=0; seen_weight_dma_done<=0;
        seen_image_dma_done<=0; seen_feature_mm2s_done<=0;
        seen_feature_s2mm_done<=0; seen_color_results<=0;
        seen_joint_last<=0;
        joint_seen<=0; shadow_flags<=0; shadow_joints<=0;
        shadow_red<=0; shadow_blue<=0; shadow_green<=0; load_inflight<=0;
        busy_r<=0; done_pending_r<=0; error_pending_r<=0;
        published_bank<=0;
        fault_lock<=0; error_code_r<=0; result_seq_r<=0;
        result_frame_id_r<=0; joint_words_r<=0; joint_flags_r<=0;
        red_word_r<=0; blue_word_r<=0; green_word_r<=0; cycle_count_r<=0;
        watchdog<=0; watchdog_timeout_pending<=0;
        image_read_done_r<=0; color_frame_start_r<=0;
        txn_state<=T_IDLE; txn_aw_seen<=0; txn_w_seen<=0;
        txn_write<=0; txn_status<=0; txn_addr<=0; txn_wdata<=0;
        txn_rdata<=0; txn_done<=0; txn_error<=0;
    end else begin
        previous_state<=state;
        previous_step<=step;
        txn_done<=1'b0;
        color_frame_start_r<=1'b0;
        if (active && !fault_lock) begin
            if (progress_event) begin
                watchdog<=0;
                watchdog_timeout_pending<=0;
            end else begin
                watchdog<=watchdog+1'b1;
                if (watchdog>=watchdog_limit-1'b1)
                    watchdog_timeout_pending<=1;
            end
        end else begin
            watchdog<=0;
            watchdog_timeout_pending<=0;
        end
        if (busy_r) cycle_count_r<=cycle_count_r+1'b1;
        if (clear_done) done_pending_r<=1'b0;
        if (clear_error) error_pending_r<=1'b0;
        // Clear only the inactive/working bank.  This is intentionally outside
        // the fault boundary: the selected published bank remains untouched.
        if (state==S_SNAPSHOT) begin
            seen_color_results<=0;
            joint_seen<=0;
            seen_joint_last<=0;
            if (published_bank) begin
                joint_flags_r<=0;
                joint_words_r<=0;
                red_word_r<=0;
                blue_word_r<=0;
                green_word_r<=0;
            end else begin
                shadow_flags<=0;
                shadow_joints<=0;
                shadow_red<=0;
                shadow_blue<=0;
                shadow_green<=0;
            end
        end

        // Single-outstanding AXI-Lite transaction engine. AW and W retire
        // independently; neither channel relies on the other's ready timing.
        case (txn_state)
            T_IDLE: begin end
            T_WRITE: begin
                if (m_axil_awvalid && m_axil_awready) txn_aw_seen<=1'b1;
                if (m_axil_wvalid && m_axil_wready) txn_w_seen<=1'b1;
                if ((txn_aw_seen || (m_axil_awvalid && m_axil_awready)) &&
                    (txn_w_seen || (m_axil_wvalid && m_axil_wready)))
                    txn_state<=T_B;
            end
            T_B: if (m_axil_bvalid) begin
                txn_error<=|m_axil_bresp;
                txn_done<=1'b1;
                txn_state<=T_IDLE;
            end
            T_AR: if (m_axil_arready) txn_state<=T_R;
            T_R: if (m_axil_rvalid) begin
                txn_rdata<=m_axil_rdata;
                txn_error<=|m_axil_rresp;
                txn_done<=1'b1;
                txn_state<=T_IDLE;
            end
            default: txn_state<=T_IDLE;
        endcase

        if (active && !fault_lock) begin
            if (down_done && stage==0) seen_down_done<=1;
            if (input_done && stage==0) seen_input_done<=1;
            if (line_done && body_stage) seen_line_done<=1;
            if (dw_done && body_stage) seen_dw_done<=1;
            if (pw_done && stage!=0) seen_pw_done<=1;
            if (arg_done && head_stage) seen_arg_done<=1;
            if (coord_done && stage==15) seen_coord_done<=1;
            if (fm_done) seen_fm_done<=1;
            if (swap_load_done && load_inflight) seen_swap_load_done<=1;
            if (color_results_valid) begin
                if (published_bank) begin
                    red_word_r<=color_red_word;
                    blue_word_r<=color_blue_word;
                    green_word_r<=color_green_word;
                end else begin
                    shadow_red<=color_red_word;
                    shadow_blue<=color_blue_word;
                    shadow_green<=color_green_word;
                end
                seen_color_results<=1;
            end
            if (joint_accept && !joint_protocol_fault) begin
                if (published_bank) begin
                    joint_words_r[coord_m_joint_index*32 +: 32]
                        <=coord_m_joint_data;
                    joint_flags_r[coord_m_joint_index]<=coord_m_joint_good;
                end else begin
                    shadow_joints[coord_m_joint_index*32 +: 32]
                        <=coord_m_joint_data;
                    shadow_flags[coord_m_joint_index]<=coord_m_joint_good;
                end
                joint_seen[coord_m_joint_index]<=1;
                if (coord_m_joint_last) seen_joint_last<=1;
            end
        end

        // State-local/private updates are no longer placed behind the raw
        // fault decode.  A final narrow override below preserves fault
        // priority for all externally visible control.
        case (state)
            S_IDLE: begin
                step<=0;
                if (start && !fault_lock && !done_pending_r) begin
                    state<=S_SNAPSHOT;
                    busy_r<=1;
                    cycle_count_r<=0;
                    image_read_done_r<=0;
                end
            end
            S_SNAPSHOT: begin
                if (published_bank) result_frame_id_r<=frame_id;
                else snap_frame_id<=frame_id;
                snap_sg_base<=sg_desc_base;
                snap_wgt_base<=wgt_base;
                snap_a_base<=fm_a_base;
                snap_b_base<=fm_b_base;
                snap_timeout<=timeout_cycles;
                snap_threshold<=threshold_cfg;
                snap_red_thresh<=red_thresh_cfg;
                snap_blue_thresh<=blue_thresh_cfg;
                snap_green_thresh<=green_thresh_cfg;
                snap_color_margin<=color_margin_cfg_in;
                snap_color_enable<=color_enable_cfg;
                snap_min_count<=min_count_cfg;
                color_frame_start_r<=1'b1;
                stage<=0;
                load_part<=0;
                cfg_seen<=0;
                seen_down_done<=0; seen_input_done<=0;
                seen_line_done<=0; seen_dw_done<=0; seen_pw_done<=0;
                seen_arg_done<=0; seen_coord_done<=0; seen_fm_done<=0;
                seen_feature_mm2s_done<=0;
                seen_feature_s2mm_done<=0;
                seen_image_dma_done<=0;
                state<=S_DESC_REQ;
            end
            S_DESC_REQ: if (rom_req_ready) state<=S_DESC_WAIT;
            S_DESC_WAIT: if (rom_rsp_valid) begin
                if (load_part) pw_desc<=rom_rsp_desc;
                else dw_desc<=rom_rsp_desc;
                state<=S_LOAD_REQ;
            end
            S_LOAD_REQ: if (swap_load_ready) begin
                seen_swap_load_done<=swap_load_done;
                seen_weight_dma_done<=0;
                weight_dma_idle<=0;
                load_inflight<=1;
                step<=0;
                state<=S_LOAD_DMA;
            end
            S_LOAD_DMA: begin
                if (txn_done) begin
                    case (step)
                        0: begin
                            status_pending<=txn_rdata[14:12];
                            step<= (|txn_rdata[14:12]) ? 4'd1 : 4'd2;
                        end
                        1: step<=2;
                        2: step<=3;
                        3: step<=4;
                        4: begin step<=0; state<=S_LOAD_WAIT; end
                        default: step<=0;
                    endcase
                end else if (txn_state==T_IDLE) begin
                    case (step)
                        0: launch_read(WEIGHT_BASE+32'h04,1'b1);
                        1: launch_write(WEIGHT_BASE+32'h04,
                                        {17'd0,status_pending,12'd0});
                        2: launch_write(WEIGHT_BASE+32'h00,32'h1);
                        3: launch_write(WEIGHT_BASE+32'h18,
                                        snap_wgt_base+load_desc[106:75]);
                        4: launch_write(WEIGHT_BASE+32'h28,
                                        {12'd0,load_desc[144:125]});
                        default: step<=0;
                    endcase
                end
            end
            S_LOAD_WAIT: begin
                if (seen_swap_load_done && weight_dma_idle &&
                    txn_state==T_IDLE && !txn_done) begin
                    seen_weight_dma_done<=1;
                    load_inflight<=0;
                    if (body_stage && !load_part) begin
                        load_part<=1;
                        state<=S_DESC_REQ;
                    end else state<=S_PREP_HW;
                end else if (txn_done) begin
                    weight_dma_idle<=txn_rdata[1];
                end else if (txn_state==T_IDLE)
                    launch_read(WEIGHT_BASE+32'h04,1'b1);
            end
            S_PREP_HW: begin
                src_hw<=dw_desc[17:9]*dw_desc[26:18];
                dst_hw<=dst_geom_desc[35:27]*dst_geom_desc[44:36];
                state<=S_PREP_PART;
            end
            S_PREP_PART: begin
                src_part_lo<=src_hw[8:0]*dw_desc[53:45];
                src_part_hi<=src_hw[17:9]*dw_desc[53:45];
                dst_part_lo<=dst_hw[8:0]*dst_geom_desc[62:54];
                dst_part_hi<=dst_hw[17:9]*dst_geom_desc[62:54];
                state<=S_PREP_SUM;
            end
            S_PREP_SUM: begin
                src_tensor_bytes<=src_part_lo+{src_part_hi,9'b0};
                dst_tensor_bytes<=dst_part_lo+{dst_part_hi,9'b0};
                state<=S_CONFIG;
            end
            S_CONFIG: if (fm_cfg_ready) begin
                step<=0;
                state<=S_ARM_DEST;
            end
            S_ARM_DEST: begin
                if (head_stage) begin
                    cfg_seen<=0;
                    state<=S_MODULE_CFG;
                end else if (txn_done) begin
                    case (step)
                        0: begin
                            status_pending<=txn_rdata[14:12];
                            step<= (|txn_rdata[14:12]) ? 4'd1 : 4'd2;
                        end
                        1: step<=2;
                        2: step<=3;
                        3: step<=4;
                        4: begin
                            step<=0;
                            cfg_seen<=0;
                            state<=S_MODULE_CFG;
                        end
                        default: step<=0;
                    endcase
                end else if (txn_state==T_IDLE) begin
                    case (step)
                        0: launch_read(FEATURE_BASE+32'h34,1'b1);
                        1: launch_write(FEATURE_BASE+32'h34,
                                        {17'd0,status_pending,12'd0});
                        2: launch_write(FEATURE_BASE+32'h30,32'h1);
                        3: launch_write(FEATURE_BASE+32'h48,selected_dst);
                        4: launch_write(FEATURE_BASE+32'h58,
                                        {12'd0,fm_dst_bytes});
                        default: step<=0;
                    endcase
                end
            end
            S_MODULE_CFG: begin
                cfg_seen<=cfg_seen | cfg_accept;
                if (((cfg_seen | cfg_accept) & cfg_required)==cfg_required) begin
                    step<=0;
                    state<=S_ARM_SOURCE;
                end
            end
            S_ARM_SOURCE: begin
                if (txn_done) begin
                    if (stage==0) begin
                        case (step)
                            0: if (txn_rdata[0]) step<=1;
                            1: step<=2;
                            2: step<=3;
                            3: begin step<=0; state<=S_RUN; end
                            default: step<=0;
                        endcase
                    end else begin
                        case (step)
                            0: begin
                                status_pending<=txn_rdata[14:12];
                                step<= (|txn_rdata[14:12]) ? 4'd1 : 4'd2;
                            end
                            1: step<=2;
                            2: step<=3;
                            3: step<=4;
                            4: begin step<=0; state<=S_RUN; end
                            default: step<=0;
                        endcase
                    end
                end else if (txn_state==T_IDLE) begin
                    if (stage==0) begin
                        case (step)
                            0: launch_read(IMAGE_BASE+32'h04,1'b1);
                            1: launch_write(IMAGE_BASE+32'h08,snap_sg_base);
                            2: launch_write(IMAGE_BASE+32'h00,32'h1);
                            3: launch_write(IMAGE_BASE+32'h10,
                                            snap_sg_base+32'd9152);
                            default: step<=0;
                        endcase
                    end else begin
                        case (step)
                            0: launch_read(FEATURE_BASE+32'h04,1'b1);
                            1: launch_write(FEATURE_BASE+32'h04,
                                            {17'd0,status_pending,12'd0});
                            2: launch_write(FEATURE_BASE+32'h00,32'h1);
                            3: launch_write(FEATURE_BASE+32'h18,
                                            selected_head_src);
                            4: launch_write(FEATURE_BASE+32'h28,
                                            {12'd0,fm_src_bytes});
                            default: step<=0;
                        endcase
                    end
                end
            end
            S_RUN: begin
                if (stage_complete && txn_state==T_IDLE && !txn_done) begin
                    step<=0;
                    if (stage==0) state<=S_IMAGE_STOP;
                    else state<=S_NEXT;
                end else if (txn_done) begin
                    if (txn_addr==FEATURE_BASE+32'h34) begin
                        if (txn_rdata[1]) seen_feature_s2mm_done<=1;
                        step<=1;
                    end else if (txn_addr==FEATURE_BASE+32'h04) begin
                        if (txn_rdata[1]) seen_feature_mm2s_done<=1;
                        step<=0;
                    end else if (txn_addr==IMAGE_BASE+32'h04) begin
                        image_dma_idle<=txn_rdata[1];
                        if (txn_rdata[1]) seen_image_dma_done<=1;
                        step<=0;
                    end
                end else if (txn_state==T_IDLE) begin
                    if (stage==0) begin
                        if (step==0) launch_read(FEATURE_BASE+32'h34,1'b1);
                        else launch_read(IMAGE_BASE+32'h04,1'b1);
                    end else if (head_stage) begin
                        launch_read(FEATURE_BASE+32'h04,1'b1);
                    end else begin
                        if (step==0) launch_read(FEATURE_BASE+32'h34,1'b1);
                        else launch_read(FEATURE_BASE+32'h04,1'b1);
                    end
                end
            end
            S_IMAGE_STOP: begin
                if (txn_done) begin
                    state<=S_IMAGE_HALT;
                end else if (txn_state==T_IDLE)
                    launch_write(IMAGE_BASE+32'h00,32'd0);
            end
            S_IMAGE_HALT: begin
                if (txn_done) begin
                    if (txn_rdata[0]) begin
                        image_read_done_r<=1;
                        state<=S_NEXT;
                    end
                end else if (txn_state==T_IDLE)
                    launch_read(IMAGE_BASE+32'h04,1'b1);
            end
            S_NEXT: begin
                if (stage==15) state<=S_POST;
                else begin
                    stage<=stage+1'b1;
                    load_part<=0;
                    cfg_seen<=0;
                    seen_down_done<=0; seen_input_done<=0;
                    seen_line_done<=0; seen_dw_done<=0; seen_pw_done<=0;
                    seen_arg_done<=0; seen_coord_done<=0; seen_fm_done<=0;
                    seen_swap_load_done<=0; seen_weight_dma_done<=0;
                    seen_feature_mm2s_done<=0;
                    seen_feature_s2mm_done<=0;
                    state<=S_DESC_REQ;
                end
            end
            S_POST: if (seen_color_results && (&joint_seen))
                state<=S_PUBLISH;
            S_PUBLISH: begin
                if (!fault_accept) begin
                    published_bank<=~published_bank;
                    result_seq_r<=result_seq_r+1'b1;
                    done_pending_r<=1;
                    busy_r<=0;
                    state<=S_IDLE;
                end
            end
            S_ERROR: begin
                busy_r<=0;
            end
            default: begin
                state<=S_ERROR;
                busy_r<=0;
                fault_lock<=1;
                error_pending_r<=1;
                error_code_r<=32'h1;
            end
        endcase

        // Last nonblocking assignments win.  The raw fault cone terminates in
        // this narrow boundary and cannot gate either 544-bit result bank.
        if (fault_accept) begin
            state<=S_ERROR;
            busy_r<=0;
            fault_lock<=1;
            error_pending_r<=1;
            error_code_r<=fault_code_next;
            load_inflight<=0;
            color_frame_start_r<=0;
            image_read_done_r<=image_read_done_r;
            // Suppress a transaction launched from T_IDLE on this edge while
            // allowing an already outstanding transaction to retire exactly
            // as before.
            if (txn_state==T_IDLE)
                txn_state<=T_IDLE;
        end
    end
end
task launch_write;
    input [31:0] addr;
    input [31:0] data;
    begin
        txn_addr <= addr;
        txn_wdata <= data;
        txn_status <= 1'b0;
        txn_write <= 1'b1;
        txn_aw_seen <= 1'b0;
        txn_w_seen <= 1'b0;
        txn_state <= T_WRITE;
    end
endtask

endmodule
