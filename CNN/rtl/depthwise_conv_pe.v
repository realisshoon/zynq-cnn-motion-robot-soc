`timescale 1ns / 1ps

// CNN-v4.0 | Verilog-2001 | full DW-layer implementation
// Scope: one cfg covers all output pixels, Cin 1..384 (1..12 batches),
// 9 taps per batch, body mode.  Weight/parameter storage is owned only by
// weight_bram_swap_fsm; this PE retains only the current response word.
// The 32-lane / 33-DSP kernel and public interface are unchanged.
module depthwise_conv_pe (
    input wire clk,
    input wire rst_n,
    input wire cfg_valid,
    output wire cfg_ready,
    input wire [255:0] cfg_desc,
    output wire done,
    output wire fault,
    input wire [255:0] s_tap_data,
    input wire s_tap_valid,
    output wire s_tap_ready,
    input wire [31:0] s_tap_mask,
    input wire [63:0] s_tap_tag,
    output wire [255:0] m_pixel_data,
    output wire m_pixel_valid,
    input wire m_pixel_ready,
    output wire [31:0] m_pixel_mask,
    output wire [63:0] m_pixel_tag,
    output wire dw_w_req_valid,
    input wire dw_w_req_ready,
    output wire [6:0] dw_w_req_addr,
    input wire dw_w_rsp_valid,
    output wire dw_w_rsp_ready,
    input wire [255:0] dw_w_rsp_data,
    output wire dw_p_req_valid,
    input wire dw_p_req_ready,
    output wire [8:0] dw_p_req_addr,
    input wire dw_p_rsp_valid,
    output wire dw_p_rsp_ready,
    input wire [63:0] dw_p_rsp_data
);

    localparam integer CNN_W_IN = 32;

    localparam [3:0] ST_IDLE = 4'd0;
    localparam [3:0] ST_W_REQ = 4'd1;
    localparam [3:0] ST_QUANT = 4'd4;
    localparam [3:0] ST_EMIT = 4'd5;
    localparam [3:0] ST_FAULT = 4'd15;

    reg [3:0] state;
    reg [3:0] tap_index;
    reg [3:0] batch_index;
    reg [4:0] cfg_op_id;
    reg [5:0] cfg_shift;
    reg [8:0] cfg_cin;
    reg [3:0] cfg_last_batch;
    // Decode lane geometry once when the descriptor is accepted.  Keeping the
    // active values registered removes the batch-index/subtract/variable-mask
    // cone from the tap-accept and requant control enables without adding a
    // datapath pipeline stage.
    reg [5:0] active_lane_count;
    reg [31:0] expected_lane_mask;
    reg [5:0] tail_lane_count;
    reg [31:0] tail_lane_mask;

    reg [255:0] weight_word;
    reg weight_buf_valid;
    reg [3:0] weight_buf_tap;
    reg [3:0] weight_req_count;
    reg weight_outstanding;
    reg [3:0] weight_outstanding_tap;
    (* use_dsp = "yes" *) reg signed [23:0] accum[0:31];
    reg [255:0] result_word;
    reg [31:0] result_mask;
    reg [63:0] result_tag;

    reg done_reg;
    reg fault_reg;

    reg [5:0] param_req_count;
    reg param_outstanding;
    reg [4:0] param_outstanding_lane;
    reg [5:0] result_count;
    reg [5:0] quant_issue_count;

    reg p0_valid;
    reg [4:0] p0_lane;
    // Bias addition is intentionally fabric logic; the shared requant multiplier
    // is the one DSP budgeted for this pipeline.
    (* use_dsp = "no" *) reg signed [24:0] p0_sum;
    reg [16:0] p0_m;
    reg signed [23:0] p0_acc;
    reg signed [23:0] p0_bias;

    reg p1_valid;
    reg [4:0] p1_lane;
    (* use_dsp = "yes" *) reg signed [42:0] p1_product;
    reg signed [23:0] p1_acc;
    reg signed [23:0] p1_bias;
    reg [16:0] p1_m;

    reg p2_valid;
    reg [4:0] p2_lane;
    (* use_dsp = "no" *) reg signed [42:0] p2_rounded;
    reg signed [23:0] p2_acc;
    reg signed [23:0] p2_bias;
    reg [16:0] p2_m;

    // Hierarchical waveform probes; these are not production debug ports.
    reg [4:0] requant_lane;
    reg signed [23:0] current_acc;
    reg signed [23:0] current_bias;
    reg [16:0] current_m;
    reg [7:0] current_result;

    wire signed [23:0] accum_probe0;
    wire signed [23:0] accum_probe1;
    wire signed [23:0] accum_probe31;
    assign accum_probe0  = accum[0];
    assign accum_probe1  = accum[1];
    assign accum_probe31 = accum[31];

    integer lane;

    // Keep each lane's 9x8 signed multiply visible to synthesis.  The original
    // function/in-loop form was numerically correct, but Vivado could cost-map
    // these small multipliers into LUT/CARRY logic.  The attribute is attached to
    // the actual multiply result; no register or cycle is added.
    wire signed [8:0] mac_activation_signed9[0:31];
    wire signed [7:0] mac_weight_signed8[0:31];
    (* use_dsp = "yes" *) wire signed [16:0] mac_product_signed17[0:31];
    wire signed [23:0] mac_term_signed24[0:31];

    genvar mac_lane;
    generate
        for (mac_lane = 0; mac_lane < CNN_W_IN; mac_lane = mac_lane + 1) begin : GEN_DW_MAC
            assign mac_activation_signed9[mac_lane] = $signed({1'b0, s_tap_data[mac_lane*8+:8]});
            assign mac_weight_signed8[mac_lane] = $signed(weight_word[mac_lane*8+:8]);
            assign mac_product_signed17[mac_lane] =
            mac_activation_signed9[mac_lane] * mac_weight_signed8[mac_lane];
            assign mac_term_signed24[mac_lane] = {
                {7{mac_product_signed17[mac_lane][16]}}, mac_product_signed17[mac_lane]
            };
        end
    endgenerate

    function signed [24:0] add_bias25;
        input signed [23:0] acc_value;
        input [31:0] bias_bits;
        reg signed [23:0] bias_value;
        begin
            bias_value = bias_bits[23:0];
            add_bias25 = $signed({acc_value[23], acc_value}) +
                $signed({bias_value[23], bias_value});
        end
    endfunction

    // End the DSP datapath at p1_product.  RNE is expressed only as a signed
    // quotient, guard/sticky/parity tests, and a bitwise ripple increment so
    // Vivado cannot absorb the rounding correction into a second DSP post-adder.
    (* use_dsp = "no" *) wire signed [42:0] rne_logic_quotient;
    reg rne_logic_guard;
    reg rne_logic_sticky;
    wire rne_logic_increment;
    wire [43:0] rne_logic_carry;
    (* use_dsp = "no" *) wire signed [42:0] rne_logic_result;
    integer rne_bit;

    assign rne_logic_quotient = $signed(p1_product) >>> cfg_shift;

    always @* begin
        rne_logic_guard = 1'b0;
        rne_logic_sticky = 1'b0;
        if (cfg_shift != 0) begin
            if (cfg_shift <= 6'd43)
                rne_logic_guard = p1_product[cfg_shift - 1'b1];
            else
                rne_logic_guard = p1_product[42];

            for (rne_bit = 0; rne_bit < 43; rne_bit = rne_bit + 1) begin
                if (rne_bit < (cfg_shift - 1'b1))
                    rne_logic_sticky = rne_logic_sticky | p1_product[rne_bit];
            end
        end
    end

    assign rne_logic_increment = rne_logic_guard &&
        (rne_logic_sticky || rne_logic_quotient[0]);
    assign rne_logic_carry[0] = rne_logic_increment;

    genvar rne_lane;
    generate
        for (rne_lane = 0; rne_lane < 43; rne_lane = rne_lane + 1) begin : GEN_RNE_INCREMENT
            assign rne_logic_result[rne_lane] =
                rne_logic_quotient[rne_lane] ^ rne_logic_carry[rne_lane];
            assign rne_logic_carry[rne_lane + 1] =
                rne_logic_quotient[rne_lane] & rne_logic_carry[rne_lane];
        end
    endgenerate

    function [7:0] clip_body;
        input signed [42:0] value;
        begin
            if (value < 0) clip_body = 8'd0;
            else if (value > 43'sd127) clip_body = 8'd127;
            else clip_body = value[7:0];
        end
    endfunction

    function [63:0] make_output_tag;
        input [63:0] tap_tag;
        reg [63:0] tag_value;
        begin
            tag_value = tap_tag;
            tag_value[30:27] = 4'd0;
            make_output_tag = tag_value;
        end
    endfunction

    wire cfg_fire;
    wire tap_fire;
    wire w_req_fire;
    wire w_rsp_fire;
    wire p_req_fire;
    wire p_rsp_fire;
    wire output_fire;
    wire can_issue_weight;
    wire can_issue_param;
    wire can_issue_zero_lane;
    wire [3:0] next_batch_index;
    wire [6:0] current_weight_base;

    function [31:0] make_lane_mask;
        input [5:0] lane_count;
        begin
            if (lane_count >= 6'd32) make_lane_mask = 32'hffffffff;
            else if (lane_count == 0) make_lane_mask = 32'd0;
            else make_lane_mask = (32'h00000001 << lane_count) - 1'b1;
        end
    endfunction

    assign cfg_ready = rst_n && (state == ST_IDLE) && !fault_reg;
    assign cfg_fire = cfg_valid && cfg_ready;

    assign next_batch_index = batch_index + 1'b1;
    assign current_weight_base = {batch_index, 3'b000}
                               + {{3{1'b0}}, batch_index};

    // Keep at most one weight request outstanding.  A consumed N+1 response may
    // launch the next request on the same edge, while a one-entry holding register
    // decouples the response channel from a stalled tap input.  Every pixel/tap
    // obtains its weight from the external owner; no layer/batch cache exists.
    assign dw_w_rsp_ready = (state == ST_W_REQ) && weight_outstanding &&
                        (!weight_buf_valid || tap_fire);
    assign w_rsp_fire = dw_w_rsp_valid && dw_w_rsp_ready;
    assign can_issue_weight = !weight_outstanding || w_rsp_fire;
    assign dw_w_req_valid = (state == ST_W_REQ) &&
                         (weight_req_count < 4'd9) && can_issue_weight;
    // batch*9 is expressed as batch*8 + batch so address arithmetic cannot
    // consume an additional DSP outside the fixed 33-DSP datapath budget.
    assign dw_w_req_addr = current_weight_base
                     + {{3{1'b0}}, weight_req_count};
    assign w_req_fire = dw_w_req_valid && dw_w_req_ready;

    assign s_tap_ready = (state == ST_W_REQ) && weight_buf_valid;
    assign tap_fire = s_tap_valid && s_tap_ready;

    assign dw_p_rsp_ready = (state == ST_QUANT) && param_outstanding;
    assign can_issue_param = !param_outstanding || (dw_p_rsp_valid && dw_p_rsp_ready);
    assign dw_p_req_valid = (state == ST_QUANT) &&
                        (param_req_count < active_lane_count) &&
                        can_issue_param;
    assign dw_p_req_addr = {batch_index, 5'b00000} + {4'b0000, param_req_count[4:0]};
    assign p_req_fire = dw_p_req_valid && dw_p_req_ready;
    assign p_rsp_fire = dw_p_rsp_valid && dw_p_rsp_ready;
    assign can_issue_zero_lane = (state == ST_QUANT) &&
                             (quant_issue_count < 6'd32) &&
                             (param_req_count >= active_lane_count) &&
                             !param_outstanding && !p_rsp_fire;

    assign m_pixel_data = result_word;
    assign m_pixel_mask = result_mask;
    assign m_pixel_tag = result_tag;
    assign m_pixel_valid = (state == ST_EMIT);
    assign output_fire = m_pixel_valid && m_pixel_ready;

    assign done = done_reg;
    assign fault = fault_reg;

    always @(posedge clk) begin
        if (!rst_n) begin
            state <= ST_IDLE;
            tap_index <= 4'd0;
            batch_index <= 4'd0;
            cfg_op_id <= 5'd0;
            cfg_shift <= 6'd0;
            cfg_cin <= 9'd0;
            cfg_last_batch <= 4'd0;
            active_lane_count <= 6'd0;
            expected_lane_mask <= 32'd0;
            tail_lane_count <= 6'd0;
            tail_lane_mask <= 32'd0;
            weight_word <= 256'd0;
            weight_buf_valid <= 1'b0;
            weight_buf_tap <= 4'd0;
            weight_req_count <= 4'd0;
            weight_outstanding <= 1'b0;
            weight_outstanding_tap <= 4'd0;
            result_word <= 256'd0;
            result_mask <= 32'd0;
            result_tag <= 64'd0;
            done_reg <= 1'b0;
            fault_reg <= 1'b0;
            param_req_count <= 6'd0;
            param_outstanding <= 1'b0;
            param_outstanding_lane <= 5'd0;
            result_count <= 6'd0;
            quant_issue_count <= 6'd0;
            p0_valid <= 1'b0;
            p0_lane <= 5'd0;
            p0_sum <= 25'sd0;
            p0_m <= 17'd0;
            p0_acc <= 24'sd0;
            p0_bias <= 24'sd0;
            p1_valid <= 1'b0;
            p1_lane <= 5'd0;
            p1_product <= 43'sd0;
            p1_acc <= 24'sd0;
            p1_bias <= 24'sd0;
            p1_m <= 17'd0;
            p2_valid <= 1'b0;
            p2_lane <= 5'd0;
            p2_rounded <= 43'sd0;
            p2_acc <= 24'sd0;
            p2_bias <= 24'sd0;
            p2_m <= 17'd0;
            requant_lane <= 5'd0;
            current_acc <= 24'sd0;
            current_bias <= 24'sd0;
            current_m <= 17'd0;
            current_result <= 8'd0;
        end else begin
            done_reg <= 1'b0;

            case (state)
                ST_IDLE: begin
                    if (cfg_fire) begin
                        cfg_op_id <= cfg_desc[4:0];
                        cfg_shift <= cfg_desc[74:69];
                        cfg_cin <= cfg_desc[53:45];
                        cfg_last_batch <= (cfg_desc[53:45] - 1'b1) >> 5;
                        if (cfg_desc[53:45] >= 9'd32) begin
                            active_lane_count <= 6'd32;
                            expected_lane_mask <= 32'hffffffff;
                        end else begin
                            active_lane_count <= cfg_desc[50:45];
                            expected_lane_mask <= make_lane_mask(cfg_desc[50:45]);
                        end
                        if (cfg_desc[49:45] == 5'd0) begin
                            tail_lane_count <= 6'd32;
                            tail_lane_mask <= 32'hffffffff;
                        end else begin
                            tail_lane_count <= {1'b0, cfg_desc[49:45]};
                            tail_lane_mask <= make_lane_mask(
                                {1'b0, cfg_desc[49:45]}
                            );
                        end
                        tap_index <= 4'd0;
                        batch_index <= 4'd0;
                        weight_buf_valid <= 1'b0;
                        weight_buf_tap <= 4'd0;
                        weight_req_count <= 4'd0;
                        weight_outstanding <= 1'b0;
                        weight_outstanding_tap <= 4'd0;
                        result_word <= 256'd0;
                        result_mask <= 32'd0;
                        result_tag <= 64'd0;

                        // Full CNN-v4.0 DW range is 1..384 channels, or at most
                        // twelve 32-lane batches.  The 4-bit batch TAG field and
                        // 7-bit batch*9+tap address cover this range exactly.
                        if ((cfg_desc[6:5] != 2'd1) ||
                        (cfg_desc[8:7] != 2'd0) ||
                        (cfg_desc[17:9] == 9'd0) ||
                        (cfg_desc[17:9] > 9'd256) ||
                        (cfg_desc[26:18] == 9'd0) ||
                        (cfg_desc[26:18] > 9'd256) ||
                        (cfg_desc[35:27] == 9'd0) ||
                        (cfg_desc[35:27] > 9'd256) ||
                        (cfg_desc[44:36] == 9'd0) ||
                        (cfg_desc[44:36] > 9'd256) ||
                        (cfg_desc[53:45] == 9'd0) ||
                        (cfg_desc[53:45] > 9'd384) ||
                        (cfg_desc[62:54] != cfg_desc[53:45]) ||
                        ((cfg_desc[64:63] != 2'd1) &&
                         (cfg_desc[64:63] != 2'd2)) ||
                        (cfg_desc[66:65] != 2'd1) ||
                        (cfg_desc[68:67] != 2'd1) ||
                        (cfg_desc[74:69] != 6'd16) ||
                        (cfg_desc[255:149] != 107'd0)) begin
                            fault_reg <= 1'b1;
                            state <= ST_FAULT;
                        end else begin
                            state <= ST_W_REQ;
                        end
                    end
                end

                ST_W_REQ: begin
                    if (w_rsp_fire) begin
                        weight_word <= dw_w_rsp_data;
                        weight_buf_tap <= weight_outstanding_tap;
                    end

                    case ({
                        tap_fire, w_rsp_fire
                    })
                        2'b10: begin
                            weight_buf_valid <= 1'b0;
                        end
                        2'b01:   weight_buf_valid <= 1'b1;
                        2'b11:   weight_buf_valid <= 1'b1;
                        default: weight_buf_valid <= weight_buf_valid;
                    endcase

                    case ({
                        w_req_fire, w_rsp_fire
                    })
                        2'b10: begin
                            weight_outstanding <= 1'b1;
                            weight_outstanding_tap <= weight_req_count;
                            weight_req_count <= weight_req_count + 1'b1;
                        end
                        2'b01: begin
                            weight_outstanding <= 1'b0;
                        end
                        2'b11: begin
                            weight_outstanding <= 1'b1;
                            weight_outstanding_tap <= weight_req_count;
                            weight_req_count <= weight_req_count + 1'b1;
                        end
                        default: begin
                            weight_outstanding <= weight_outstanding;
                        end
                    endcase

                    if (tap_fire) begin
                        if ((s_tap_tag[63:40] != 24'd0) ||
                        (s_tap_tag[37:33] != cfg_op_id) ||
                        (s_tap_tag[30:27] != tap_index) ||
                        (s_tap_tag[26:20] != 7'd0) ||
                        (s_tap_tag[19:16] != batch_index) ||
                        (s_tap_mask != expected_lane_mask) ||
                        (weight_buf_tap != tap_index)) begin
                            fault_reg <= 1'b1;
                            state <= ST_FAULT;
                        end else begin
                            for (lane = 0; lane < CNN_W_IN; lane = lane + 1) begin
                                if (!expected_lane_mask[lane]) accum[lane] <= 24'sd0;
                                else if (tap_index == 4'd0) accum[lane] <= mac_term_signed24[lane];
                                else accum[lane] <= accum[lane] + mac_term_signed24[lane];
                            end

                            if (tap_index == 4'd8) begin
                                result_mask <= s_tap_mask;
                                result_tag <= make_output_tag(s_tap_tag);
                                param_req_count <= 6'd0;
                                param_outstanding <= 1'b0;
                                param_outstanding_lane <= 5'd0;
                                result_count <= 6'd0;
                                quant_issue_count <= 6'd0;
                                p0_valid <= 1'b0;
                                p1_valid <= 1'b0;
                                p2_valid <= 1'b0;
                                state <= ST_QUANT;
                            end else begin
                                tap_index <= tap_index + 1'b1;
                            end
                        end
                    end
                end

                ST_QUANT: begin
                    // One shared multiplier, with lane issue initiation interval 1.
                    p0_valid <= 1'b0;
                    p1_valid <= p0_valid;
                    p2_valid <= p1_valid;

                    if (p0_valid) begin
                        p1_lane <= p0_lane;
                        p1_product <= $signed(p0_sum) * $signed({1'b0, p0_m});
                        p1_acc <= p0_acc;
                        p1_bias <= p0_bias;
                        p1_m <= p0_m;
                    end

                    if (p1_valid) begin
                        p2_lane <= p1_lane;
                        p2_rounded <= rne_logic_result;
                        p2_acc <= p1_acc;
                        p2_bias <= p1_bias;
                        p2_m <= p1_m;
                    end

                    if (p2_valid) begin
                        result_word[p2_lane*8+:8] <= clip_body(p2_rounded);
                        requant_lane <= p2_lane;
                        current_acc <= p2_acc;
                        current_bias <= p2_bias;
                        current_m <= p2_m;
                        current_result <= clip_body(p2_rounded);
                        result_count <= result_count + 1'b1;
                        if (result_count == 6'd31) state <= ST_EMIT;
                    end

                    if (p_rsp_fire) begin
                        if ((dw_p_rsp_data[31:24] != {8{dw_p_rsp_data[23]}}) ||
                        (dw_p_rsp_data[63:49] != 15'd0)) begin
                            fault_reg <= 1'b1;
                            state <= ST_FAULT;
                        end else begin
                            p0_valid <= 1'b1;
                            p0_lane <= param_outstanding_lane;
                            p0_sum <= add_bias25(
                                accum[param_outstanding_lane], dw_p_rsp_data[31:0]
                            );
                            p0_m <= dw_p_rsp_data[48:32];
                            p0_acc <= accum[param_outstanding_lane];
                            p0_bias <= dw_p_rsp_data[23:0];
                            quant_issue_count <= quant_issue_count + 1'b1;
                        end
                    end else if (can_issue_zero_lane) begin
                        // Invalid tail lanes consume no parameter read but still
                        // occupy one lane slot, preserving the 32-lane schedule.
                        p0_valid <= 1'b1;
                        p0_lane <= quant_issue_count[4:0];
                        p0_sum <= 25'sd0;
                        p0_m <= 17'd0;
                        p0_acc <= 24'sd0;
                        p0_bias <= 24'sd0;
                        quant_issue_count <= quant_issue_count + 1'b1;
                    end

                    case ({
                        p_req_fire, p_rsp_fire
                    })
                        2'b10: begin
                            param_outstanding <= 1'b1;
                            param_outstanding_lane <= param_req_count[4:0];
                            param_req_count <= param_req_count + 1'b1;
                        end
                        2'b01: begin
                            param_outstanding <= 1'b0;
                        end
                        2'b11: begin
                            param_outstanding <= 1'b1;
                            param_outstanding_lane <= param_req_count[4:0];
                            param_req_count <= param_req_count + 1'b1;
                        end
                        default: begin
                            param_outstanding <= param_outstanding;
                        end
                    endcase
                end

                ST_EMIT: begin
                    if (output_fire) begin
                        if (batch_index == cfg_last_batch) begin
                            if (result_tag[38]) begin
                                // TAG64 frame_end is asserted only on the final
                                // pixel/final beat of the cfg operation.
                                done_reg <= 1'b1;
                                state <= ST_IDLE;
                            end else begin
                                // Same cfg, next output pixel.  Weight words are
                                // requested again from the external owner.
                                batch_index <= 4'd0;
                                active_lane_count <= (cfg_cin >= 9'd32) ?
                                                     6'd32 : cfg_cin[5:0];
                                expected_lane_mask <= (cfg_cin >= 9'd32) ?
                                                      32'hffffffff :
                                                      make_lane_mask(cfg_cin[5:0]);
                                tap_index <= 4'd0;
                                weight_word <= 256'd0;
                                weight_buf_valid <= 1'b0;
                                weight_buf_tap <= 4'd0;
                                weight_req_count <= 4'd0;
                                weight_outstanding <= 1'b0;
                                weight_outstanding_tap <= 4'd0;
                                result_word <= 256'd0;
                                result_mask <= 32'd0;
                                result_tag <= 64'd0;
                                state <= ST_W_REQ;
                            end
                        end else begin
                            batch_index <= batch_index + 1'b1;
                            if (next_batch_index == cfg_last_batch) begin
                                active_lane_count <= tail_lane_count;
                                expected_lane_mask <= tail_lane_mask;
                            end else begin
                                active_lane_count <= 6'd32;
                                expected_lane_mask <= 32'hffffffff;
                            end
                            tap_index <= 4'd0;
                            weight_word <= 256'd0;
                            weight_buf_valid <= 1'b0;
                            weight_buf_tap <= 4'd0;
                            weight_req_count <= 4'd0;
                            weight_outstanding <= 1'b0;
                            weight_outstanding_tap <= 4'd0;
                            result_word <= 256'd0;
                            result_mask <= 32'd0;
                            result_tag <= 64'd0;
                            state <= ST_W_REQ;
                        end
                    end
                end

                ST_FAULT: begin
                    state <= ST_FAULT;
                end

                default: begin
                    fault_reg <= 1'b1;
                    state <= ST_FAULT;
                end
            endcase
        end
    end

endmodule
