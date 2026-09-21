`timescale 1ns / 1ps
// CNN-v4.0 Conv0 processing element.
// Fixed geometry: 256x256x3 -> 128x128x24, K3/S2/D1/P1.
// Verilog-2001 only; rst_n is sampled synchronously.
module input_conv_pe (
    input  wire         clk,
    input  wire         rst_n,
    input  wire         cfg_valid,
    output wire         cfg_ready,
    input  wire [255:0] cfg_desc,
    output wire         done,
    output wire         fault,
    input  wire [23:0]  s_rgb_data,
    input  wire         s_rgb_valid,
    output wire         s_rgb_ready,
    input  wire [63:0]  s_rgb_tag,
    output wire [63:0]  m_body_data,
    output wire         m_body_valid,
    input  wire         m_body_ready,
    output wire [3:0]   m_body_mask,
    output wire [63:0]  m_body_tag,
    output wire         conv0_req_valid,
    input  wire         conv0_req_ready,
    output wire [4:0]   conv0_req_addr,
    input  wire         conv0_rsp_valid,
    output wire         conv0_rsp_ready,
    input  wire [215:0] conv0_rsp_data,
    input  wire [63:0]  conv0_rsp_params
);

    localparam [2:0] ST_IDLE=3'd0, ST_FILL=3'd1, ST_WINDOW=3'd2,
                     ST_ISSUE=3'd3, ST_DRAIN=3'd4, ST_EMIT=3'd5,
                     ST_DONE=3'd6, ST_FAULT=3'd7;
    localparam signed [9:0] PAD_SAMPLE = 10'sd255;

    reg [2:0] state;
    reg [255:0] cfg_latched;
    reg sticky_fault, done_r;
    reg [7:0] in_row, in_col;
    reg [16:0] input_count;

    // Two independent one-read/one-write READ_FIRST row delays. RAM contents
    // are intentionally not reset.
    (* ram_style = "block" *) reg [23:0] row1_mem [0:255];
    (* ram_style = "block" *) reg [23:0] row2_mem [0:255];
    reg [23:0] row1_dout, row2_dout;
    reg row1_pipe_valid;
    reg [7:0] row1_pipe_addr;

    // One cycle aligns current RGB/tag/position with the synchronous RAM data.
    reg pending_valid;
    reg [23:0] pending_rgb;
    reg [63:0] pending_tag;
    reg [7:0] pending_row, pending_col;

    // Previous two samples of each window row; component order is R,G,B.
    reg signed [9:0] top_m2 [0:2];
    reg signed [9:0] top_m1 [0:2];
    reg signed [9:0] mid_m2 [0:2];
    reg signed [9:0] mid_m1 [0:2];
    reg signed [9:0] bot_m2 [0:2];
    reg signed [9:0] bot_m1 [0:2];
    // Frozen order is 3*(3*ky+kx)+rgb.
    reg signed [9:0] window_sample [0:26];
    reg [6:0] window_row, window_col;

    reg [4:0] issue_count, response_count, result_count;
    reg [2:0] emit_group;

    // Ten registered calculation stages: product, five adder levels, bias,
    // multiply, RNE, then clip/cache.
    (* use_dsp = "yes" *) reg signed [17:0] product_s0 [0:25];
    (* use_dsp = "yes", use_dsp48 = "yes", dont_touch = "yes" *)
    reg signed [17:0] product26_s0;
    (* use_dsp = "no" *) reg signed [24:0] sum_s1 [0:13];
    (* use_dsp = "no" *) reg signed [24:0] sum_s2 [0:6];
    (* use_dsp = "no" *) reg signed [24:0] sum_s3 [0:3];
    (* use_dsp = "no" *) reg signed [24:0] sum_s4 [0:1];
    (* use_dsp = "no" *) reg signed [24:0] sum_s5;
    reg signed [23:0] bias_s0, bias_s1, bias_s2, bias_s3, bias_s4, bias_s5;
    reg signed [17:0] mult_s0, mult_s1, mult_s2, mult_s3, mult_s4, mult_s5;
    (* use_dsp = "no", dont_touch = "yes" *) reg signed [24:0] biased_s6;
    reg signed [17:0] mult_s6;
    (* use_dsp = "yes", dont_touch = "yes" *)
    reg signed [42:0] requant_product_s7;
    (* use_dsp = "no" *) reg signed [42:0] rounded_s8;
    reg [4:0] channel_s0, channel_s1, channel_s2, channel_s3, channel_s4;
    reg [4:0] channel_s5, channel_s6, channel_s7, channel_s8;
    reg valid_s0, valid_s1, valid_s2, valid_s3, valid_s4;
    reg valid_s5, valid_s6, valid_s7, valid_s8;
    reg [7:0] result_cache [0:23];
    reg [63:0] body_data_r;
    integer i;

    function signed [9:0] expand_pixel;
        input [7:0] pixel;
        begin
            expand_pixel = {1'b0, pixel, 1'b0};
        end
    endfunction

    function signed [42:0] round_even_shift17;
        input signed [42:0] value;
        reg signed [42:0] q, r;
        begin
            q = value >>> 17;
            r = value - (q <<< 17);
            if ((r > 43'sd65536) || ((r == 43'sd65536) && q[0]))
                round_even_shift17 = q + 43'sd1;
            else
                round_even_shift17 = q;
        end
    endfunction

    function [7:0] clip_body;
        input signed [42:0] value;
        begin
            if (value < 0) clip_body = 8'd0;
            else if (value > 43'sd127) clip_body = 8'd127;
            else clip_body = value[7:0];
        end
    endfunction

    wire cfg_fields_ok =
        (cfg_desc[4:0] == 5'd0) && (cfg_desc[6:5] == 2'd0) &&
        (cfg_desc[8:7] == 2'd0) && (cfg_desc[17:9] == 9'd256) &&
        (cfg_desc[26:18] == 9'd256) && (cfg_desc[35:27] == 9'd128) &&
        (cfg_desc[44:36] == 9'd128) && (cfg_desc[53:45] == 9'd3) &&
        (cfg_desc[62:54] == 9'd24) && (cfg_desc[64:63] == 2'd2) &&
        (cfg_desc[66:65] == 2'd1) && (cfg_desc[68:67] == 2'd1) &&
        (cfg_desc[74:69] == 6'd17) && (cfg_desc[255:149] == 107'd0);

    assign cfg_ready = rst_n && state == ST_IDLE && !sticky_fault;
    wire cfg_accept = cfg_valid && cfg_ready;
    wire pending_trigger = pending_valid && pending_row[0] && pending_col[0];
    assign s_rgb_ready = rst_n && state == ST_FILL && !sticky_fault &&
                         input_count < 17'd65536 && !pending_trigger;
    wire rgb_accept = s_rgb_valid && s_rgb_ready;

    assign conv0_req_valid = rst_n && !sticky_fault && state == ST_ISSUE &&
                             issue_count < 5'd24;
    assign conv0_req_addr = issue_count;
    wire req_accept = conv0_req_valid && conv0_req_ready;
    assign conv0_rsp_ready = rst_n && !sticky_fault &&
                             (state == ST_ISSUE || state == ST_DRAIN) &&
                             response_count < issue_count &&
                             response_count < 5'd24;
    wire rsp_accept = conv0_rsp_valid && conv0_rsp_ready;
    wire rsp_params_ok =
        conv0_rsp_params[31:24] == {8{conv0_rsp_params[23]}} &&
        conv0_rsp_params[63:49] == 15'd0;

    assign m_body_valid = rst_n && !sticky_fault && state == ST_EMIT;
    assign m_body_data = body_data_r;
    assign m_body_mask = 4'b1111;
    assign m_body_tag = {24'd0,
                         emit_group == 3'd5,
                         window_row == 7'd127 && window_col == 7'd127 &&
                             emit_group == 3'd5,
                         cfg_latched[4:0], emit_group == 3'd5,
                         1'b0, 4'd0, {4'd0,emit_group}, 4'd0,
                         {1'b0,window_row}, {1'b0,window_col}};
    wire body_accept = m_body_valid && m_body_ready;
    assign done = done_r;
    assign fault = sticky_fault;

    // Cache and group only change on accepted work, so payload is stall-stable.
    always @* begin
        body_data_r = 64'd0;
        case (emit_group)
            3'd0: body_data_r = {8'd0,result_cache[3], 8'd0,result_cache[2],
                                 8'd0,result_cache[1], 8'd0,result_cache[0]};
            3'd1: body_data_r = {8'd0,result_cache[7], 8'd0,result_cache[6],
                                 8'd0,result_cache[5], 8'd0,result_cache[4]};
            3'd2: body_data_r = {8'd0,result_cache[11],8'd0,result_cache[10],
                                 8'd0,result_cache[9], 8'd0,result_cache[8]};
            3'd3: body_data_r = {8'd0,result_cache[15],8'd0,result_cache[14],
                                 8'd0,result_cache[13],8'd0,result_cache[12]};
            3'd4: body_data_r = {8'd0,result_cache[19],8'd0,result_cache[18],
                                 8'd0,result_cache[17],8'd0,result_cache[16]};
            3'd5: body_data_r = {8'd0,result_cache[23],8'd0,result_cache[22],
                                 8'd0,result_cache[21],8'd0,result_cache[20]};
            default: body_data_r = 64'd0;
        endcase
    end

    // Both RAM addresses and writes advance only on an accepted RGB pixel.
    always @(posedge clk) begin
        if (rgb_accept) begin
            row1_dout        <= row1_mem[in_col];
            row2_dout        <= row2_mem[in_col];
            row1_mem[in_col] <= s_rgb_data;
        end
        // Cascade the previous row into row2 one cycle after its row1 read.
        // The delayed address gives each RAM exactly one read and one write.
        if (row1_pipe_valid)
            row2_mem[row1_pipe_addr] <= row1_dout;
    end

    always @(posedge clk) begin
        if (!rst_n) begin
            state <= ST_IDLE;
            cfg_latched <= 256'd0;
            sticky_fault <= 1'b0;
            done_r <= 1'b0;
            in_row <= 8'd0;
            in_col <= 8'd0;
            input_count <= 17'd0;
            row1_pipe_valid <= 1'b0;
            row1_pipe_addr <= 8'd0;
            pending_valid <= 1'b0;
            pending_rgb <= 24'd0;
            pending_tag <= 64'd0;
            pending_row <= 8'd0;
            pending_col <= 8'd0;
            window_row <= 7'd0;
            window_col <= 7'd0;
            issue_count <= 5'd0;
            response_count <= 5'd0;
            result_count <= 5'd0;
            emit_group <= 3'd0;
            valid_s0 <= 1'b0; valid_s1 <= 1'b0; valid_s2 <= 1'b0;
            valid_s3 <= 1'b0; valid_s4 <= 1'b0; valid_s5 <= 1'b0;
            valid_s6 <= 1'b0; valid_s7 <= 1'b0; valid_s8 <= 1'b0;
            for (i=0; i<3; i=i+1) begin
                top_m2[i] <= PAD_SAMPLE; top_m1[i] <= PAD_SAMPLE;
                mid_m2[i] <= PAD_SAMPLE; mid_m1[i] <= PAD_SAMPLE;
                bot_m2[i] <= PAD_SAMPLE; bot_m1[i] <= PAD_SAMPLE;
            end
            for (i=0; i<27; i=i+1) window_sample[i] <= PAD_SAMPLE;
            for (i=0; i<24; i=i+1) result_cache[i] <= 8'd0;
        end else begin
            done_r <= 1'b0;
            row1_pipe_valid <= rgb_accept;
            if (rgb_accept) row1_pipe_addr <= in_col;

            valid_s0 <= 1'b0;
            valid_s1 <= valid_s0; valid_s2 <= valid_s1;
            valid_s3 <= valid_s2; valid_s4 <= valid_s3;
            valid_s5 <= valid_s4; valid_s6 <= valid_s5;
            valid_s7 <= valid_s6; valid_s8 <= valid_s7;
            channel_s1 <= channel_s0; channel_s2 <= channel_s1;
            channel_s3 <= channel_s2; channel_s4 <= channel_s3;
            channel_s5 <= channel_s4; channel_s6 <= channel_s5;
            channel_s7 <= channel_s6; channel_s8 <= channel_s7;
            bias_s1 <= bias_s0; bias_s2 <= bias_s1; bias_s3 <= bias_s2;
            bias_s4 <= bias_s3; bias_s5 <= bias_s4;
            mult_s1 <= mult_s0; mult_s2 <= mult_s1; mult_s3 <= mult_s2;
            mult_s4 <= mult_s3; mult_s5 <= mult_s4;

            if (valid_s0) begin
                for (i=0; i<13; i=i+1)
                    sum_s1[i] <= {{7{product_s0[2*i][17]}},product_s0[2*i]} +
                                 {{7{product_s0[2*i+1][17]}},product_s0[2*i+1]};
                sum_s1[13] <= {{7{product26_s0[17]}},product26_s0};
            end
            if (valid_s1)
                for (i=0; i<7; i=i+1)
                    sum_s2[i] <= sum_s1[2*i] + sum_s1[2*i+1];
            if (valid_s2) begin
                sum_s3[0] <= sum_s2[0] + sum_s2[1];
                sum_s3[1] <= sum_s2[2] + sum_s2[3];
                sum_s3[2] <= sum_s2[4] + sum_s2[5];
                sum_s3[3] <= sum_s2[6];
            end
            if (valid_s3) begin
                sum_s4[0] <= sum_s3[0] + sum_s3[1];
                sum_s4[1] <= sum_s3[2] + sum_s3[3];
            end
            if (valid_s4) sum_s5 <= sum_s4[0] + sum_s4[1];
            if (valid_s5) begin
                biased_s6 <= sum_s5 + ($signed({bias_s5[23],bias_s5}) <<< 1);
                mult_s6 <= mult_s5;
            end
            if (valid_s6)
                requant_product_s7 <= $signed(biased_s6) * $signed(mult_s6);
            if (valid_s7)
                rounded_s8 <= round_even_shift17(requant_product_s7);

            if (rsp_accept && rsp_params_ok) begin
                valid_s0 <= 1'b1;
                channel_s0 <= response_count;
                bias_s0 <= $signed(conv0_rsp_params[23:0]);
                mult_s0 <= $signed({1'b0,conv0_rsp_params[48:32]});
                for (i=0; i<26; i=i+1)
                    product_s0[i] <= $signed(window_sample[i]) *
                                     $signed(conv0_rsp_data[8*i +: 8]);
                product26_s0 <= $signed(window_sample[26]) *
                                $signed(conv0_rsp_data[215:208]);
            end

            // Clip/cache is stage ten.
            if (valid_s8) begin
                result_cache[channel_s8] <= clip_body(rounded_s8);
                result_count <= result_count + 5'd1;
                if (result_count == 5'd23) begin
                    emit_group <= 3'd0;
                    state <= ST_EMIT;
                end
            end

            // Consume the RGB token aligned to row1_dout/row2_dout.
            if (pending_valid && state == ST_FILL) begin
                if (pending_col == 8'd0) begin
                    top_m2[0] <= PAD_SAMPLE; top_m2[1] <= PAD_SAMPLE;
                    top_m2[2] <= PAD_SAMPLE; mid_m2[0] <= PAD_SAMPLE;
                    mid_m2[1] <= PAD_SAMPLE; mid_m2[2] <= PAD_SAMPLE;
                    bot_m2[0] <= PAD_SAMPLE; bot_m2[1] <= PAD_SAMPLE;
                    bot_m2[2] <= PAD_SAMPLE;
                end else begin
                    for (i=0; i<3; i=i+1) begin
                        top_m2[i] <= top_m1[i];
                        mid_m2[i] <= mid_m1[i];
                        bot_m2[i] <= bot_m1[i];
                    end
                end
                top_m1[0] <= pending_row<8'd2 ? PAD_SAMPLE : expand_pixel(row2_dout[7:0]);
                top_m1[1] <= pending_row<8'd2 ? PAD_SAMPLE : expand_pixel(row2_dout[15:8]);
                top_m1[2] <= pending_row<8'd2 ? PAD_SAMPLE : expand_pixel(row2_dout[23:16]);
                mid_m1[0] <= expand_pixel(row1_dout[7:0]);
                mid_m1[1] <= expand_pixel(row1_dout[15:8]);
                mid_m1[2] <= expand_pixel(row1_dout[23:16]);
                bot_m1[0] <= expand_pixel(pending_rgb[7:0]);
                bot_m1[1] <= expand_pixel(pending_rgb[15:8]);
                bot_m1[2] <= expand_pixel(pending_rgb[23:16]);

                if (pending_trigger) begin
                    window_sample[0] <= top_m2[0];
                    window_sample[1] <= top_m2[1];
                    window_sample[2] <= top_m2[2];
                    window_sample[3] <= top_m1[0];
                    window_sample[4] <= top_m1[1];
                    window_sample[5] <= top_m1[2];
                    window_sample[6] <= pending_row<8'd2 ? PAD_SAMPLE : expand_pixel(row2_dout[7:0]);
                    window_sample[7] <= pending_row<8'd2 ? PAD_SAMPLE : expand_pixel(row2_dout[15:8]);
                    window_sample[8] <= pending_row<8'd2 ? PAD_SAMPLE : expand_pixel(row2_dout[23:16]);
                    window_sample[9] <= mid_m2[0];
                    window_sample[10] <= mid_m2[1];
                    window_sample[11] <= mid_m2[2];
                    window_sample[12] <= mid_m1[0];
                    window_sample[13] <= mid_m1[1];
                    window_sample[14] <= mid_m1[2];
                    window_sample[15] <= expand_pixel(row1_dout[7:0]);
                    window_sample[16] <= expand_pixel(row1_dout[15:8]);
                    window_sample[17] <= expand_pixel(row1_dout[23:16]);
                    window_sample[18] <= bot_m2[0];
                    window_sample[19] <= bot_m2[1];
                    window_sample[20] <= bot_m2[2];
                    window_sample[21] <= bot_m1[0];
                    window_sample[22] <= bot_m1[1];
                    window_sample[23] <= bot_m1[2];
                    window_sample[24] <= expand_pixel(pending_rgb[7:0]);
                    window_sample[25] <= expand_pixel(pending_rgb[15:8]);
                    window_sample[26] <= expand_pixel(pending_rgb[23:16]);
                    window_row <= pending_row[7:1];
                    window_col <= pending_col[7:1];
                    state <= ST_WINDOW;
                end
            end

            case ({rgb_accept,pending_valid && state==ST_FILL})
                2'b10: pending_valid <= 1'b1;
                2'b01: pending_valid <= 1'b0;
                2'b11: pending_valid <= 1'b1;
                default: pending_valid <= pending_valid;
            endcase
            if (rgb_accept) begin
                pending_rgb <= s_rgb_data;
                pending_tag <= s_rgb_tag;
                pending_row <= in_row;
                pending_col <= in_col;
                input_count <= input_count + 17'd1;
                if (in_col == 8'd255) begin
                    in_col <= 8'd0;
                    if (in_row != 8'd255) in_row <= in_row + 8'd1;
                end else in_col <= in_col + 8'd1;
            end

            if (req_accept) begin
                issue_count <= issue_count + 5'd1;
                if (issue_count == 5'd23) state <= ST_DRAIN;
            end
            if (rsp_accept && rsp_params_ok)
                response_count <= response_count + 5'd1;

            case (state)
                ST_IDLE: if (cfg_accept && cfg_fields_ok) begin
                    cfg_latched <= cfg_desc;
                    in_row <= 8'd0; in_col <= 8'd0; input_count <= 17'd0;
                    pending_valid <= 1'b0;
                    window_row <= 7'd0; window_col <= 7'd0;
                    issue_count <= 5'd0; response_count <= 5'd0;
                    result_count <= 5'd0; emit_group <= 3'd0;
                    state <= ST_FILL;
                end
                ST_WINDOW: begin
                    issue_count <= 5'd0;
                    response_count <= 5'd0;
                    result_count <= 5'd0;
                    state <= ST_ISSUE;
                end
                ST_EMIT: if (body_accept) begin
                    if (emit_group == 3'd5) begin
                        if (window_row == 7'd127 && window_col == 7'd127) begin
                            done_r <= 1'b1;
                            state <= ST_DONE;
                        end else begin
                            emit_group <= 3'd0;
                            state <= ST_FILL;
                        end
                    end else emit_group <= emit_group + 3'd1;
                end
                ST_DONE: state <= ST_IDLE;
                ST_FAULT: state <= ST_FAULT;
                default: begin end
            endcase

            // Sticky fault: reset is the only clear mechanism.
            if ((cfg_accept && !cfg_fields_ok) ||
                (rsp_accept && !rsp_params_ok) ||
                (valid_s8 && (channel_s8 >= 5'd24 || result_count >= 5'd24)) ||
                input_count > 17'd65536 || issue_count > 5'd24 ||
                response_count > issue_count || response_count > 5'd24) begin
                sticky_fault <= 1'b1;
                state <= ST_FAULT;
                pending_valid <= 1'b0;
                valid_s0 <= 1'b0; valid_s1 <= 1'b0; valid_s2 <= 1'b0;
                valid_s3 <= 1'b0; valid_s4 <= 1'b0; valid_s5 <= 1'b0;
                valid_s6 <= 1'b0; valid_s7 <= 1'b0; valid_s8 <= 1'b0;
            end
        end
    end
endmodule
