`timescale 1ns / 1ps

// CNN-v4.0 | color_marker_detect | Verilog-2001
//
// Local implementation freeze:
//   tap_data = {B, G, R}; R is the least-significant byte.
//   A single unsigned restoring divider is shared in this order:
//     red_x, red_y, blue_x, blue_y, green_x, green_y.
//   Each launched division occupies exactly 30 clocks including launch and
//   result registration (28 restoring iterations plus load/output clocks).
//
// PROVISIONAL POLICY -- pending central fault-taxonomy review:
//   Only unambiguous local protocol/range violations are checked here:
//     * tap_last without tap_accept
//     * frame_start while a frame/result operation is active
//     * accepted tap_row > 715 or tap_col > 1279
//   Full raster order, duplicate, missing-pixel, row-step, and exact tap-count
//   checks remain producer/integration responsibilities.
//
// Resource intent: DSP = 0, BRAM = 0.  No /, %, or multiply operator is used.

module color_marker_detect (
    input wire clk,
    input wire rst_n,
    output wire fault,
    input wire [23:0] tap_data,
    input wire [9:0] tap_row,
    input wire [10:0] tap_col,
    input wire tap_accept,
    input wire tap_last,
    input wire frame_start,
    input wire [23:0] red_cfg,
    input wire [23:0] blue_cfg,
    input wire [23:0] green_cfg,
    input wire [23:0] margin_cfg,
    input wire [2:0] color_enable,
    input wire [17:0] min_count,
    output wire [31:0] red_word,
    output wire [31:0] blue_word,
    output wire [31:0] green_word,
    output wire results_valid
);

localparam [2:0] S_IDLE     = 3'd0;
localparam [2:0] S_ACCUM    = 3'd1;
localparam [2:0] S_SCHEDULE = 3'd2;
localparam [2:0] S_DIVIDE   = 3'd3;
localparam [2:0] S_COMPLETE = 3'd4;
localparam [2:0] S_FAULT    = 3'd5;

localparam [2:0] DIV_RED_X   = 3'd0;
localparam [2:0] DIV_RED_Y   = 3'd1;
localparam [2:0] DIV_BLUE_X  = 3'd2;
localparam [2:0] DIV_BLUE_Y  = 3'd3;
localparam [2:0] DIV_GREEN_X = 3'd4;
localparam [2:0] DIV_GREEN_Y = 3'd5;

reg [2:0] state;
reg fault_reg;
reg results_valid_reg;
reg [31:0] red_word_reg;
reg [31:0] blue_word_reg;
reg [31:0] green_word_reg;

reg [23:0] red_cfg_reg;
reg [23:0] blue_cfg_reg;
reg [23:0] green_cfg_reg;
reg [23:0] margin_cfg_reg;
reg [2:0] color_enable_reg;
reg [17:0] min_count_reg;

(* use_dsp = "no" *) reg [17:0] red_count;
(* use_dsp = "no" *) reg [17:0] blue_count;
(* use_dsp = "no" *) reg [17:0] green_count;
(* use_dsp = "no" *) reg [27:0] red_sum_x;
(* use_dsp = "no" *) reg [27:0] red_sum_y;
(* use_dsp = "no" *) reg [27:0] blue_sum_x;
(* use_dsp = "no" *) reg [27:0] blue_sum_y;
(* use_dsp = "no" *) reg [27:0] green_sum_x;
(* use_dsp = "no" *) reg [27:0] green_sum_y;

reg [17:0] final_red_count;
reg [17:0] final_blue_count;
reg [17:0] final_green_count;
reg [27:0] final_red_sum_x;
reg [27:0] final_red_sum_y;
reg [27:0] final_blue_sum_x;
reg [27:0] final_blue_sum_y;
reg [27:0] final_green_sum_x;
reg [27:0] final_green_sum_y;

reg red_found_work;
reg blue_found_work;
reg green_found_work;
reg [10:0] red_x_work;
reg [9:0] red_y_work;
reg [10:0] blue_x_work;
reg [9:0] blue_y_work;
reg [10:0] green_x_work;
reg [9:0] green_y_work;

// Shared unsigned restoring divider state.
reg [2:0] div_target;
reg div_active;
reg [5:0] div_cycle;
(* use_dsp = "no" *) reg [27:0] div_dividend;
(* use_dsp = "no" *) reg [17:0] div_denominator;
(* use_dsp = "no" *) reg [18:0] div_remainder;
(* use_dsp = "no" *) reg [27:0] div_quotient;

// Internal verification events. They are intentionally not public ports.
reg div_launch_pulse;
reg div_done_pulse;

wire [7:0] tap_r;
wire [7:0] tap_g;
wire [7:0] tap_b;
assign tap_r = tap_data[7:0];
assign tap_g = tap_data[15:8];
assign tap_b = tap_data[23:16];

// Channel-dominance filter adapted from fpga-vga-conductor-game.
// margin_cfg packs {blue_margin, green_margin, red_margin}.  Nine-bit sums
// avoid unsigned wrap when the competing channel plus margin exceeds 255.
wire red_hit;
wire blue_hit;
wire green_hit;
assign red_hit = color_enable_reg[0] &&
    (tap_r >= red_cfg_reg[7:0]) &&
    ({1'b0,tap_r} >= ({1'b0,tap_g} + {1'b0,margin_cfg_reg[7:0]})) &&
    ({1'b0,tap_r} >= ({1'b0,tap_b} + {1'b0,margin_cfg_reg[7:0]}));
assign blue_hit = color_enable_reg[1] &&
    (tap_b >= blue_cfg_reg[23:16]) &&
    ({1'b0,tap_b} >= ({1'b0,tap_r} + {1'b0,margin_cfg_reg[23:16]})) &&
    ({1'b0,tap_b} >= ({1'b0,tap_g} + {1'b0,margin_cfg_reg[23:16]}));
assign green_hit = color_enable_reg[2] &&
    (tap_g >= green_cfg_reg[15:8]) &&
    ({1'b0,tap_g} >= ({1'b0,tap_r} + {1'b0,margin_cfg_reg[15:8]})) &&
    ({1'b0,tap_g} >= ({1'b0,tap_b} + {1'b0,margin_cfg_reg[15:8]}));

// These expressions deliberately include the current accepted tap.  They are
// used when tap_last is asserted so the final pixel reaches the divider.
wire [17:0] red_count_with_tap;
wire [17:0] blue_count_with_tap;
wire [17:0] green_count_with_tap;
wire [27:0] red_sum_x_with_tap;
wire [27:0] red_sum_y_with_tap;
wire [27:0] blue_sum_x_with_tap;
wire [27:0] blue_sum_y_with_tap;
wire [27:0] green_sum_x_with_tap;
wire [27:0] green_sum_y_with_tap;

assign red_count_with_tap = red_count + (red_hit ? 18'd1 : 18'd0);
assign blue_count_with_tap = blue_count + (blue_hit ? 18'd1 : 18'd0);
assign green_count_with_tap = green_count + (green_hit ? 18'd1 : 18'd0);
assign red_sum_x_with_tap = red_sum_x +
    (red_hit ? {{17{1'b0}}, tap_col} : 28'd0);
assign red_sum_y_with_tap = red_sum_y +
    (red_hit ? {{18{1'b0}}, tap_row} : 28'd0);
assign blue_sum_x_with_tap = blue_sum_x +
    (blue_hit ? {{17{1'b0}}, tap_col} : 28'd0);
assign blue_sum_y_with_tap = blue_sum_y +
    (blue_hit ? {{18{1'b0}}, tap_row} : 28'd0);
assign green_sum_x_with_tap = green_sum_x +
    (green_hit ? {{17{1'b0}}, tap_col} : 28'd0);
assign green_sum_y_with_tap = green_sum_y +
    (green_hit ? {{18{1'b0}}, tap_row} : 28'd0);

wire [18:0] div_shifted_remainder;
wire [18:0] div_denominator_ext;
wire div_step_ge;
wire [18:0] div_step_remainder;

assign div_shifted_remainder = {div_remainder[17:0], div_dividend[27]};
assign div_denominator_ext = {1'b0, div_denominator};
assign div_step_ge = div_shifted_remainder >= div_denominator_ext;
assign div_step_remainder = div_step_ge ?
    (div_shifted_remainder - div_denominator_ext) :
    div_shifted_remainder;

wire tap_protocol_fault;
wire tap_range_fault;
wire new_frame_fault;
assign tap_protocol_fault = tap_last && !tap_accept;
assign tap_range_fault =
    (state == S_ACCUM) && tap_accept &&
    ((tap_row > 10'd715) || (tap_col > 11'd1279));
assign new_frame_fault = frame_start && (state != S_IDLE);

wire target_has_nonzero_count;
assign target_has_nonzero_count =
    ((div_target == DIV_RED_X) || (div_target == DIV_RED_Y)) ?
        (final_red_count != 18'd0) :
    ((div_target == DIV_BLUE_X) || (div_target == DIV_BLUE_Y)) ?
        (final_blue_count != 18'd0) :
        (final_green_count != 18'd0);

wire [27:0] scheduled_numerator;
wire [17:0] scheduled_denominator;
assign scheduled_numerator =
    (div_target == DIV_RED_X)   ? final_red_sum_x :
    (div_target == DIV_RED_Y)   ? final_red_sum_y :
    (div_target == DIV_BLUE_X)  ? final_blue_sum_x :
    (div_target == DIV_BLUE_Y)  ? final_blue_sum_y :
    (div_target == DIV_GREEN_X) ? final_green_sum_x :
                                  final_green_sum_y;
assign scheduled_denominator =
    ((div_target == DIV_RED_X) || (div_target == DIV_RED_Y)) ?
        final_red_count :
    ((div_target == DIV_BLUE_X) || (div_target == DIV_BLUE_Y)) ?
        final_blue_count : final_green_count;

assign fault = fault_reg;
assign red_word = red_word_reg;
assign blue_word = blue_word_reg;
assign green_word = green_word_reg;
assign results_valid = results_valid_reg;

always @(posedge clk) begin
    if (!rst_n) begin
        state <= S_IDLE;
        fault_reg <= 1'b0;
        results_valid_reg <= 1'b0;
        red_word_reg <= 32'd0;
        blue_word_reg <= 32'd0;
        green_word_reg <= 32'd0;

        red_cfg_reg <= 24'h6464A0;
        blue_cfg_reg <= 24'hA06464;
        green_cfg_reg <= 24'h406040;
        margin_cfg_reg <= 24'h402010;
        color_enable_reg <= 3'b111;
        min_count_reg <= 18'd8;

        red_count <= 18'd0;
        blue_count <= 18'd0;
        green_count <= 18'd0;
        red_sum_x <= 28'd0;
        red_sum_y <= 28'd0;
        blue_sum_x <= 28'd0;
        blue_sum_y <= 28'd0;
        green_sum_x <= 28'd0;
        green_sum_y <= 28'd0;

        final_red_count <= 18'd0;
        final_blue_count <= 18'd0;
        final_green_count <= 18'd0;
        final_red_sum_x <= 28'd0;
        final_red_sum_y <= 28'd0;
        final_blue_sum_x <= 28'd0;
        final_blue_sum_y <= 28'd0;
        final_green_sum_x <= 28'd0;
        final_green_sum_y <= 28'd0;

        red_found_work <= 1'b0;
        blue_found_work <= 1'b0;
        green_found_work <= 1'b0;
        red_x_work <= 11'd0;
        red_y_work <= 10'd0;
        blue_x_work <= 11'd0;
        blue_y_work <= 10'd0;
        green_x_work <= 11'd0;
        green_y_work <= 10'd0;

        div_target <= DIV_RED_X;
        div_active <= 1'b0;
        div_cycle <= 6'd0;
        div_dividend <= 28'd0;
        div_denominator <= 18'd0;
        div_remainder <= 19'd0;
        div_quotient <= 28'd0;
        div_launch_pulse <= 1'b0;
        div_done_pulse <= 1'b0;
    end else begin
        results_valid_reg <= 1'b0;
        div_launch_pulse <= 1'b0;
        div_done_pulse <= 1'b0;

        if (fault_reg) begin
            state <= S_FAULT;
            div_active <= 1'b0;
        end else if (tap_protocol_fault || tap_range_fault ||
                     new_frame_fault) begin
            fault_reg <= 1'b1;
            state <= S_FAULT;
            div_active <= 1'b0;
        end else begin
            case (state)
                S_IDLE: begin
                    div_active <= 1'b0;
                    if (frame_start) begin
                        red_cfg_reg <= red_cfg;
                        blue_cfg_reg <= blue_cfg;
                        green_cfg_reg <= green_cfg;
                        margin_cfg_reg <= margin_cfg;
                        color_enable_reg <= color_enable;
                        min_count_reg <= min_count;

                        red_count <= 18'd0;
                        blue_count <= 18'd0;
                        green_count <= 18'd0;
                        red_sum_x <= 28'd0;
                        red_sum_y <= 28'd0;
                        blue_sum_x <= 28'd0;
                        blue_sum_y <= 28'd0;
                        green_sum_x <= 28'd0;
                        green_sum_y <= 28'd0;

                        final_red_count <= 18'd0;
                        final_blue_count <= 18'd0;
                        final_green_count <= 18'd0;
                        final_red_sum_x <= 28'd0;
                        final_red_sum_y <= 28'd0;
                        final_blue_sum_x <= 28'd0;
                        final_blue_sum_y <= 28'd0;
                        final_green_sum_x <= 28'd0;
                        final_green_sum_y <= 28'd0;

                        red_found_work <= 1'b0;
                        blue_found_work <= 1'b0;
                        green_found_work <= 1'b0;
                        red_x_work <= 11'd0;
                        red_y_work <= 10'd0;
                        blue_x_work <= 11'd0;
                        blue_y_work <= 10'd0;
                        green_x_work <= 11'd0;
                        green_y_work <= 10'd0;
                        div_target <= DIV_RED_X;
                        state <= S_ACCUM;
                    end
                end

                S_ACCUM: begin
                    if (tap_accept) begin
                        red_count <= red_count_with_tap;
                        blue_count <= blue_count_with_tap;
                        green_count <= green_count_with_tap;
                        red_sum_x <= red_sum_x_with_tap;
                        red_sum_y <= red_sum_y_with_tap;
                        blue_sum_x <= blue_sum_x_with_tap;
                        blue_sum_y <= blue_sum_y_with_tap;
                        green_sum_x <= green_sum_x_with_tap;
                        green_sum_y <= green_sum_y_with_tap;

                        if (tap_last) begin
                            final_red_count <= red_count_with_tap;
                            final_blue_count <= blue_count_with_tap;
                            final_green_count <= green_count_with_tap;
                            final_red_sum_x <= red_sum_x_with_tap;
                            final_red_sum_y <= red_sum_y_with_tap;
                            final_blue_sum_x <= blue_sum_x_with_tap;
                            final_blue_sum_y <= blue_sum_y_with_tap;
                            final_green_sum_x <= green_sum_x_with_tap;
                            final_green_sum_y <= green_sum_y_with_tap;
                            red_found_work <=
                                red_count_with_tap >= min_count_reg;
                            blue_found_work <=
                                blue_count_with_tap >= min_count_reg;
                            green_found_work <=
                                green_count_with_tap >= min_count_reg;
                            div_target <= DIV_RED_X;
                            state <= S_SCHEDULE;
                        end
                    end
                end

                S_SCHEDULE: begin
                    if (target_has_nonzero_count) begin
                        div_dividend <= scheduled_numerator;
                        div_denominator <= scheduled_denominator;
                        div_remainder <= 19'd0;
                        div_quotient <= 28'd0;
                        div_cycle <= 6'd0;
                        div_active <= 1'b1;
                        div_launch_pulse <= 1'b1;
                        state <= S_DIVIDE;
                    end else if (div_target == DIV_GREEN_Y) begin
                        state <= S_COMPLETE;
                    end else begin
                        div_target <= div_target + 1'b1;
                    end
                end

                S_DIVIDE: begin
                    if (div_cycle < 6'd28) begin
                        div_remainder <= div_step_remainder;
                        div_dividend <= {div_dividend[26:0], 1'b0};
                        div_quotient <=
                            {div_quotient[26:0], div_step_ge};
                        div_cycle <= div_cycle + 1'b1;
                    end else begin
                        div_active <= 1'b0;
                        div_done_pulse <= 1'b1;
                        case (div_target)
                            DIV_RED_X:
                                red_x_work <= div_quotient[10:0];
                            DIV_RED_Y:
                                red_y_work <= div_quotient[9:0];
                            DIV_BLUE_X:
                                blue_x_work <= div_quotient[10:0];
                            DIV_BLUE_Y:
                                blue_y_work <= div_quotient[9:0];
                            DIV_GREEN_X:
                                green_x_work <= div_quotient[10:0];
                            default:
                                green_y_work <= div_quotient[9:0];
                        endcase

                        if (div_target == DIV_GREEN_Y) begin
                            state <= S_COMPLETE;
                        end else begin
                            div_target <= div_target + 1'b1;
                            state <= S_SCHEDULE;
                        end
                    end
                end

                S_COMPLETE: begin
                    if (red_found_work)
                        red_word_reg <=
                            {1'b1, 10'd0, red_y_work, red_x_work};
                    else
                        red_word_reg <= 32'd0;

                    if (blue_found_work)
                        blue_word_reg <=
                            {1'b1, 10'd0, blue_y_work, blue_x_work};
                    else
                        blue_word_reg <= 32'd0;

                    if (green_found_work)
                        green_word_reg <=
                            {1'b1, 10'd0, green_y_work, green_x_work};
                    else
                        green_word_reg <= 32'd0;

                    results_valid_reg <= 1'b1;
                    state <= S_IDLE;
                end

                default: begin
                    state <= S_FAULT;
                    fault_reg <= 1'b1;
                    div_active <= 1'b0;
                end
            endcase
        end
    end
end

endmodule
