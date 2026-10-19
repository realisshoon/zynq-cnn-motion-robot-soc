`timescale 1ns / 1ps

// CNN-v4.0 | coord_restore | Verilog-2001
//
// Canonical behavior:
//   X_Q16 = grid_col*80*65536 + offset_x*15480
//   Y_Q16 = (grid_row*80 - 280)*65536 + offset_y*15480
//   final Q16->integer conversion: signed RNE, ties-to-even
//   good = signed(score) >= signed(threshold)
//          && 0 <= x < 1280 && 0 <= y < 720
//   invalid => x=y=0, score is preserved
//
// Pipeline:
//   input accept -> S0 latch -> S1 constant multiply -> S2 base add
//                -> S3 RNE -> S4 bounds/pack/output
//   No-stall latency: 5 enabled stages, II=1.
//   If output is stalled, the whole pipeline is held.
//
// Resource intent:
//   DSP = 0, BRAM = 0.
//   Constant 15480 multiply is shift/add:
//     15480 = 2^14 - 2^10 + 2^7 - 2^3.
//
module coord_restore (
    input wire clk,
    input wire rst_n,

    input wire cfg_valid,
    output wire cfg_ready,
    input wire [255:0] cfg_desc,

    output wire done,
    output wire fault,

    input wire [63:0] s_result_data,
    input wire s_result_valid,
    output wire s_result_ready,
    input wire s_result_last,
    input wire [4:0] s_result_joint,

    output wire [31:0] m_joint_data,
    output wire m_joint_valid,
    input wire m_joint_ready,
    output wire m_joint_last,
    output wire [4:0] m_joint_index,
    output wire m_joint_good,

    input wire [7:0] threshold
);

// -----------------------------------------------------------------------------
// Fixed common parameters. Public widths are intentionally unchanged.
// -----------------------------------------------------------------------------
localparam integer CNN_W_IN = 32;
localparam integer CNN_W_OUT = 4;
localparam integer CNN_ACCUM_WIDTH = 24;
localparam integer CNN_REQUANT_IN_WIDTH = 25;
localparam integer CNN_M_WIDTH = 18;
localparam integer CNN_SHIFT = 16;
localparam integer CNN_OPS = 29;
localparam integer CNN_STAGES = 16;
localparam [31:0] CNN_VERSION = 32'h00040000;

localparam integer CFG_OP_ID_LSB = 0, CFG_OP_ID_WIDTH = 5;
localparam integer CFG_KIND_LSB = 5, CFG_KIND_WIDTH = 2;
localparam integer CFG_MODE_LSB = 7, CFG_MODE_WIDTH = 2;
localparam integer CFG_HIN_LSB = 9, CFG_HIN_WIDTH = 9;
localparam integer CFG_WIN_LSB = 18, CFG_WIN_WIDTH = 9;
localparam integer CFG_HOUT_LSB = 27, CFG_HOUT_WIDTH = 9;
localparam integer CFG_WOUT_LSB = 36, CFG_WOUT_WIDTH = 9;
localparam integer CFG_CIN_LSB = 45, CFG_CIN_WIDTH = 9;
localparam integer CFG_COUT_LSB = 54, CFG_COUT_WIDTH = 9;
localparam integer CFG_STRIDE_LSB = 63, CFG_STRIDE_WIDTH = 2;
localparam integer CFG_DILATION_LSB = 65, CFG_DILATION_WIDTH = 2;
localparam integer CFG_PAD_LSB = 67, CFG_PAD_WIDTH = 2;
localparam integer CFG_SHIFT_LSB = 69, CFG_SHIFT_WIDTH = 6;
localparam integer CFG_WEIGHT_OFFSET_LSB = 75, CFG_WEIGHT_OFFSET_WIDTH = 32;
localparam integer CFG_PARAM_OFFSET_LSB = 107, CFG_PARAM_OFFSET_WIDTH = 18;
localparam integer CFG_DMA_BYTES_LSB = 125, CFG_DMA_BYTES_WIDTH = 20;
localparam integer CFG_STAGE_ID_LSB = 145, CFG_STAGE_ID_WIDTH = 4;
localparam integer CFG_RESERVED_LSB = 149, CFG_RESERVED_WIDTH = 107;

// -----------------------------------------------------------------------------
// Constant helpers.
// -----------------------------------------------------------------------------

// signed16 * 15480 in signed40 without a multiplier.
// 15480 = 16384 - 1024 + 128 - 8.
function signed [39:0] mul_15480;
    input signed [15:0] value;
    reg signed [39:0] sx;
    begin
        sx = {{24{value[15]}}, value};
        mul_15480 =
            (sx <<< 14)
          - (sx <<< 10)
          + (sx <<< 7)
          - (sx <<< 3);
    end
endfunction

// grid_col * 80 * 65536, returned as signed40.
function signed [39:0] x_base_q16;
    input [7:0] col;
    reg signed [39:0] c;
    reg signed [39:0] c80;
    begin
        c = {32'd0, col};
        c80 = (c <<< 6) + (c <<< 4); // *80 = *64 + *16
        x_base_q16 = c80 <<< 16;
    end
endfunction

// (grid_row * 80 - 280) * 65536, returned as signed40.
function signed [39:0] y_base_q16;
    input [7:0] row;
    reg signed [39:0] r;
    reg signed [39:0] r80_minus_pad;
    begin
        r = {32'd0, row};
        r80_minus_pad = (r <<< 6) + (r <<< 4) - 40'sd280;
        y_base_q16 = r80_minus_pad <<< 16;
    end
endfunction

// Signed arithmetic Q16 right shift with round-to-nearest, ties-to-even.
// q is floor(p/2^16), and remainder is therefore in [0, 65535].
function signed [39:0] rne_q16;
    input signed [39:0] p;
    reg signed [39:0] q;
    reg signed [39:0] r;
    begin
        q = p >>> 16;
        r = p - (q <<< 16);

        if ((r > 40'sd32768) ||
            ((r == 40'sd32768) && q[0]))
            q = q + 40'sd1;

        rne_q16 = q;
    end
endfunction

// -----------------------------------------------------------------------------
// Configuration / operation control.
// -----------------------------------------------------------------------------
reg [255:0] cfg_desc_reg;
reg [7:0] threshold_reg;
reg active_reg;
reg fault_reg;
reg done_reg;

reg [4:0] expected_joint;
reg input_complete;

assign cfg_ready = rst_n && (~active_reg) && (~fault_reg);
assign done = done_reg;
assign fault = fault_reg;

// -----------------------------------------------------------------------------
// Five-stage pipeline state.
// -----------------------------------------------------------------------------

// S0: accepted argmax result.
reg v0;
reg [7:0]  s0_grid_col;
reg [7:0]  s0_grid_row;
reg signed [15:0] s0_offset_x;
reg signed [15:0] s0_offset_y;
reg [7:0]  s0_score;
reg [4:0]  s0_joint;
reg        s0_last;

// S1: constant offset multiplication.
reg v1;
(* use_dsp = "no" *) reg signed [39:0] s1_x_mul;
(* use_dsp = "no" *) reg signed [39:0] s1_y_mul;
reg [7:0]  s1_grid_col;
reg [7:0]  s1_grid_row;
reg [7:0]  s1_score;
reg [4:0]  s1_joint;
reg        s1_last;

// S2: base + offset Q16.
reg v2;
reg signed [39:0] s2_x_q16;
reg signed [39:0] s2_y_q16;
reg [7:0]  s2_score;
reg [4:0]  s2_joint;
reg        s2_last;

// S3: rounded integer coordinates.
reg v3;
reg signed [39:0] s3_x;
reg signed [39:0] s3_y;
reg [7:0]  s3_score;
reg [4:0]  s3_joint;
reg        s3_last;

// S4: final packed output.
reg v4;
reg [31:0] s4_data;
reg [4:0]  s4_joint;
reg        s4_last;
reg        s4_good;

// Entire pipeline freezes when the output is backpressured.
wire pipe_ce;
assign pipe_ce = (~v4) || m_joint_ready;

// Upstream can be accepted only during an active configured operation,
// before all 17 joints are accepted, and while the pipeline can advance.
assign s_result_ready =
    active_reg &&
    (~fault_reg) &&
    (~input_complete) &&
    pipe_ce;

wire input_accept;
assign input_accept = s_result_valid && s_result_ready;

// -----------------------------------------------------------------------------
// Input contract checks.
// -----------------------------------------------------------------------------
wire joint_mismatch;
wire last_mismatch;
wire grid_col_bad;
wire grid_row_bad;
wire reserved_bad;
wire input_protocol_error;

assign joint_mismatch = (s_result_joint != expected_joint);
assign last_mismatch =
    (s_result_last != (expected_joint == 5'd16));

// grid_col/grid_row are carried in 8-bit fields, but valid grid coordinates
// for the 16x16 head are only 0..15.
assign grid_col_bad = |s_result_data[7:4];
assign grid_row_bad = |s_result_data[15:12];

// result_data64[63:56] is reserved and must remain zero.
assign reserved_bad = |s_result_data[63:56];

assign input_protocol_error =
    joint_mismatch ||
    last_mismatch ||
    grid_col_bad ||
    grid_row_bad ||
    reserved_bad;

// -----------------------------------------------------------------------------
// S3 -> S4 bounds, threshold, and packing.
// -----------------------------------------------------------------------------
wire s3_score_ok;
wire s3_x_ok;
wire s3_y_ok;
wire s3_good_calc;
wire [11:0] s3_x_packed;
wire [11:0] s3_y_packed;
wire [31:0] s3_word_calc;

assign s3_score_ok =
    ($signed(s3_score) >= $signed(threshold_reg));

assign s3_x_ok =
    ($signed(s3_x) >= 40'sd0) &&
    ($signed(s3_x) < 40'sd1280);

assign s3_y_ok =
    ($signed(s3_y) >= 40'sd0) &&
    ($signed(s3_y) < 40'sd720);

assign s3_good_calc = s3_score_ok && s3_x_ok && s3_y_ok;

assign s3_x_packed = s3_good_calc ? s3_x[11:0] : 12'd0;
assign s3_y_packed = s3_good_calc ? s3_y[11:0] : 12'd0;

// score is always preserved, including invalid coordinates.
assign s3_word_calc = {
    s3_score,
    s3_y_packed,
    s3_x_packed
};

// Public output is the registered S4 payload.
assign m_joint_data  = s4_data;
assign m_joint_valid = v4;
assign m_joint_last  = s4_last;
assign m_joint_index = s4_joint;
assign m_joint_good  = s4_good;

// -----------------------------------------------------------------------------
// Sequential logic.
// Synchronous active-low reset: rst_n is sampled only on posedge clk.
// -----------------------------------------------------------------------------
always @(posedge clk) begin
    if (!rst_n) begin
        cfg_desc_reg <= 256'd0;
        threshold_reg <= 8'hD2; // -46 two's-complement default

        active_reg <= 1'b0;
        fault_reg <= 1'b0;
        done_reg <= 1'b0;

        expected_joint <= 5'd0;
        input_complete <= 1'b0;

        v0 <= 1'b0;
        s0_grid_col <= 8'd0;
        s0_grid_row <= 8'd0;
        s0_offset_x <= 16'sd0;
        s0_offset_y <= 16'sd0;
        s0_score <= 8'd0;
        s0_joint <= 5'd0;
        s0_last <= 1'b0;

        v1 <= 1'b0;
        s1_x_mul <= 40'sd0;
        s1_y_mul <= 40'sd0;
        s1_grid_col <= 8'd0;
        s1_grid_row <= 8'd0;
        s1_score <= 8'd0;
        s1_joint <= 5'd0;
        s1_last <= 1'b0;

        v2 <= 1'b0;
        s2_x_q16 <= 40'sd0;
        s2_y_q16 <= 40'sd0;
        s2_score <= 8'd0;
        s2_joint <= 5'd0;
        s2_last <= 1'b0;

        v3 <= 1'b0;
        s3_x <= 40'sd0;
        s3_y <= 40'sd0;
        s3_score <= 8'd0;
        s3_joint <= 5'd0;
        s3_last <= 1'b0;

        v4 <= 1'b0;
        s4_data <= 32'd0;
        s4_joint <= 5'd0;
        s4_last <= 1'b0;
        s4_good <= 1'b0;
    end
    else begin
        // done is a one-cycle pulse.
        done_reg <= 1'b0;

        // Sticky fault blocks all new work until reset.
        if (!fault_reg) begin

            // New operation configuration is accepted only while idle.
            if (cfg_valid && cfg_ready) begin
                cfg_desc_reg <= cfg_desc;

                // The top-level contract presents a frame-snapshotted threshold.
                // This module snapshots that value again at its cfg boundary so
                // all 17 joints in the operation use one signed8 threshold.
                threshold_reg <= threshold;

                active_reg <= 1'b1;
                expected_joint <= 5'd0;
                input_complete <= 1'b0;

                // Idle invariant says the pipeline is empty; explicitly clear
                // valid bits at the new operation boundary.
                v0 <= 1'b0;
                v1 <= 1'b0;
                v2 <= 1'b0;
                v3 <= 1'b0;
                v4 <= 1'b0;
            end

            // An accepted malformed beat is discarded and causes sticky fault.
            // Existing partial results are invalidated because the operation can
            // no longer produce a complete 17-joint packet.
            if (input_accept && input_protocol_error) begin
                fault_reg <= 1'b1;
                active_reg <= 1'b0;
                input_complete <= 1'b0;

                v0 <= 1'b0;
                v1 <= 1'b0;
                v2 <= 1'b0;
                v3 <= 1'b0;
                v4 <= 1'b0;
            end
            else begin
                // Final downstream accept is the operation completion event.
                if (m_joint_valid && m_joint_ready && m_joint_last) begin
                    done_reg <= 1'b1;
                    active_reg <= 1'b0;
                    input_complete <= 1'b0;
                    expected_joint <= 5'd0;
                end

                // Global pipeline enable.  If S4 is valid and the downstream
                // deasserts ready, every stage and every joint tag is held.
                if (pipe_ce) begin
                    // S4: bounds / signed threshold / final packing.
                    v4 <= v3;
                    if (v3) begin
                        s4_data <= s3_word_calc;
                        s4_joint <= s3_joint;
                        s4_last <= s3_last;
                        s4_good <= s3_good_calc;
                    end

                    // S3: final Q16 RNE.  No fractional bits are discarded
                    // before this stage.
                    v3 <= v2;
                    if (v2) begin
                        s3_x <= rne_q16(s2_x_q16);
                        s3_y <= rne_q16(s2_y_q16);
                        s3_score <= s2_score;
                        s3_joint <= s2_joint;
                        s3_last <= s2_last;
                    end

                    // S2: add spatial base and signed scaled offset.
                    v2 <= v1;
                    if (v1) begin
                        s2_x_q16 <=
                            x_base_q16(s1_grid_col) + s1_x_mul;
                        s2_y_q16 <=
                            y_base_q16(s1_grid_row) + s1_y_mul;
                        s2_score <= s1_score;
                        s2_joint <= s1_joint;
                        s2_last <= s1_last;
                    end

                    // S1: offset * 15480 using shift/add only.
                    v1 <= v0;
                    if (v0) begin
                        s1_x_mul <= mul_15480(s0_offset_x);
                        s1_y_mul <= mul_15480(s0_offset_y);
                        s1_grid_col <= s0_grid_col;
                        s1_grid_row <= s0_grid_row;
                        s1_score <= s0_score;
                        s1_joint <= s0_joint;
                        s1_last <= s0_last;
                    end

                    // S0 defaults empty on an enabled cycle; a new accepted
                    // input beat refills it below.
                    v0 <= 1'b0;

                    if (input_accept) begin
                        v0 <= 1'b1;

                        s0_grid_col <= s_result_data[7:0];
                        s0_grid_row <= s_result_data[15:8];
                        s0_offset_x <= $signed(s_result_data[31:16]);
                        s0_offset_y <= $signed(s_result_data[47:32]);
                        s0_score <= s_result_data[55:48];
                        s0_joint <= s_result_joint;
                        s0_last <= s_result_last;

                        if (expected_joint == 5'd16) begin
                            input_complete <= 1'b1;
                        end
                        else begin
                            expected_joint <= expected_joint + 5'd1;
                        end
                    end
                end
            end
        end
    end
end

endmodule
