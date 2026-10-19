`timescale 1ns / 1ps

// CNN_ACCELERATOR_TOP REAL12 STRUCTURAL INTEGRATION
// Twelve released child units are approved real RTL; no stubs are present.
module cnn_accelerator_top (
    input wire clk,
    input wire rst_n,
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
    output wire m_axil_rready,
    input wire [11:0] s_axil_awaddr,
    input wire s_axil_awvalid,
    output wire s_axil_awready,
    input wire [2:0] s_axil_awprot,
    input wire [31:0] s_axil_wdata,
    input wire [3:0] s_axil_wstrb,
    input wire s_axil_wvalid,
    output wire s_axil_wready,
    output wire [1:0] s_axil_bresp,
    output wire s_axil_bvalid,
    input wire s_axil_bready,
    input wire [11:0] s_axil_araddr,
    input wire s_axil_arvalid,
    output wire s_axil_arready,
    input wire [2:0] s_axil_arprot,
    output wire [31:0] s_axil_rdata,
    output wire [1:0] s_axil_rresp,
    output wire s_axil_rvalid,
    input wire s_axil_rready,
    input wire [63:0] s_image_data,
    input wire s_image_valid,
    output wire s_image_ready,
    input wire s_image_last,
    input wire [7:0] s_image_keep,
    input wire [63:0] s_weight_data,
    input wire s_weight_valid,
    output wire s_weight_ready,
    input wire s_weight_last,
    input wire [7:0] s_weight_keep,
    input wire [63:0] s_feature_data,
    input wire s_feature_valid,
    output wire s_feature_ready,
    input wire s_feature_last,
    input wire [7:0] s_feature_keep,
    output wire [63:0] m_feature_data,
    output wire m_feature_valid,
    input wire m_feature_ready,
    output wire m_feature_last,
    output wire [7:0] m_feature_keep,
    output wire irq
);

    wire core_rst_n;
    reg start;
    reg clear_done;
    reg clear_error;
    reg soft_reset_pulse;
    reg [7:0] threshold_cfg;
    reg [23:0] red_thresh_cfg;
    reg [23:0] blue_thresh_cfg;
    reg [23:0] green_thresh_cfg;
    reg [23:0] color_margin_cfg;
    reg [2:0] color_enable_cfg;
    reg [17:0] min_count_cfg;
    reg [31:0] frame_id;
    reg [31:0] sg_desc_base;
    reg [31:0] frame_base;
    reg [31:0] wgt_base;
    reg [31:0] fm_a_base;
    reg [31:0] fm_b_base;
    reg [31:0] timeout_cycles;
    reg irq_enable;

    wire input_cfg_valid;
    wire input_cfg_ready;
    wire [255:0] input_cfg_desc;
    wire input_done;
    wire input_fault;
    wire line_cfg_valid;
    wire line_cfg_ready;
    wire [255:0] line_cfg_desc;
    wire line_done;
    wire line_fault;
    wire dw_cfg_valid;
    wire dw_cfg_ready;
    wire [255:0] dw_cfg_desc;
    wire dw_done;
    wire dw_fault;
    wire pw_cfg_valid;
    wire pw_cfg_ready;
    wire [255:0] pw_cfg_desc;
    wire pw_done;
    wire pw_fault;
    wire fm_cfg_valid;
    wire fm_cfg_ready;
    wire [255:0] fm_cfg_desc;
    wire fm_done;
    wire fm_fault;
    wire [31:0] fm_src_addr;
    wire [31:0] fm_dst_addr;
    wire [19:0] fm_src_bytes;
    wire [19:0] fm_dst_bytes;
    wire fm_read_en;
    wire fm_write_en;
    wire fm_read_dma_done;
    wire fm_write_dma_done;
    wire fm_dma_error;
    wire fm_read_done;
    wire fm_write_done;
    wire down_cfg_valid;
    wire down_cfg_ready;
    wire [255:0] down_cfg_desc;
    wire down_done;
    wire down_fault;
    wire arg_cfg_valid;
    wire arg_cfg_ready;
    wire [255:0] arg_cfg_desc;
    wire arg_done;
    wire arg_fault;
    wire coord_cfg_valid;
    wire coord_cfg_ready;
    wire [255:0] coord_cfg_desc;
    wire coord_done;
    wire coord_fault;
    wire [31:0] coord_m_joint_data;
    wire coord_m_joint_valid;
    wire coord_m_joint_ready;
    wire coord_m_joint_last;
    wire [4:0] coord_m_joint_index;
    wire coord_m_joint_good;
    wire [7:0] coord_threshold;
    wire swap_fault;
    wire swap_load_valid;
    wire swap_load_ready;
    wire [255:0] swap_load_desc;
    wire swap_load_done;
    wire color_fault;
    wire color_frame_start;
    wire [23:0] color_red_cfg;
    wire [23:0] color_blue_cfg;
    wire [23:0] color_green_cfg;
    wire [23:0] color_margin_cfg_active;
    wire [2:0] color_enable_active;
    wire [17:0] color_min_count;
    wire [31:0] color_red_word;
    wire [31:0] color_blue_word;
    wire [31:0] color_green_word;
    wire color_results_valid;
    wire rom_fault;
    wire rom_req_valid;
    wire rom_req_ready;
    wire [4:0] rom_req_op;
    wire rom_rsp_valid;
    wire rom_rsp_ready;
    wire [255:0] rom_rsp_desc;
    wire datapath_progress;
    wire busy;
    wire done_pending;
    wire error_pending;
    wire [31:0] error_code;
    wire [31:0] result_seq;
    wire [31:0] result_frame_id;
    wire [543:0] joint_words;
    wire [16:0] joint_flags;
    wire [31:0] red_word;
    wire [31:0] blue_word;
    wire [31:0] green_word;
    wire [31:0] cycle_count;
    wire image_read_done;
    wire [9:0] debug_fault_sources;
    reg [9:0] debug_fault_sources_latched;
    wire [4:0] debug_state;
    wire [3:0] debug_stage;
    wire [3:0] debug_step;
    wire [2:0] debug_txn_state;
    wire [31:0] debug_txn_addr;
    wire [31:0] debug_txn_data;
    wire [9:0] fm_debug_fault_reason;
    wire [20:0] fm_debug_read_input_bytes;
    wire [20:0] fm_debug_read_parsed_bytes;
    wire [20:0] fm_debug_write_input_bytes;
    wire [20:0] fm_debug_write_packed_bytes;
    wire [20:0] fm_debug_write_output_bytes;
    wire [31:0] fm_debug_stream_status;
    wire [63:0] fm_debug_last_body_tag;
    wire [31:0] fm_debug_protocol_misc;
    wire [19:0] fm_debug_expected_src_bytes;
    wire [19:0] fm_debug_expected_dst_bytes;
    wire [23:0] down_rgb_data;
    wire down_rgb_valid;
    wire down_rgb_ready;
    wire [63:0] down_rgb_tag;
    wire [23:0] down_tap_data;
    wire [9:0] down_tap_row;
    wire [10:0] down_tap_col;
    wire down_tap_accept;
    wire down_tap_last;
    // The DMA/downsample path already presents the detector's frozen
    // The Bayer/Gamma/VDMA path stores each pixel as {R,B,G}.  The frozen
    // color_marker_detect input contract is {B,G,R}, so reorder exactly once
    // at this private tap boundary.  This does not alter the CNN RGB stream.
    wire [23:0] color_tap_bgr = {down_tap_data[15:8],
                                 down_tap_data[7:0],
                                 down_tap_data[23:16]};
    wire [63:0] input_body_data;
    wire input_body_valid;
    wire input_body_ready;
    wire [3:0] input_body_mask;
    wire [63:0] input_body_tag;
    wire [63:0] fm_body_data;
    wire fm_body_valid;
    wire fm_body_ready;
    wire [3:0] fm_body_mask;
    wire [63:0] fm_body_tag;
    wire [255:0] fm_pixel_data;
    wire fm_pixel_valid;
    wire fm_pixel_ready;
    wire [31:0] fm_pixel_mask;
    wire [63:0] fm_pixel_tag;
    wire line_pixel_valid;
    wire line_pixel_ready;
    wire [255:0] line_tap_data;
    wire line_tap_valid;
    wire line_tap_ready;
    wire [31:0] line_tap_mask;
    wire [63:0] line_tap_tag;
    wire [255:0] dw_pixel_data;
    wire dw_pixel_valid;
    wire dw_pixel_ready;
    wire [31:0] dw_pixel_mask;
    wire [63:0] dw_pixel_tag;
    wire [255:0] pw_pixel_data;
    wire pw_pixel_valid;
    wire pw_pixel_ready;
    wire [31:0] pw_pixel_mask;
    wire [63:0] pw_pixel_tag;
    wire [63:0] pw_value_data;
    wire pw_value_valid;
    wire pw_value_ready;
    wire [3:0] pw_value_mask;
    wire [63:0] pw_value_tag;
    wire arg_head_ready;
    wire [63:0] arg_result_data;
    wire arg_result_valid;
    wire arg_result_ready;
    wire arg_result_last;
    wire [4:0] arg_result_joint;
    wire conv0_req_valid;
    wire conv0_req_ready;
    wire [4:0] conv0_req_addr;
    wire conv0_rsp_valid;
    wire conv0_rsp_ready;
    wire [215:0] conv0_rsp_data;
    wire [63:0] conv0_rsp_params;
    wire dw_w_req_valid;
    wire dw_w_req_ready;
    wire [6:0] dw_w_req_addr;
    wire dw_w_rsp_valid;
    wire dw_w_rsp_ready;
    wire [255:0] dw_w_rsp_data;
    wire dw_p_req_valid;
    wire dw_p_req_ready;
    wire [8:0] dw_p_req_addr;
    wire dw_p_rsp_valid;
    wire dw_p_rsp_ready;
    wire [63:0] dw_p_rsp_data;
    wire pw_req_valid;
    wire pw_req_ready;
    wire [10:0] pw_req_addr;
    wire pw_rsp_valid;
    wire pw_rsp_ready;
    wire [1023:0] pw_rsp_data;
    wire [255:0] pw_rsp_params;
    wire [6:0] pw_req_group;

    reg fm_body_from_input_reg;
    reg fm_read_to_head_reg;
    reg pw_head_select_reg;
    wire fm_cfg_fire = fm_cfg_valid && fm_cfg_ready;
    wire pw_cfg_fire = pw_cfg_valid && pw_cfg_ready;
    wire fm_body_from_input = fm_body_from_input_reg;
    wire fm_read_to_head = fm_read_to_head_reg;
    wire pw_head_select = pw_head_select_reg;

    assign fm_body_data  = fm_body_from_input ? input_body_data  : pw_value_data;
    assign fm_body_valid = fm_body_from_input ? input_body_valid :
                           (pw_head_select ? 1'b0 : pw_value_valid);
    assign fm_body_mask  = fm_body_from_input ? input_body_mask  : pw_value_mask;
    assign fm_body_tag   = fm_body_from_input ? input_body_tag   : pw_value_tag;
    assign input_body_ready = fm_body_from_input ? fm_body_ready : 1'b0;
    assign pw_value_ready = pw_head_select ? arg_head_ready :
                            (fm_body_from_input ? 1'b0 : fm_body_ready);

    assign line_pixel_valid = fm_read_to_head ? 1'b0 : fm_pixel_valid;
    assign fm_pixel_ready = fm_read_to_head ? pw_pixel_ready : line_pixel_ready;
    assign pw_pixel_data  = pw_head_select ? fm_pixel_data  : dw_pixel_data;
    assign pw_pixel_valid = pw_head_select ? fm_pixel_valid : dw_pixel_valid;
    assign pw_pixel_mask  = pw_head_select ? fm_pixel_mask  : dw_pixel_mask;
    assign pw_pixel_tag   = pw_head_select ? fm_pixel_tag   : dw_pixel_tag;
    assign dw_pixel_ready = pw_head_select ? 1'b0 : pw_pixel_ready;

    assign datapath_progress =
        (s_image_valid && s_image_ready) ||
        (s_weight_valid && s_weight_ready) ||
        (s_feature_valid && s_feature_ready) ||
        (m_feature_valid && m_feature_ready) ||
        (down_rgb_valid && down_rgb_ready) ||
        (input_body_valid && input_body_ready) ||
        (fm_pixel_valid && fm_pixel_ready) ||
        (line_tap_valid && line_tap_ready) ||
        (dw_pixel_valid && dw_pixel_ready) ||
        (pw_value_valid && pw_value_ready) ||
        (arg_result_valid && arg_result_ready) ||
        (coord_m_joint_valid && coord_m_joint_ready) ||
        (conv0_req_valid && conv0_req_ready) ||
        (dw_w_req_valid && dw_w_req_ready) ||
        (dw_p_req_valid && dw_p_req_ready) ||
        (pw_req_valid && pw_req_ready);

    assign core_rst_n = rst_n && !soft_reset_pulse;
    assign irq = irq_enable && (done_pending || error_pending);

    // Diagnostic-only, read-only observability.  The released control and
    // datapath contracts remain unchanged; the first asserted source is
    // retained until core reset so firmware can identify a sticky fault.
    always @(posedge clk) begin
        if (!core_rst_n)
            debug_fault_sources_latched <= 10'd0;
        else if (|debug_fault_sources)
            debug_fault_sources_latched <= debug_fault_sources_latched |
                                            debug_fault_sources;
    end

    always @(posedge clk) begin
        if (!core_rst_n) begin
            fm_body_from_input_reg <= 1'b0;
            fm_read_to_head_reg <= 1'b0;
            pw_head_select_reg <= 1'b0;
        end else begin
            if (fm_cfg_fire) begin
                fm_body_from_input_reg <= (fm_cfg_desc[4:0] == 5'd0);
                fm_read_to_head_reg <= (fm_cfg_desc[4:0] >= 5'd27);
            end
            if (pw_cfg_fire)
                pw_head_select_reg <= (pw_cfg_desc[4:0] >= 5'd27);
        end
    end

    reg aw_hold;
    reg [11:0] awaddr_hold;
    reg w_hold;
    reg [31:0] wdata_hold;
    reg [3:0] wstrb_hold;
    reg bvalid_reg;
    reg [1:0] bresp_reg;
    reg rvalid_reg;
    reg [31:0] rdata_reg;
    reg [1:0] rresp_reg;

    assign s_axil_awready = rst_n && !aw_hold && !bvalid_reg;
    assign s_axil_wready  = rst_n && !w_hold && !bvalid_reg;
    assign s_axil_bvalid  = bvalid_reg;
    assign s_axil_bresp   = bresp_reg;
    assign s_axil_arready = rst_n && !rvalid_reg;
    assign s_axil_rvalid  = rvalid_reg;
    assign s_axil_rdata   = rdata_reg;
    assign s_axil_rresp   = rresp_reg;

    function [31:0] merge_wstrb;
        input [31:0] old_value;
        input [31:0] new_value;
        input [3:0] strobe;
        integer byte_index;
        begin
            merge_wstrb = old_value;
            for (byte_index = 0; byte_index < 4; byte_index = byte_index + 1)
                if (strobe[byte_index])
                    merge_wstrb[byte_index*8 +: 8] = new_value[byte_index*8 +: 8];
        end
    endfunction

    reg [31:0] read_mux_data;
    reg [1:0] read_mux_resp;
    always @* begin
        read_mux_data = 32'd0;
        read_mux_resp = 2'b00;
        if (s_axil_araddr[1:0] != 2'b00) begin
            read_mux_resp = 2'b10;
        end else begin
            case (s_axil_araddr)
                12'h000: read_mux_data = 32'd0;
                12'h004: read_mux_data = {28'd0, image_read_done,
                                           error_pending, busy, done_pending};
                12'h008: read_mux_data = {24'd0, threshold_cfg};
                12'h00c: read_mux_data = 32'd5;
                12'h010: read_mux_data = 32'd280;
                12'h014: read_mux_data = 32'd0;
                12'h018: read_mux_data = joint_words[31:0];
                12'h01c: read_mux_data = joint_words[63:32];
                12'h020: read_mux_data = joint_words[95:64];
                12'h024: read_mux_data = joint_words[127:96];
                12'h028: read_mux_data = joint_words[159:128];
                12'h02c: read_mux_data = joint_words[191:160];
                12'h030: read_mux_data = joint_words[223:192];
                12'h034: read_mux_data = joint_words[255:224];
                12'h038: read_mux_data = joint_words[287:256];
                12'h03c: read_mux_data = joint_words[319:288];
                12'h040: read_mux_data = joint_words[351:320];
                12'h044: read_mux_data = joint_words[383:352];
                12'h048: read_mux_data = joint_words[415:384];
                12'h04c: read_mux_data = joint_words[447:416];
                12'h050: read_mux_data = joint_words[479:448];
                12'h054: read_mux_data = joint_words[511:480];
                12'h058: read_mux_data = joint_words[543:512];
                12'h05c: read_mux_data = {15'd0, joint_flags};
                12'h060: read_mux_data = {8'd0, red_thresh_cfg};
                12'h064: read_mux_data = {8'd0, blue_thresh_cfg};
                12'h068: read_mux_data = red_word;
                12'h06c: read_mux_data = blue_word;
                12'h070: read_mux_data = frame_id;
                12'h074: read_mux_data = result_seq;
                12'h078: read_mux_data = error_code;
                12'h07c: read_mux_data = {31'd0, irq_enable};
                12'h080: read_mux_data = cycle_count;
                12'h084: read_mux_data = result_frame_id;
                12'h088: read_mux_data = {14'd0, min_count_cfg};
                12'h08c: read_mux_data = wgt_base;
                12'h090: read_mux_data = fm_a_base;
                12'h094: read_mux_data = fm_b_base;
                12'h098: read_mux_data = sg_desc_base;
                12'h09c: read_mux_data = frame_base;
                12'h0a0: read_mux_data = timeout_cycles;
                12'h0a4: read_mux_data = 32'h00040003;
                12'h0a8: read_mux_data = 32'h0000000d;
                12'h0ac: read_mux_data = 32'hc9854bb2;
                12'h0b0: read_mux_data = {22'd0,debug_fault_sources_latched};
                12'h0b4: read_mux_data = {8'hd1,6'd0,error_pending,busy,
                                           debug_txn_state,debug_step,
                                           debug_stage,debug_state};
                12'h0b8: read_mux_data = debug_txn_addr;
                12'h0bc: read_mux_data = debug_txn_data;
                12'h0c0: read_mux_data = {22'd0,debug_fault_sources};
                12'h0c4: read_mux_data = {22'd0,fm_debug_fault_reason};
                12'h0c8: read_mux_data = {11'd0,fm_debug_read_input_bytes};
                12'h0cc: read_mux_data = {11'd0,fm_debug_read_parsed_bytes};
                12'h0d0: read_mux_data = {11'd0,fm_debug_write_input_bytes};
                12'h0d4: read_mux_data = {11'd0,fm_debug_write_packed_bytes};
                12'h0d8: read_mux_data = {11'd0,fm_debug_write_output_bytes};
                12'h0dc: read_mux_data = fm_debug_stream_status;
                12'h0e0: read_mux_data = fm_debug_last_body_tag[31:0];
                12'h0e4: read_mux_data = fm_debug_last_body_tag[63:32];
                12'h0e8: read_mux_data = fm_debug_protocol_misc;
                12'h0ec: read_mux_data = {12'd0,fm_debug_expected_src_bytes};
                12'h0f0: read_mux_data = {12'd0,fm_debug_expected_dst_bytes};
                12'h0f4: read_mux_data = {8'd0,green_thresh_cfg};
                12'h0f8: read_mux_data = green_word;
                12'h0fc: read_mux_data = {29'd0,color_enable_cfg};
                12'h100: read_mux_data = {8'd0,color_margin_cfg};
                default: begin
                    read_mux_data = 32'd0;
                    read_mux_resp = 2'b10;
                end
            endcase
        end
    end

    reg [31:0] merged_value;
    always @(posedge clk) begin
        if (!rst_n || soft_reset_pulse) begin
            aw_hold <= 1'b0;
            awaddr_hold <= 12'd0;
            w_hold <= 1'b0;
            wdata_hold <= 32'd0;
            wstrb_hold <= 4'd0;
            bvalid_reg <= 1'b0;
            bresp_reg <= 2'b00;
            rvalid_reg <= 1'b0;
            rdata_reg <= 32'd0;
            rresp_reg <= 2'b00;
            start <= 1'b0;
            clear_done <= 1'b0;
            clear_error <= 1'b0;
            soft_reset_pulse <= 1'b0;
            threshold_cfg <= 8'hd2;
            red_thresh_cfg <= 24'h6464a0;
            blue_thresh_cfg <= 24'ha06464;
            green_thresh_cfg <= 24'h406040;
            color_margin_cfg <= 24'h402010;
            color_enable_cfg <= 3'b111;
            min_count_cfg <= 18'd8;
            frame_id <= 32'd0;
            sg_desc_base <= 32'h11200000;
            frame_base <= 32'h0a000000;
            wgt_base <= 32'h10000000;
            fm_a_base <= 32'h11000000;
            fm_b_base <= 32'h11100000;
            timeout_cycles <= 32'h05f5e100;
            irq_enable <= 1'b0;
        end else begin
            start <= 1'b0;
            clear_done <= 1'b0;
            clear_error <= 1'b0;
            soft_reset_pulse <= 1'b0;

            if (s_axil_awvalid && s_axil_awready) begin
                aw_hold <= 1'b1;
                awaddr_hold <= s_axil_awaddr;
            end
            if (s_axil_wvalid && s_axil_wready) begin
                w_hold <= 1'b1;
                wdata_hold <= s_axil_wdata;
                wstrb_hold <= s_axil_wstrb;
            end
            if (bvalid_reg && s_axil_bready)
                bvalid_reg <= 1'b0;

            if (aw_hold && w_hold && !bvalid_reg) begin
                aw_hold <= 1'b0;
                w_hold <= 1'b0;
                bvalid_reg <= 1'b1;
                bresp_reg <= 2'b00;
                if (awaddr_hold[1:0] != 2'b00) begin
                    bresp_reg <= 2'b10;
                end else begin
                    case (awaddr_hold)
                        12'h000: begin
                            if ((wdata_hold[2:0] == 3'b001) && wstrb_hold[0] &&
                                !busy && !error_pending && !done_pending)
                                start <= 1'b1;
                            else if ((wdata_hold[2:0] == 3'b010) && wstrb_hold[0])
                                clear_done <= 1'b1;
                            else if ((wdata_hold[2:0] == 3'b100) && wstrb_hold[0])
                                soft_reset_pulse <= 1'b1;
                            else if (wdata_hold[2:0] != 3'b000)
                                bresp_reg <= 2'b10;
                        end
                        12'h008: begin
                            merged_value = merge_wstrb({24'd0, threshold_cfg},
                                                       wdata_hold, wstrb_hold);
                            if (busy || error_pending || merged_value[31:8] != 0)
                                bresp_reg <= 2'b10;
                            else threshold_cfg <= merged_value[7:0];
                        end
                        12'h060: begin
                            merged_value = merge_wstrb({8'd0, red_thresh_cfg},
                                                       wdata_hold, wstrb_hold);
                            if (busy || error_pending || merged_value[31:24] != 0)
                                bresp_reg <= 2'b10;
                            else red_thresh_cfg <= merged_value[23:0];
                        end
                        12'h064: begin
                            merged_value = merge_wstrb({8'd0, blue_thresh_cfg},
                                                       wdata_hold, wstrb_hold);
                            if (busy || error_pending || merged_value[31:24] != 0)
                                bresp_reg <= 2'b10;
                            else blue_thresh_cfg <= merged_value[23:0];
                        end
                        12'h0f4: begin
                            merged_value = merge_wstrb({8'd0, green_thresh_cfg},
                                                       wdata_hold, wstrb_hold);
                            if (busy || error_pending || merged_value[31:24] != 0)
                                bresp_reg <= 2'b10;
                            else green_thresh_cfg <= merged_value[23:0];
                        end
                        12'h0fc: begin
                            merged_value = merge_wstrb({29'd0, color_enable_cfg},
                                                       wdata_hold, wstrb_hold);
                            if (busy || error_pending || merged_value[31:3] != 0)
                                bresp_reg <= 2'b10;
                            else color_enable_cfg <= merged_value[2:0];
                        end
                        12'h100: begin
                            merged_value = merge_wstrb({8'd0, color_margin_cfg},
                                                       wdata_hold, wstrb_hold);
                            if (busy || error_pending || merged_value[31:24] != 0)
                                bresp_reg <= 2'b10;
                            else color_margin_cfg <= merged_value[23:0];
                        end
                        12'h070: begin
                            if (busy || error_pending) bresp_reg <= 2'b10;
                            else frame_id <= merge_wstrb(frame_id, wdata_hold, wstrb_hold);
                        end
                        12'h078: begin
                            if (|(wdata_hold & {4{8'hff}} &
                                  {{8{wstrb_hold[3]}},{8{wstrb_hold[2]}},
                                   {8{wstrb_hold[1]}},{8{wstrb_hold[0]}}}))
                                clear_error <= 1'b1;
                        end
                        12'h07c: begin
                            merged_value = merge_wstrb({31'd0, irq_enable},
                                                       wdata_hold, wstrb_hold);
                            if (merged_value[31:1] != 0) bresp_reg <= 2'b10;
                            else irq_enable <= merged_value[0];
                        end
                        12'h088: begin
                            merged_value = merge_wstrb({14'd0, min_count_cfg},
                                                       wdata_hold, wstrb_hold);
                            if (busy || error_pending || merged_value[31:18] != 0 ||
                                merged_value[17:0] == 0 || merged_value[17:0] > 18'd184320)
                                bresp_reg <= 2'b10;
                            else min_count_cfg <= merged_value[17:0];
                        end
                        12'h08c, 12'h090, 12'h094, 12'h098, 12'h09c: begin
                            merged_value = 32'd0;
                            case (awaddr_hold)
                                12'h08c: merged_value = merge_wstrb(wgt_base, wdata_hold, wstrb_hold);
                                12'h090: merged_value = merge_wstrb(fm_a_base, wdata_hold, wstrb_hold);
                                12'h094: merged_value = merge_wstrb(fm_b_base, wdata_hold, wstrb_hold);
                                12'h098: merged_value = merge_wstrb(sg_desc_base, wdata_hold, wstrb_hold);
                                default: merged_value = merge_wstrb(frame_base, wdata_hold, wstrb_hold);
                            endcase
                            if (busy || error_pending || merged_value[5:0] != 0)
                                bresp_reg <= 2'b10;
                            else begin
                                if (awaddr_hold == 12'h08c) wgt_base <= merged_value;
                                else if (awaddr_hold == 12'h090) fm_a_base <= merged_value;
                                else if (awaddr_hold == 12'h094) fm_b_base <= merged_value;
                                else if (awaddr_hold == 12'h098) sg_desc_base <= merged_value;
                                else frame_base <= merged_value;
                            end
                        end
                        12'h0a0: begin
                            merged_value = merge_wstrb(timeout_cycles, wdata_hold, wstrb_hold);
                            if (busy || error_pending || merged_value == 0)
                                bresp_reg <= 2'b10;
                            else timeout_cycles <= merged_value;
                        end
                        default: bresp_reg <= 2'b10;
                    endcase
                end
            end

            if (s_axil_arvalid && s_axil_arready) begin
                rvalid_reg <= 1'b1;
                rdata_reg <= read_mux_data;
                rresp_reg <= read_mux_resp;
            end else if (rvalid_reg && s_axil_rready) begin
                rvalid_reg <= 1'b0;
            end
        end
    end

    top_level_fsm u_top_level_fsm (
        .clk(clk),
        .rst_n(core_rst_n),
        .input_cfg_valid(input_cfg_valid),
        .input_cfg_ready(input_cfg_ready),
        .input_cfg_desc(input_cfg_desc),
        .input_done(input_done),
        .input_fault(input_fault),
        .line_cfg_valid(line_cfg_valid),
        .line_cfg_ready(line_cfg_ready),
        .line_cfg_desc(line_cfg_desc),
        .line_done(line_done),
        .line_fault(line_fault),
        .dw_cfg_valid(dw_cfg_valid),
        .dw_cfg_ready(dw_cfg_ready),
        .dw_cfg_desc(dw_cfg_desc),
        .dw_done(dw_done),
        .dw_fault(dw_fault),
        .pw_cfg_valid(pw_cfg_valid),
        .pw_cfg_ready(pw_cfg_ready),
        .pw_cfg_desc(pw_cfg_desc),
        .pw_done(pw_done),
        .pw_fault(pw_fault),
        .fm_cfg_valid(fm_cfg_valid),
        .fm_cfg_ready(fm_cfg_ready),
        .fm_cfg_desc(fm_cfg_desc),
        .fm_done(fm_done),
        .fm_fault(fm_fault),
        .fm_src_addr(fm_src_addr),
        .fm_dst_addr(fm_dst_addr),
        .fm_src_bytes(fm_src_bytes),
        .fm_dst_bytes(fm_dst_bytes),
        .fm_read_en(fm_read_en),
        .fm_write_en(fm_write_en),
        .fm_read_dma_done(fm_read_dma_done),
        .fm_write_dma_done(fm_write_dma_done),
        .fm_dma_error(fm_dma_error),
        .fm_read_done(fm_read_done),
        .fm_write_done(fm_write_done),
        .down_cfg_valid(down_cfg_valid),
        .down_cfg_ready(down_cfg_ready),
        .down_cfg_desc(down_cfg_desc),
        .down_done(down_done),
        .down_fault(down_fault),
        .arg_cfg_valid(arg_cfg_valid),
        .arg_cfg_ready(arg_cfg_ready),
        .arg_cfg_desc(arg_cfg_desc),
        .arg_done(arg_done),
        .arg_fault(arg_fault),
        .coord_cfg_valid(coord_cfg_valid),
        .coord_cfg_ready(coord_cfg_ready),
        .coord_cfg_desc(coord_cfg_desc),
        .coord_done(coord_done),
        .coord_fault(coord_fault),
        .coord_m_joint_data(coord_m_joint_data),
        .coord_m_joint_valid(coord_m_joint_valid),
        .coord_m_joint_ready(coord_m_joint_ready),
        .coord_m_joint_last(coord_m_joint_last),
        .coord_m_joint_index(coord_m_joint_index),
        .coord_m_joint_good(coord_m_joint_good),
        .coord_threshold(coord_threshold),
        .swap_fault(swap_fault),
        .swap_load_valid(swap_load_valid),
        .swap_load_ready(swap_load_ready),
        .swap_load_desc(swap_load_desc),
        .swap_load_done(swap_load_done),
        .color_fault(color_fault),
        .color_frame_start(color_frame_start),
        .color_red_cfg(color_red_cfg),
        .color_blue_cfg(color_blue_cfg),
        .color_green_cfg(color_green_cfg),
        .color_margin_cfg(color_margin_cfg_active),
        .color_enable(color_enable_active),
        .color_min_count(color_min_count),
        .color_red_word(color_red_word),
        .color_blue_word(color_blue_word),
        .color_green_word(color_green_word),
        .color_results_valid(color_results_valid),
        .rom_fault(rom_fault),
        .rom_req_valid(rom_req_valid),
        .rom_req_ready(rom_req_ready),
        .rom_req_op(rom_req_op),
        .rom_rsp_valid(rom_rsp_valid),
        .rom_rsp_ready(rom_rsp_ready),
        .rom_rsp_desc(rom_rsp_desc),
        .threshold_cfg(threshold_cfg),
        .red_thresh_cfg(red_thresh_cfg),
        .blue_thresh_cfg(blue_thresh_cfg),
        .green_thresh_cfg(green_thresh_cfg),
        .color_margin_cfg_in(color_margin_cfg),
        .color_enable_cfg(color_enable_cfg),
        .min_count_cfg(min_count_cfg),
        .datapath_progress(datapath_progress),
        .start(start),
        .clear_done(clear_done),
        .clear_error(clear_error),
        .frame_id(frame_id),
        .sg_desc_base(sg_desc_base),
        .wgt_base(wgt_base),
        .fm_a_base(fm_a_base),
        .fm_b_base(fm_b_base),
        .timeout_cycles(timeout_cycles),
        .busy(busy),
        .done_pending(done_pending),
        .error_pending(error_pending),
        .error_code(error_code),
        .result_seq(result_seq),
        .result_frame_id(result_frame_id),
        .joint_words(joint_words),
        .joint_flags(joint_flags),
        .red_word(red_word),
        .blue_word(blue_word),
        .green_word(green_word),
        .cycle_count(cycle_count),
        .image_read_done(image_read_done),
        .debug_fault_sources(debug_fault_sources),
        .debug_state(debug_state),
        .debug_stage(debug_stage),
        .debug_step(debug_step),
        .debug_txn_state(debug_txn_state),
        .debug_txn_addr(debug_txn_addr),
        .debug_txn_data(debug_txn_data),
        .m_axil_awaddr(m_axil_awaddr),
        .m_axil_awvalid(m_axil_awvalid),
        .m_axil_awready(m_axil_awready),
        .m_axil_awprot(m_axil_awprot),
        .m_axil_wdata(m_axil_wdata),
        .m_axil_wstrb(m_axil_wstrb),
        .m_axil_wvalid(m_axil_wvalid),
        .m_axil_wready(m_axil_wready),
        .m_axil_bresp(m_axil_bresp),
        .m_axil_bvalid(m_axil_bvalid),
        .m_axil_bready(m_axil_bready),
        .m_axil_araddr(m_axil_araddr),
        .m_axil_arvalid(m_axil_arvalid),
        .m_axil_arready(m_axil_arready),
        .m_axil_arprot(m_axil_arprot),
        .m_axil_rdata(m_axil_rdata),
        .m_axil_rresp(m_axil_rresp),
        .m_axil_rvalid(m_axil_rvalid),
        .m_axil_rready(m_axil_rready)
    );
    layer_param_rom u_layer_param_rom (
        .clk(clk),
        .rst_n(core_rst_n),
        .fault(rom_fault),
        .req_valid(rom_req_valid),
        .req_ready(rom_req_ready),
        .req_op(rom_req_op),
        .rsp_valid(rom_rsp_valid),
        .rsp_ready(rom_rsp_ready),
        .rsp_desc(rom_rsp_desc)
    );
    weight_bram_swap_fsm u_weight_bram_swap_fsm (
        .clk(clk),
        .rst_n(core_rst_n),
        .fault(swap_fault),
        .load_valid(swap_load_valid),
        .load_ready(swap_load_ready),
        .load_desc(swap_load_desc),
        .load_done(swap_load_done),
        .s_dma_data(s_weight_data),
        .s_dma_valid(s_weight_valid),
        .s_dma_ready(s_weight_ready),
        .s_dma_last(s_weight_last),
        .s_dma_keep(s_weight_keep),
        .conv0_req_valid(conv0_req_valid),
        .conv0_req_ready(conv0_req_ready),
        .conv0_req_addr(conv0_req_addr),
        .conv0_rsp_valid(conv0_rsp_valid),
        .conv0_rsp_ready(conv0_rsp_ready),
        .conv0_rsp_data(conv0_rsp_data),
        .conv0_rsp_params(conv0_rsp_params),
        .dw_w_req_valid(dw_w_req_valid),
        .dw_w_req_ready(dw_w_req_ready),
        .dw_w_req_addr(dw_w_req_addr),
        .dw_w_rsp_valid(dw_w_rsp_valid),
        .dw_w_rsp_ready(dw_w_rsp_ready),
        .dw_w_rsp_data(dw_w_rsp_data),
        .dw_p_req_valid(dw_p_req_valid),
        .dw_p_req_ready(dw_p_req_ready),
        .dw_p_req_addr(dw_p_req_addr),
        .dw_p_rsp_valid(dw_p_rsp_valid),
        .dw_p_rsp_ready(dw_p_rsp_ready),
        .dw_p_rsp_data(dw_p_rsp_data),
        .pw_req_valid(pw_req_valid),
        .pw_req_ready(pw_req_ready),
        .pw_req_addr(pw_req_addr),
        .pw_rsp_valid(pw_rsp_valid),
        .pw_rsp_ready(pw_rsp_ready),
        .pw_rsp_data(pw_rsp_data),
        .pw_rsp_params(pw_rsp_params),
        .pw_req_group(pw_req_group)
    );
    feature_map_io u_feature_map_io (
        .clk(clk),
        .rst_n(core_rst_n),
        .cfg_valid(fm_cfg_valid),
        .cfg_ready(fm_cfg_ready),
        .cfg_desc(fm_cfg_desc),
        .done(fm_done),
        .fault(fm_fault),
        .s_body_data(fm_body_data),
        .s_body_valid(fm_body_valid),
        .s_body_ready(fm_body_ready),
        .s_body_mask(fm_body_mask),
        .s_body_tag(fm_body_tag),
        .m_pixel_data(fm_pixel_data),
        .m_pixel_valid(fm_pixel_valid),
        .m_pixel_ready(fm_pixel_ready),
        .m_pixel_mask(fm_pixel_mask),
        .m_pixel_tag(fm_pixel_tag),
        .src_addr(fm_src_addr),
        .dst_addr(fm_dst_addr),
        .src_bytes(fm_src_bytes),
        .dst_bytes(fm_dst_bytes),
        .read_en(fm_read_en),
        .write_en(fm_write_en),
        .read_dma_done(fm_read_dma_done),
        .write_dma_done(fm_write_dma_done),
        .dma_error(fm_dma_error),
        .read_done(fm_read_done),
        .write_done(fm_write_done),
        .s_dma_read_data(s_feature_data),
        .s_dma_read_valid(s_feature_valid),
        .s_dma_read_ready(s_feature_ready),
        .s_dma_read_last(s_feature_last),
        .s_dma_read_keep(s_feature_keep),
        .m_dma_write_data(m_feature_data),
        .m_dma_write_valid(m_feature_valid),
        .m_dma_write_ready(m_feature_ready),
        .m_dma_write_last(m_feature_last),
        .m_dma_write_keep(m_feature_keep),
        .debug_fault_reason(fm_debug_fault_reason),
        .debug_read_input_bytes(fm_debug_read_input_bytes),
        .debug_read_parsed_bytes(fm_debug_read_parsed_bytes),
        .debug_write_input_bytes(fm_debug_write_input_bytes),
        .debug_write_packed_bytes(fm_debug_write_packed_bytes),
        .debug_write_output_bytes(fm_debug_write_output_bytes),
        .debug_stream_status(fm_debug_stream_status),
        .debug_last_body_tag(fm_debug_last_body_tag),
        .debug_protocol_misc(fm_debug_protocol_misc),
        .debug_expected_src_bytes(fm_debug_expected_src_bytes),
        .debug_expected_dst_bytes(fm_debug_expected_dst_bytes)
    );
    line_buffer u_line_buffer (
        .clk(clk),
        .rst_n(core_rst_n),
        .cfg_valid(line_cfg_valid),
        .cfg_ready(line_cfg_ready),
        .cfg_desc(line_cfg_desc),
        .done(line_done),
        .fault(line_fault),
        .s_pixel_data(fm_pixel_data),
        .s_pixel_valid(line_pixel_valid),
        .s_pixel_ready(line_pixel_ready),
        .s_pixel_mask(fm_pixel_mask),
        .s_pixel_tag(fm_pixel_tag),
        .m_tap_data(line_tap_data),
        .m_tap_valid(line_tap_valid),
        .m_tap_ready(line_tap_ready),
        .m_tap_mask(line_tap_mask),
        .m_tap_tag(line_tap_tag)
    );
    input_conv_pe u_input_conv_pe (
        .clk(clk),
        .rst_n(core_rst_n),
        .cfg_valid(input_cfg_valid),
        .cfg_ready(input_cfg_ready),
        .cfg_desc(input_cfg_desc),
        .done(input_done),
        .fault(input_fault),
        .s_rgb_data(down_rgb_data),
        .s_rgb_valid(down_rgb_valid),
        .s_rgb_ready(down_rgb_ready),
        .s_rgb_tag(down_rgb_tag),
        .m_body_data(input_body_data),
        .m_body_valid(input_body_valid),
        .m_body_ready(input_body_ready),
        .m_body_mask(input_body_mask),
        .m_body_tag(input_body_tag),
        .conv0_req_valid(conv0_req_valid),
        .conv0_req_ready(conv0_req_ready),
        .conv0_req_addr(conv0_req_addr),
        .conv0_rsp_valid(conv0_rsp_valid),
        .conv0_rsp_ready(conv0_rsp_ready),
        .conv0_rsp_data(conv0_rsp_data),
        .conv0_rsp_params(conv0_rsp_params)
    );
    depthwise_conv_pe u_depthwise_conv_pe (
        .clk(clk),
        .rst_n(core_rst_n),
        .cfg_valid(dw_cfg_valid),
        .cfg_ready(dw_cfg_ready),
        .cfg_desc(dw_cfg_desc),
        .done(dw_done),
        .fault(dw_fault),
        .s_tap_data(line_tap_data),
        .s_tap_valid(line_tap_valid),
        .s_tap_ready(line_tap_ready),
        .s_tap_mask(line_tap_mask),
        .s_tap_tag(line_tap_tag),
        .m_pixel_data(dw_pixel_data),
        .m_pixel_valid(dw_pixel_valid),
        .m_pixel_ready(dw_pixel_ready),
        .m_pixel_mask(dw_pixel_mask),
        .m_pixel_tag(dw_pixel_tag),
        .dw_w_req_valid(dw_w_req_valid),
        .dw_w_req_ready(dw_w_req_ready),
        .dw_w_req_addr(dw_w_req_addr),
        .dw_w_rsp_valid(dw_w_rsp_valid),
        .dw_w_rsp_ready(dw_w_rsp_ready),
        .dw_w_rsp_data(dw_w_rsp_data),
        .dw_p_req_valid(dw_p_req_valid),
        .dw_p_req_ready(dw_p_req_ready),
        .dw_p_req_addr(dw_p_req_addr),
        .dw_p_rsp_valid(dw_p_rsp_valid),
        .dw_p_rsp_ready(dw_p_rsp_ready),
        .dw_p_rsp_data(dw_p_rsp_data)
    );
    pointwise_conv_pe u_pointwise_conv_pe (
        .clk(clk),
        .rst_n(core_rst_n),
        .cfg_valid(pw_cfg_valid),
        .cfg_ready(pw_cfg_ready),
        .cfg_desc(pw_cfg_desc),
        .done(pw_done),
        .fault(pw_fault),
        .s_pixel_data(pw_pixel_data),
        .s_pixel_valid(pw_pixel_valid),
        .s_pixel_ready(pw_pixel_ready),
        .s_pixel_mask(pw_pixel_mask),
        .s_pixel_tag(pw_pixel_tag),
        .m_value_data(pw_value_data),
        .m_value_valid(pw_value_valid),
        .m_value_ready(pw_value_ready),
        .m_value_mask(pw_value_mask),
        .m_value_tag(pw_value_tag),
        .pw_req_valid(pw_req_valid),
        .pw_req_ready(pw_req_ready),
        .pw_req_addr(pw_req_addr),
        .pw_rsp_valid(pw_rsp_valid),
        .pw_rsp_ready(pw_rsp_ready),
        .pw_rsp_data(pw_rsp_data),
        .pw_rsp_params(pw_rsp_params),
        .pw_req_group(pw_req_group)
    );
    downsample_module u_downsample_module (
        .clk(clk),
        .rst_n(core_rst_n),
        .cfg_valid(down_cfg_valid),
        .cfg_ready(down_cfg_ready),
        .cfg_desc(down_cfg_desc),
        .done(down_done),
        .fault(down_fault),
        .s_dma_data(s_image_data),
        .s_dma_valid(s_image_valid),
        .s_dma_ready(s_image_ready),
        .s_dma_last(s_image_last),
        .s_dma_keep(s_image_keep),
        .m_rgb_data(down_rgb_data),
        .m_rgb_valid(down_rgb_valid),
        .m_rgb_ready(down_rgb_ready),
        .m_rgb_tag(down_rgb_tag),
        .tap_data(down_tap_data),
        .tap_row(down_tap_row),
        .tap_col(down_tap_col),
        .tap_accept(down_tap_accept),
        .tap_last(down_tap_last)
    );
    argmax_offset_select u_argmax_offset_select (
        .clk(clk),
        .rst_n(core_rst_n),
        .cfg_valid(arg_cfg_valid),
        .cfg_ready(arg_cfg_ready),
        .cfg_desc(arg_cfg_desc),
        .done(arg_done),
        .fault(arg_fault),
        .s_head_data(pw_value_data),
        .s_head_valid(pw_head_select ? pw_value_valid : 1'b0),
        .s_head_ready(arg_head_ready),
        .s_head_mask(pw_value_mask),
        .s_head_tag(pw_value_tag),
        .m_result_data(arg_result_data),
        .m_result_valid(arg_result_valid),
        .m_result_ready(arg_result_ready),
        .m_result_last(arg_result_last),
        .m_result_joint(arg_result_joint)
    );
    coord_restore u_coord_restore (
        .clk(clk),
        .rst_n(core_rst_n),
        .cfg_valid(coord_cfg_valid),
        .cfg_ready(coord_cfg_ready),
        .cfg_desc(coord_cfg_desc),
        .done(coord_done),
        .fault(coord_fault),
        .s_result_data(arg_result_data),
        .s_result_valid(arg_result_valid),
        .s_result_ready(arg_result_ready),
        .s_result_last(arg_result_last),
        .s_result_joint(arg_result_joint),
        .m_joint_data(coord_m_joint_data),
        .m_joint_valid(coord_m_joint_valid),
        .m_joint_ready(coord_m_joint_ready),
        .m_joint_last(coord_m_joint_last),
        .m_joint_index(coord_m_joint_index),
        .m_joint_good(coord_m_joint_good),
        .threshold(coord_threshold)
    );
    color_marker_detect u_color_marker_detect (
        .clk(clk),
        .rst_n(core_rst_n),
        .fault(color_fault),
        .tap_data(color_tap_bgr),
        .tap_row(down_tap_row),
        .tap_col(down_tap_col),
        .tap_accept(down_tap_accept),
        .tap_last(down_tap_last),
        .frame_start(color_frame_start),
        .red_cfg(color_red_cfg),
        .blue_cfg(color_blue_cfg),
        .green_cfg(color_green_cfg),
        .margin_cfg(color_margin_cfg_active),
        .color_enable(color_enable_active),
        .min_count(color_min_count),
        .red_word(color_red_word),
        .blue_word(color_blue_word),
        .green_word(color_green_word),
        .results_valid(color_results_valid)
    );

endmodule
