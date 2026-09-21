`timescale 1ns/1ps
`include "cnn_common_params.vh"

module pointwise_u8s8_dsp_mult (
    input wire clk,
    input wire ce,
    input wire [7:0] activation,
    input wire [7:0] weight_bits,
    output wire signed [15:0] product
);
    /* UINT8 activation is zero-extended before the signed multiply so that
       8'h80..8'hff remain positive.  The S8 product range fits in 16 bits. */
    (* use_dsp = "yes" *) reg signed [15:0] product_reg;

    always @(posedge clk) begin
        if (ce)
            product_reg <= $signed({1'b0, activation}) * $signed(weight_bits);
    end

    assign product = product_reg;
endmodule

module pointwise_s16_pair_add_lut (
    input wire clk,
    input wire ce,
    input wire signed [15:0] lhs,
    input wire signed [15:0] rhs,
    output wire signed [16:0] sum
);
    (* use_dsp = "no" *) reg signed [16:0] sum_reg;

    always @(posedge clk) begin
        if (ce)
            sum_reg <= $signed({lhs[15], lhs}) +
                       $signed({rhs[15], rhs});
    end

    assign sum = sum_reg;
endmodule

module pointwise_rshift_even_43_lut (
    input wire clk,
    input wire rst_n,
    input wire ce,
    input wire signed [42:0] value,
    input wire [5:0] shift_count,
    output wire signed [42:0] rounded
);
    (* use_dsp = "no" *) reg signed [42:0] rounded_reg;

    function signed [42:0] rshift_even_43;
        input signed [42:0] function_value;
        input [5:0] function_shift_count;
        reg signed [42:0] quotient;
        begin
            /* For arithmetic q=floor(value/2^N), the non-negative
               remainder is exactly value[N-1:0].  Guard/sticky/tie-even
               therefore avoids a second barrel shift and 43-bit subtract. */
            quotient = function_value;
            case (function_shift_count)
                6'd0: rshift_even_43 = function_value;
                6'd1: begin
                    quotient = function_value >>> 1;
                    if (function_value[0] &&
                        (1'b0 || quotient[0]))
                        rshift_even_43 = quotient + 43'sd1;
                    else
                        rshift_even_43 = quotient;
                end
                6'd2: begin
                    quotient = function_value >>> 2;
                    if (function_value[1] &&
                        ((|function_value[0:0]) || quotient[0]))
                        rshift_even_43 = quotient + 43'sd1;
                    else
                        rshift_even_43 = quotient;
                end
                6'd3: begin
                    quotient = function_value >>> 3;
                    if (function_value[2] &&
                        ((|function_value[1:0]) || quotient[0]))
                        rshift_even_43 = quotient + 43'sd1;
                    else
                        rshift_even_43 = quotient;
                end
                6'd4: begin
                    quotient = function_value >>> 4;
                    if (function_value[3] &&
                        ((|function_value[2:0]) || quotient[0]))
                        rshift_even_43 = quotient + 43'sd1;
                    else
                        rshift_even_43 = quotient;
                end
                6'd5: begin
                    quotient = function_value >>> 5;
                    if (function_value[4] &&
                        ((|function_value[3:0]) || quotient[0]))
                        rshift_even_43 = quotient + 43'sd1;
                    else
                        rshift_even_43 = quotient;
                end
                6'd6: begin
                    quotient = function_value >>> 6;
                    if (function_value[5] &&
                        ((|function_value[4:0]) || quotient[0]))
                        rshift_even_43 = quotient + 43'sd1;
                    else
                        rshift_even_43 = quotient;
                end
                6'd7: begin
                    quotient = function_value >>> 7;
                    if (function_value[6] &&
                        ((|function_value[5:0]) || quotient[0]))
                        rshift_even_43 = quotient + 43'sd1;
                    else
                        rshift_even_43 = quotient;
                end
                6'd8: begin
                    quotient = function_value >>> 8;
                    if (function_value[7] &&
                        ((|function_value[6:0]) || quotient[0]))
                        rshift_even_43 = quotient + 43'sd1;
                    else
                        rshift_even_43 = quotient;
                end
                6'd9: begin
                    quotient = function_value >>> 9;
                    if (function_value[8] &&
                        ((|function_value[7:0]) || quotient[0]))
                        rshift_even_43 = quotient + 43'sd1;
                    else
                        rshift_even_43 = quotient;
                end
                6'd10: begin
                    quotient = function_value >>> 10;
                    if (function_value[9] &&
                        ((|function_value[8:0]) || quotient[0]))
                        rshift_even_43 = quotient + 43'sd1;
                    else
                        rshift_even_43 = quotient;
                end
                6'd11: begin
                    quotient = function_value >>> 11;
                    if (function_value[10] &&
                        ((|function_value[9:0]) || quotient[0]))
                        rshift_even_43 = quotient + 43'sd1;
                    else
                        rshift_even_43 = quotient;
                end
                6'd12: begin
                    quotient = function_value >>> 12;
                    if (function_value[11] &&
                        ((|function_value[10:0]) || quotient[0]))
                        rshift_even_43 = quotient + 43'sd1;
                    else
                        rshift_even_43 = quotient;
                end
                6'd13: begin
                    quotient = function_value >>> 13;
                    if (function_value[12] &&
                        ((|function_value[11:0]) || quotient[0]))
                        rshift_even_43 = quotient + 43'sd1;
                    else
                        rshift_even_43 = quotient;
                end
                6'd14: begin
                    quotient = function_value >>> 14;
                    if (function_value[13] &&
                        ((|function_value[12:0]) || quotient[0]))
                        rshift_even_43 = quotient + 43'sd1;
                    else
                        rshift_even_43 = quotient;
                end
                6'd15: begin
                    quotient = function_value >>> 15;
                    if (function_value[14] &&
                        ((|function_value[13:0]) || quotient[0]))
                        rshift_even_43 = quotient + 43'sd1;
                    else
                        rshift_even_43 = quotient;
                end
                6'd16: begin
                    quotient = function_value >>> 16;
                    if (function_value[15] &&
                        ((|function_value[14:0]) || quotient[0]))
                        rshift_even_43 = quotient + 43'sd1;
                    else
                        rshift_even_43 = quotient;
                end
                6'd17: begin
                    quotient = function_value >>> 17;
                    if (function_value[16] &&
                        ((|function_value[15:0]) || quotient[0]))
                        rshift_even_43 = quotient + 43'sd1;
                    else
                        rshift_even_43 = quotient;
                end
                6'd18: begin
                    quotient = function_value >>> 18;
                    if (function_value[17] &&
                        ((|function_value[16:0]) || quotient[0]))
                        rshift_even_43 = quotient + 43'sd1;
                    else
                        rshift_even_43 = quotient;
                end
                6'd19: begin
                    quotient = function_value >>> 19;
                    if (function_value[18] &&
                        ((|function_value[17:0]) || quotient[0]))
                        rshift_even_43 = quotient + 43'sd1;
                    else
                        rshift_even_43 = quotient;
                end
                6'd20: begin
                    quotient = function_value >>> 20;
                    if (function_value[19] &&
                        ((|function_value[18:0]) || quotient[0]))
                        rshift_even_43 = quotient + 43'sd1;
                    else
                        rshift_even_43 = quotient;
                end
                6'd21: begin
                    quotient = function_value >>> 21;
                    if (function_value[20] &&
                        ((|function_value[19:0]) || quotient[0]))
                        rshift_even_43 = quotient + 43'sd1;
                    else
                        rshift_even_43 = quotient;
                end
                6'd22: begin
                    quotient = function_value >>> 22;
                    if (function_value[21] &&
                        ((|function_value[20:0]) || quotient[0]))
                        rshift_even_43 = quotient + 43'sd1;
                    else
                        rshift_even_43 = quotient;
                end
                6'd23: begin
                    quotient = function_value >>> 23;
                    if (function_value[22] &&
                        ((|function_value[21:0]) || quotient[0]))
                        rshift_even_43 = quotient + 43'sd1;
                    else
                        rshift_even_43 = quotient;
                end
                6'd24: begin
                    quotient = function_value >>> 24;
                    if (function_value[23] &&
                        ((|function_value[22:0]) || quotient[0]))
                        rshift_even_43 = quotient + 43'sd1;
                    else
                        rshift_even_43 = quotient;
                end
                6'd25: begin
                    quotient = function_value >>> 25;
                    if (function_value[24] &&
                        ((|function_value[23:0]) || quotient[0]))
                        rshift_even_43 = quotient + 43'sd1;
                    else
                        rshift_even_43 = quotient;
                end
                6'd26: begin
                    quotient = function_value >>> 26;
                    if (function_value[25] &&
                        ((|function_value[24:0]) || quotient[0]))
                        rshift_even_43 = quotient + 43'sd1;
                    else
                        rshift_even_43 = quotient;
                end
                6'd27: begin
                    quotient = function_value >>> 27;
                    if (function_value[26] &&
                        ((|function_value[25:0]) || quotient[0]))
                        rshift_even_43 = quotient + 43'sd1;
                    else
                        rshift_even_43 = quotient;
                end
                6'd28: begin
                    quotient = function_value >>> 28;
                    if (function_value[27] &&
                        ((|function_value[26:0]) || quotient[0]))
                        rshift_even_43 = quotient + 43'sd1;
                    else
                        rshift_even_43 = quotient;
                end
                6'd29: begin
                    quotient = function_value >>> 29;
                    if (function_value[28] &&
                        ((|function_value[27:0]) || quotient[0]))
                        rshift_even_43 = quotient + 43'sd1;
                    else
                        rshift_even_43 = quotient;
                end
                6'd30: begin
                    quotient = function_value >>> 30;
                    if (function_value[29] &&
                        ((|function_value[28:0]) || quotient[0]))
                        rshift_even_43 = quotient + 43'sd1;
                    else
                        rshift_even_43 = quotient;
                end
                6'd31: begin
                    quotient = function_value >>> 31;
                    if (function_value[30] &&
                        ((|function_value[29:0]) || quotient[0]))
                        rshift_even_43 = quotient + 43'sd1;
                    else
                        rshift_even_43 = quotient;
                end
                default: rshift_even_43 = function_value;
            endcase
        end
    endfunction

    always @(posedge clk) begin
        if (!rst_n)
            rounded_reg <= 43'sd0;
        else if (ce)
            rounded_reg <= rshift_even_43(value, shift_count);
    end

    assign rounded = rounded_reg;
endmodule

module pointwise_conv_pe (
    input wire clk,
    input wire rst_n,
    input wire cfg_valid,
    output wire cfg_ready,
    input wire [255:0] cfg_desc,
    output wire done,
    output wire fault,
    input wire [255:0] s_pixel_data,
    input wire s_pixel_valid,
    output wire s_pixel_ready,
    input wire [31:0] s_pixel_mask,
    input wire [63:0] s_pixel_tag,
    output wire [63:0] m_value_data,
    output wire m_value_valid,
    input wire m_value_ready,
    output wire [3:0] m_value_mask,
    output wire [63:0] m_value_tag,
    output wire pw_req_valid,
    input wire pw_req_ready,
    output wire [10:0] pw_req_addr,
    input wire pw_rsp_valid,
    output wire pw_rsp_ready,
    input wire [1023:0] pw_rsp_data,
    input wire [255:0] pw_rsp_params,
    output wire [6:0] pw_req_group
);

    localparam [1:0] SLOT_EMPTY = 2'd0;
    localparam [1:0] SLOT_FILL  = 2'd1;
    localparam [1:0] SLOT_FULL  = 2'd2;
    localparam [1:0] SLOT_READ  = 2'd3;

    reg active;
    reg fault_reg;
    reg done_reg;

    reg [4:0] cfg_op_id;
    reg [1:0] cfg_mode;
    reg [8:0] cfg_hin, cfg_win, cfg_hout, cfg_wout;
    reg [8:0] cfg_cin, cfg_cout;
    reg [5:0] cfg_shift;
    reg [3:0] gin_reg;
    reg [6:0] gout_reg;
    reg [3:0] last_batch_reg;
    reg [6:0] last_group_reg;
    reg [7:0] last_input_row_reg;
    reg [7:0] last_input_col_reg;
    reg [4:0] expected_input_op_reg;
    reg [31:0] last_input_mask_reg;

    /* 2 slots x 12 input batches x 256 bits = 6144 bits.  This is the
       specified pixel cache, not a weight or parameter RAM. */
    (* ram_style = "distributed" *) reg [255:0] pixel_mem [0:23];
    reg [1:0] slot_state [0:1];
    reg [7:0] slot_row [0:1];
    reg [7:0] slot_col [0:1];

    reg fill_active;
    reg fill_slot;
    reg next_fill_slot;
    reg [7:0] input_row;
    reg [7:0] input_col;
    reg [3:0] input_batch;
    reg input_complete;

    reg sched_active;
    reg sched_slot;
    reg next_sched_slot;
    reg [6:0] sched_group;
    reg [3:0] sched_batch;

    reg pending_valid;
    reg pending_slot;
    reg [6:0] pending_group;
    reg [3:0] pending_batch;

    reg [10:0] pipe_valid;
    reg [6:0] first_pipe;
    reg [6:0] last_pipe;
    reg [63:0] tag_pipe [0:10];
    reg [3:0] mask_pipe [0:10];
    reg slot_pipe [0:10];
    reg [255:0] params_pipe [0:7];

    wire signed [15:0] p0_product [0:127];
    wire signed [16:0] p1_sum [0:63];
    (* use_dsp = "no" *) reg signed [17:0] p2_sum [0:31];
    (* use_dsp = "no" *) reg signed [18:0] p3_sum [0:15];
    (* use_dsp = "no" *) reg signed [19:0] p4_sum [0:7];
    (* use_dsp = "no" *) reg signed [20:0] p5_sum [0:3];
    (* use_dsp = "no" *) reg signed [23:0] accum [0:3];
    reg signed [23:0] p6_acc_value [0:3];
    reg signed [24:0] p7_bias_value [0:3];
    (* use_dsp = "yes" *) reg signed [42:0] p8_product [0:3];
    wire signed [42:0] p9_rounded [0:3];
    reg [63:0] p10_data;

    integer lane;
    integer term;
    integer stage_index;
    genvar p0_mac_index;
    genvar p1_add_index;
    genvar p9_lane_index;

    function [3:0] output_mask;
        input [8:0] channels;
        input [6:0] group_index;
        reg [9:0] remaining;
        begin
            remaining = {1'b0, channels} - {1'b0, group_index, 2'b00};
            if (remaining >= 10'd4)
                output_mask = 4'hf;
            else if (remaining == 0)
                output_mask = 4'h0;
            else
                output_mask = (4'h1 << remaining) - 1'b1;
        end
    endfunction

    function [15:0] saturate_value;
        input signed [42:0] value;
        input [1:0] mode_value;
        begin
            case (mode_value)
                `CNN_MODE_BODY: begin
                    if (value < 0)
                        saturate_value = 16'd0;
                    else if (value > 43'sd127)
                        saturate_value = 16'd127;
                    else
                        saturate_value = {9'd0, value[6:0]};
                end
                `CNN_MODE_HEAT: begin
                    if (value < -43'sd128)
                        saturate_value = 16'hff80;
                    else if (value > 43'sd127)
                        saturate_value = 16'h007f;
                    else
                        saturate_value = {{8{value[7]}}, value[7:0]};
                end
                default: begin
                    if (value < -43'sd32768)
                        saturate_value = 16'h8000;
                    else if (value > 43'sd32767)
                        saturate_value = 16'h7fff;
                    else
                        saturate_value = value[15:0];
                end
            endcase
        end
    endfunction

    function [63:0] make_output_tag;
        input [7:0] row_value;
        input [7:0] col_value;
        input [6:0] group_value;
        reg group_is_last;
        reg pixel_is_final;
        reg [63:0] tag_value;
        begin
            group_is_last = (group_value == last_group_reg);
            pixel_is_final = ({1'b0, row_value} == cfg_hout - 1'b1) &&
                             ({1'b0, col_value} == cfg_wout - 1'b1);
            tag_value = 64'd0;
            tag_value[`TAG_COL_MSB:`TAG_COL_LSB] = col_value;
            tag_value[`TAG_ROW_MSB:`TAG_ROW_LSB] = row_value;
            tag_value[`TAG_BATCH_MSB:`TAG_BATCH_LSB] = 4'd0;
            tag_value[`TAG_GROUP_MSB:`TAG_GROUP_LSB] = group_value;
            tag_value[`TAG_TAP_MSB:`TAG_TAP_LSB] = 4'd0;
            tag_value[`TAG_BATCH_LAST_BIT] = 1'b1;
            tag_value[`TAG_PIXEL_LAST_BIT] = group_is_last;
            tag_value[`TAG_OP_ID_MSB:`TAG_OP_ID_LSB] = cfg_op_id;
            tag_value[`TAG_FRAME_END_BIT] = pixel_is_final && group_is_last;
            tag_value[`TAG_GROUP_LAST_BIT] = group_is_last;
            make_output_tag = tag_value;
        end
    endfunction

    wire cfg_fire = cfg_valid && cfg_ready;
    wire [9:0] cfg_gin_wide = ({1'b0, cfg_desc[`CFG_CIN_MSB:`CFG_CIN_LSB]} + 10'd31) >> 5;
    wire [9:0] cfg_gout_wide = ({1'b0, cfg_desc[`CFG_COUT_MSB:`CFG_COUT_LSB]} + 10'd3) >> 2;
    wire cfg_op_is_pw = ((cfg_desc[`CFG_OP_ID_MSB:`CFG_OP_ID_LSB] >= 5'd2) &&
                         (cfg_desc[`CFG_OP_ID_MSB:`CFG_OP_ID_LSB] <= 5'd28));
    wire cfg_error =
        (cfg_desc[`CFG_KIND_MSB:`CFG_KIND_LSB] != `CNN_KIND_POINTWISE) ||
        (cfg_desc[`CFG_MODE_MSB:`CFG_MODE_LSB] > `CNN_MODE_OFFSET) ||
        !cfg_op_is_pw ||
        (cfg_desc[`CFG_HIN_MSB:`CFG_HIN_LSB] == 0) ||
        (cfg_desc[`CFG_WIN_MSB:`CFG_WIN_LSB] == 0) ||
        (cfg_desc[`CFG_HOUT_MSB:`CFG_HOUT_LSB] == 0) ||
        (cfg_desc[`CFG_WOUT_MSB:`CFG_WOUT_LSB] == 0) ||
        (cfg_desc[`CFG_HIN_MSB:`CFG_HIN_LSB] > 9'd256) ||
        (cfg_desc[`CFG_WIN_MSB:`CFG_WIN_LSB] > 9'd256) ||
        (cfg_desc[`CFG_HOUT_MSB:`CFG_HOUT_LSB] > 9'd256) ||
        (cfg_desc[`CFG_WOUT_MSB:`CFG_WOUT_LSB] > 9'd256) ||
        (cfg_desc[`CFG_HIN_MSB:`CFG_HIN_LSB] != cfg_desc[`CFG_HOUT_MSB:`CFG_HOUT_LSB]) ||
        (cfg_desc[`CFG_WIN_MSB:`CFG_WIN_LSB] != cfg_desc[`CFG_WOUT_MSB:`CFG_WOUT_LSB]) ||
        (cfg_desc[`CFG_CIN_MSB:`CFG_CIN_LSB] == 0) ||
        (cfg_desc[`CFG_COUT_MSB:`CFG_COUT_LSB] == 0) ||
        (cfg_desc[`CFG_CIN_MSB:`CFG_CIN_LSB] > 9'd384) ||
        (cfg_desc[`CFG_COUT_MSB:`CFG_COUT_LSB] > 9'd384) ||
        (cfg_desc[`CFG_SHIFT_MSB:`CFG_SHIFT_LSB] > 6'd31) ||
        (cfg_gin_wide == 0) || (cfg_gin_wide > 10'd12) ||
        (cfg_gout_wide == 0) || (cfg_gout_wide > 10'd96);

    wire selected_fill_slot = fill_active ? fill_slot : next_fill_slot;
    wire selected_fill_empty = (slot_state[selected_fill_slot] == SLOT_EMPTY);
    assign s_pixel_ready = rst_n && active && !fault_reg && !input_complete &&
                           (fill_active || selected_fill_empty);
    wire input_fire = s_pixel_valid && s_pixel_ready;
    wire input_last_batch = (input_batch == last_batch_reg);
    wire input_final_pixel = (input_row == last_input_row_reg) &&
                             (input_col == last_input_col_reg);
    wire [31:0] expected_input_mask = input_last_batch ?
                                      last_input_mask_reg : 32'hffffffff;
    wire input_protocol_ok =
        (s_pixel_tag[`TAG_COL_MSB:`TAG_COL_LSB] == input_col) &&
        (s_pixel_tag[`TAG_ROW_MSB:`TAG_ROW_LSB] == input_row) &&
        (s_pixel_tag[`TAG_BATCH_MSB:`TAG_BATCH_LSB] == input_batch) &&
        (s_pixel_tag[`TAG_GROUP_MSB:`TAG_GROUP_LSB] == 0) &&
        (s_pixel_tag[`TAG_TAP_MSB:`TAG_TAP_LSB] == 0) &&
        (s_pixel_tag[`TAG_BATCH_LAST_BIT] == input_last_batch) &&
        (s_pixel_tag[`TAG_PIXEL_LAST_BIT] == input_last_batch) &&
        (s_pixel_tag[`TAG_OP_ID_MSB:`TAG_OP_ID_LSB] == expected_input_op_reg) &&
        (s_pixel_tag[`TAG_FRAME_END_BIT] == (input_final_pixel && input_last_batch)) &&
        (s_pixel_tag[`TAG_GROUP_LAST_BIT] == 1'b0) &&
        (s_pixel_tag[`TAG_RESERVED_MSB:`TAG_RESERVED_LSB] == 0) &&
        (s_pixel_mask == expected_input_mask);
    wire input_good = input_fire && input_protocol_ok;
    wire [4:0] input_mem_index = (selected_fill_slot ? 5'd12 : 5'd0) + input_batch;

    wire compute_ce = !pipe_valid[10] || m_value_ready;
    assign pw_rsp_ready = rst_n && active && !fault_reg && compute_ce && pending_valid;
    wire rsp_fire = pw_rsp_valid && pw_rsp_ready;
    wire request_credit = !pending_valid || rsp_fire;
    // A new offer requires compute credit; an unaccepted offer must persist.
    // Scheduler address/group only advance on req_fire, so no payload copy is needed.
    reg request_hold_valid;
    assign pw_req_valid = rst_n && active && !fault_reg &&
                          (request_hold_valid || (compute_ce && sched_active && request_credit));
    assign pw_req_group = sched_group;
    assign pw_req_addr = ({4'd0, sched_group} * {7'd0, gin_reg}) + {7'd0, sched_batch};
    wire req_fire = pw_req_valid && pw_req_ready;
    always @(posedge clk) begin
        if (!rst_n || cfg_fire || !active || fault_reg)
            request_hold_valid <= 1'b0;
        else
            request_hold_valid <= pw_req_valid && !pw_req_ready;
    end
    wire unexpected_rsp = active && !fault_reg && pw_rsp_valid && !pending_valid;

    wire [4:0] pending_mem_index = (pending_slot ? 5'd12 : 5'd0) + pending_batch;
    wire [255:0] pending_pixel_word = pixel_mem[pending_mem_index];
    wire mac_issue = rsp_fire;
    wire mac_issue_last = mac_issue && (pending_batch == last_batch_reg);
    wire p10_load = active && !fault_reg && compute_ce && pipe_valid[9];

    generate
        for (p0_mac_index = 0; p0_mac_index < 128;
             p0_mac_index = p0_mac_index + 1) begin : gen_p0_dsp
            pointwise_u8s8_dsp_mult u_p0_mult (
                .clk(clk),
                .ce(mac_issue),
                .activation(pending_pixel_word[
                    (p0_mac_index % `CNN_W_IN)*8 +: 8]),
                .weight_bits(pw_rsp_data[p0_mac_index*8 +: 8]),
                .product(p0_product[p0_mac_index])
            );
        end
        for (p1_add_index = 0; p1_add_index < 64;
             p1_add_index = p1_add_index + 1) begin : gen_p1_lut
            pointwise_s16_pair_add_lut u_p1_add (
                .clk(clk),
                .ce(active && !fault_reg && compute_ce),
                .lhs(p0_product[p1_add_index*2]),
                .rhs(p0_product[p1_add_index*2+1]),
                .sum(p1_sum[p1_add_index])
            );
        end
        for (p9_lane_index = 0; p9_lane_index < `CNN_W_OUT;
             p9_lane_index = p9_lane_index + 1) begin : gen_p9_lut
            pointwise_rshift_even_43_lut u_p9_rne (
                .clk(clk),
                .rst_n(rst_n),
                .ce(active && !fault_reg && compute_ce && pipe_valid[8]),
                .value(p8_product[p9_lane_index]),
                .shift_count(cfg_shift),
                .rounded(p9_rounded[p9_lane_index])
            );
        end
    endgenerate

    assign m_value_data = p10_data;
    assign m_value_valid = rst_n && !fault_reg && pipe_valid[10];
    assign m_value_mask = mask_pipe[10];
    assign m_value_tag = tag_pipe[10];
    wire output_fire = m_value_valid && m_value_ready;

    assign cfg_ready = rst_n && !active && !fault_reg;
    assign done = rst_n && done_reg;
    assign fault = rst_n && fault_reg;

    always @(posedge clk) begin
        if (!rst_n) begin
            active <= 1'b0;
            fault_reg <= 1'b0;
            done_reg <= 1'b0;
            cfg_op_id <= 5'd0;
            cfg_mode <= 2'd0;
            cfg_hin <= 9'd0; cfg_win <= 9'd0;
            cfg_hout <= 9'd0; cfg_wout <= 9'd0;
            cfg_cin <= 9'd0; cfg_cout <= 9'd0;
            cfg_shift <= 6'd0;
            gin_reg <= 4'd0; gout_reg <= 7'd0;
            last_batch_reg <= 4'd0; last_group_reg <= 7'd0;
            last_input_row_reg <= 8'd0;
            last_input_col_reg <= 8'd0;
            expected_input_op_reg <= 5'd0;
            last_input_mask_reg <= 32'd0;
            slot_state[0] <= SLOT_EMPTY;
            slot_state[1] <= SLOT_EMPTY;
            slot_row[0] <= 8'd0; slot_row[1] <= 8'd0;
            slot_col[0] <= 8'd0; slot_col[1] <= 8'd0;
            fill_active <= 1'b0;
            fill_slot <= 1'b0;
            next_fill_slot <= 1'b0;
            input_row <= 8'd0;
            input_col <= 8'd0;
            input_batch <= 4'd0;
            input_complete <= 1'b0;
            sched_active <= 1'b0;
            sched_slot <= 1'b0;
            next_sched_slot <= 1'b0;
            sched_group <= 7'd0;
            sched_batch <= 4'd0;
            pending_valid <= 1'b0;
            pending_slot <= 1'b0;
            pending_group <= 7'd0;
            pending_batch <= 4'd0;
        end else begin
            done_reg <= 1'b0;

            if (cfg_fire) begin
                if (cfg_error) begin
                    fault_reg <= 1'b1;
                    active <= 1'b0;
                end else begin
                    active <= 1'b1;
                    cfg_op_id <= cfg_desc[`CFG_OP_ID_MSB:`CFG_OP_ID_LSB];
                    cfg_mode <= cfg_desc[`CFG_MODE_MSB:`CFG_MODE_LSB];
                    cfg_hin <= cfg_desc[`CFG_HIN_MSB:`CFG_HIN_LSB];
                    cfg_win <= cfg_desc[`CFG_WIN_MSB:`CFG_WIN_LSB];
                    cfg_hout <= cfg_desc[`CFG_HOUT_MSB:`CFG_HOUT_LSB];
                    cfg_wout <= cfg_desc[`CFG_WOUT_MSB:`CFG_WOUT_LSB];
                    cfg_cin <= cfg_desc[`CFG_CIN_MSB:`CFG_CIN_LSB];
                    cfg_cout <= cfg_desc[`CFG_COUT_MSB:`CFG_COUT_LSB];
                    cfg_shift <= cfg_desc[`CFG_SHIFT_MSB:`CFG_SHIFT_LSB];
                    gin_reg <= cfg_gin_wide[3:0];
                    gout_reg <= cfg_gout_wide[6:0];
                    last_batch_reg <= cfg_gin_wide[3:0] - 1'b1;
                    last_group_reg <= cfg_gout_wide[6:0] - 1'b1;
                    last_input_row_reg <=
                        cfg_desc[`CFG_HIN_LSB +: 8] - 1'b1;
                    last_input_col_reg <=
                        cfg_desc[`CFG_WIN_LSB +: 8] - 1'b1;
                    expected_input_op_reg <=
                        (cfg_desc[`CFG_OP_ID_MSB:`CFG_OP_ID_LSB] >= 5'd27) ?
                        cfg_desc[`CFG_OP_ID_MSB:`CFG_OP_ID_LSB] :
                        (cfg_desc[`CFG_OP_ID_MSB:`CFG_OP_ID_LSB] - 1'b1);
                    if (cfg_desc[`CFG_CIN_LSB +: 5] == 0)
                        last_input_mask_reg <= 32'hffffffff;
                    else
                        last_input_mask_reg <=
                            (32'h1 << cfg_desc[`CFG_CIN_LSB +: 5]) - 1'b1;
                    slot_state[0] <= SLOT_EMPTY;
                    slot_state[1] <= SLOT_EMPTY;
                    fill_active <= 1'b0;
                    fill_slot <= 1'b0;
                    next_fill_slot <= 1'b0;
                    input_row <= 8'd0;
                    input_col <= 8'd0;
                    input_batch <= 4'd0;
                    input_complete <= 1'b0;
                    sched_active <= 1'b0;
                    sched_slot <= 1'b0;
                    next_sched_slot <= 1'b0;
                    sched_group <= 7'd0;
                    sched_batch <= 4'd0;
                    pending_valid <= 1'b0;
                end
            end else if (active && !fault_reg) begin
                if (unexpected_rsp || (input_fire && !input_protocol_ok)) begin
                    fault_reg <= 1'b1;
                    sched_active <= 1'b0;
                    pending_valid <= 1'b0;
                end else begin
                    if (input_good) begin
                        for (lane = 0; lane < `CNN_W_IN; lane = lane + 1) begin
                            if (s_pixel_mask[lane])
                                pixel_mem[input_mem_index][lane*8 +: 8] <=
                                    s_pixel_data[lane*8 +: 8];
                            else
                                pixel_mem[input_mem_index][lane*8 +: 8] <= 8'd0;
                        end
                        if (!fill_active) begin
                            fill_active <= !input_last_batch;
                            fill_slot <= selected_fill_slot;
                            slot_row[selected_fill_slot] <= input_row;
                            slot_col[selected_fill_slot] <= input_col;
                        end
                        if (input_last_batch) begin
                            slot_state[selected_fill_slot] <= SLOT_FULL;
                            fill_active <= 1'b0;
                            next_fill_slot <= ~selected_fill_slot;
                            input_batch <= 4'd0;
                            if (input_final_pixel) begin
                                input_complete <= 1'b1;
                            end else if ({1'b0, input_col} == cfg_win - 1'b1) begin
                                input_col <= 8'd0;
                                input_row <= input_row + 1'b1;
                            end else begin
                                input_col <= input_col + 1'b1;
                            end
                        end else begin
                            slot_state[selected_fill_slot] <= SLOT_FILL;
                            fill_active <= 1'b1;
                            fill_slot <= selected_fill_slot;
                            input_batch <= input_batch + 1'b1;
                        end
                    end

                    if (!sched_active && (slot_state[next_sched_slot] == SLOT_FULL)) begin
                        sched_active <= 1'b1;
                        sched_slot <= next_sched_slot;
                        slot_state[next_sched_slot] <= SLOT_READ;
                        next_sched_slot <= ~next_sched_slot;
                        sched_group <= 7'd0;
                        sched_batch <= 4'd0;
                    end

                    case ({req_fire, rsp_fire})
                        2'b10, 2'b11: begin
                            pending_valid <= 1'b1;
                            pending_slot <= sched_slot;
                            pending_group <= sched_group;
                            pending_batch <= sched_batch;
                        end
                        2'b01: pending_valid <= 1'b0;
                        default: pending_valid <= pending_valid;
                    endcase

                    if (req_fire) begin
                        if ((sched_group == last_group_reg) &&
                            (sched_batch == last_batch_reg)) begin
                            if (slot_state[next_sched_slot] == SLOT_FULL) begin
                                sched_active <= 1'b1;
                                sched_slot <= next_sched_slot;
                                slot_state[next_sched_slot] <= SLOT_READ;
                                next_sched_slot <= ~next_sched_slot;
                                sched_group <= 7'd0;
                                sched_batch <= 4'd0;
                            end else begin
                                sched_active <= 1'b0;
                            end
                        end else if (sched_batch == last_batch_reg) begin
                            sched_batch <= 4'd0;
                            sched_group <= sched_group + 1'b1;
                        end else begin
                            sched_batch <= sched_batch + 1'b1;
                        end
                    end

                    if (output_fire && tag_pipe[10][`TAG_GROUP_LAST_BIT]) begin
                        slot_state[slot_pipe[10]] <= SLOT_EMPTY;
                        if (tag_pipe[10][`TAG_FRAME_END_BIT]) begin
                            active <= 1'b0;
                            done_reg <= 1'b1;
                            sched_active <= 1'b0;
                            pending_valid <= 1'b0;
                        end
                    end
                end
            end
        end
    end

    always @(posedge clk) begin
        if (!rst_n) begin
            pipe_valid <= 11'd0;
            first_pipe <= 7'd0;
            last_pipe <= 7'd0;
            p10_data <= 64'd0;
            for (lane = 0; lane < `CNN_W_OUT; lane = lane + 1) begin
                accum[lane] <= 24'sd0;
                p6_acc_value[lane] <= 24'sd0;
                p7_bias_value[lane] <= 25'sd0;
                p8_product[lane] <= 43'sd0;
            end
            for (stage_index = 0; stage_index <= 10; stage_index = stage_index + 1) begin
                tag_pipe[stage_index] <= 64'd0;
                mask_pipe[stage_index] <= 4'd0;
                slot_pipe[stage_index] <= 1'b0;
            end
            for (stage_index = 0; stage_index <= 7; stage_index = stage_index + 1)
                params_pipe[stage_index] <= 256'd0;
        end else if (cfg_fire) begin
            pipe_valid <= 11'd0;
            first_pipe <= 7'd0;
            last_pipe <= 7'd0;
            p10_data <= 64'd0;
            for (lane = 0; lane < `CNN_W_OUT; lane = lane + 1)
                accum[lane] <= 24'sd0;
        end else if (active && !fault_reg && compute_ce) begin
            pipe_valid[0] <= mac_issue;
            pipe_valid[1] <= pipe_valid[0];
            pipe_valid[2] <= pipe_valid[1];
            pipe_valid[3] <= pipe_valid[2];
            pipe_valid[4] <= pipe_valid[3];
            pipe_valid[5] <= pipe_valid[4];
            pipe_valid[6] <= pipe_valid[5];
            pipe_valid[7] <= pipe_valid[6] && last_pipe[6];
            pipe_valid[8] <= pipe_valid[7];
            pipe_valid[9] <= pipe_valid[8];
            pipe_valid[10] <= pipe_valid[9];

            first_pipe[0] <= (pending_batch == 0);
            last_pipe[0] <= (pending_batch == last_batch_reg);
            for (stage_index = 1; stage_index <= 6; stage_index = stage_index + 1) begin
                first_pipe[stage_index] <= first_pipe[stage_index-1];
                last_pipe[stage_index] <= last_pipe[stage_index-1];
            end

            if (mac_issue) begin
                params_pipe[0] <= pw_rsp_params;
                tag_pipe[0] <= make_output_tag(
                    slot_row[pending_slot], slot_col[pending_slot], pending_group);
                mask_pipe[0] <= output_mask(cfg_cout, pending_group);
                slot_pipe[0] <= pending_slot;
            end

            for (stage_index = 1; stage_index <= 10; stage_index = stage_index + 1) begin
                tag_pipe[stage_index] <= tag_pipe[stage_index-1];
                mask_pipe[stage_index] <= mask_pipe[stage_index-1];
                slot_pipe[stage_index] <= slot_pipe[stage_index-1];
            end
            for (stage_index = 1; stage_index <= 7; stage_index = stage_index + 1)
                params_pipe[stage_index] <= params_pipe[stage_index-1];

            for (lane = 0; lane < `CNN_W_OUT; lane = lane + 1)
                for (term = 0; term < 8; term = term + 1)
                    p2_sum[lane*8+term] <=
                        $signed({p1_sum[lane*16+term*2][16],
                                 p1_sum[lane*16+term*2]}) +
                        $signed({p1_sum[lane*16+term*2+1][16],
                                 p1_sum[lane*16+term*2+1]});

            for (lane = 0; lane < `CNN_W_OUT; lane = lane + 1)
                for (term = 0; term < 4; term = term + 1)
                    p3_sum[lane*4+term] <=
                        $signed({p2_sum[lane*8+term*2][17],
                                 p2_sum[lane*8+term*2]}) +
                        $signed({p2_sum[lane*8+term*2+1][17],
                                 p2_sum[lane*8+term*2+1]});

            for (lane = 0; lane < `CNN_W_OUT; lane = lane + 1)
                for (term = 0; term < 2; term = term + 1)
                    p4_sum[lane*2+term] <=
                        $signed({p3_sum[lane*4+term*2][18],
                                 p3_sum[lane*4+term*2]}) +
                        $signed({p3_sum[lane*4+term*2+1][18],
                                 p3_sum[lane*4+term*2+1]});

            for (lane = 0; lane < `CNN_W_OUT; lane = lane + 1)
                p5_sum[lane] <=
                    $signed({p4_sum[lane*2][19], p4_sum[lane*2]}) +
                    $signed({p4_sum[lane*2+1][19], p4_sum[lane*2+1]});

            if (pipe_valid[5]) begin
                for (lane = 0; lane < `CNN_W_OUT; lane = lane + 1) begin
                    if (first_pipe[5]) begin
                        accum[lane] <= {{3{p5_sum[lane][20]}}, p5_sum[lane]};
                        p6_acc_value[lane] <= {{3{p5_sum[lane][20]}}, p5_sum[lane]};
                    end else begin
                        accum[lane] <= accum[lane] +
                                       {{3{p5_sum[lane][20]}}, p5_sum[lane]};
                        p6_acc_value[lane] <= accum[lane] +
                                             {{3{p5_sum[lane][20]}}, p5_sum[lane]};
                    end
                end
            end

            if (pipe_valid[6] && last_pipe[6]) begin
                for (lane = 0; lane < `CNN_W_OUT; lane = lane + 1)
                    p7_bias_value[lane] <=
                        $signed({p6_acc_value[lane][23], p6_acc_value[lane]}) +
                        $signed({params_pipe[6][lane*64+23],
                                 params_pipe[6][lane*64 +: 24]});
            end

            if (pipe_valid[7]) begin
                for (lane = 0; lane < `CNN_W_OUT; lane = lane + 1)
                    p8_product[lane] <=
                        $signed(p7_bias_value[lane]) *
                        $signed({1'b0, params_pipe[7][lane*64+32 +: 17]});
            end

            if (pipe_valid[9]) begin
                for (lane = 0; lane < `CNN_W_OUT; lane = lane + 1) begin
                    if (mask_pipe[9][lane])
                        p10_data[lane*16 +: 16] <=
                            saturate_value(p9_rounded[lane], cfg_mode);
                    else
                        p10_data[lane*16 +: 16] <= 16'd0;
                end
            end
        end
    end

endmodule
