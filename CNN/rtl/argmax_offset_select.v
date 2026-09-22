`timescale 1ns / 1ps

// CNN-v4.0 | Verilog-2001 skeleton | technical contract unchanged
module argmax_offset_select (
    input wire clk,
    input wire rst_n,
    input wire cfg_valid,
    output wire cfg_ready,
    input wire [255:0] cfg_desc,
    output wire done,
    output wire fault,
    input wire [63:0] s_head_data,
    input wire s_head_valid,
    output wire s_head_ready,
    input wire [3:0] s_head_mask,
    input wire [63:0] s_head_tag,
    output wire [63:0] m_result_data,
    output wire m_result_valid,
    input wire m_result_ready,
    output wire m_result_last,
    output wire [4:0] m_result_joint
);

// Fixed common parameters; do not change the external port widths.
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

// cfg mode encoding from CNN-v4.0 descriptor contract.
localparam [1:0] MODE_BODY    = 2'd0;
localparam [1:0] MODE_HEATMAP = 2'd1;
localparam [1:0] MODE_OFFSET  = 2'd2;

// Internal state.  Encoding is local and does not affect the public contract.
localparam [2:0] ST_IDLE          = 3'd0;
localparam [2:0] ST_HEAT_SCAN     = 3'd1;
localparam [2:0] ST_OFFSET_SCAN   = 3'd2;
localparam [2:0] ST_OFFSET_VERIFY = 3'd3;
localparam [2:0] ST_OUTPUT        = 3'd4;
localparam [2:0] ST_FAULT         = 3'd5;

reg [2:0] state;
reg done_reg;
reg fault_reg;

// cfg is latched on cfg_valid && cfg_ready and held through the operation.
reg [255:0] cfg_desc_reg;
reg [1:0] cfg_mode_reg;
reg [4:0] cfg_op_id_reg;

// HWC row -> col -> output-group scan position.
reg [3:0] exp_row;
reg [3:0] exp_col;
reg [3:0] exp_group;

// Heatmap state: 17 joints, no full head tensor storage.
reg [7:0] best_score [0:16];
reg [3:0] best_row [0:16];
reg [3:0] best_col [0:16];
reg seen [0:16];

// Offset state: only the chosen best-position offsets are kept.
reg [15:0] offset_x [0:16];
reg [15:0] offset_y [0:16];
reg got_x [0:16];
reg got_y [0:16];

reg heatmap_complete;
reg [4:0] out_joint;

integer i;
integer j;

// Four signed16 lanes.  In heatmap mode, each lane is sign-extended INT8;
// in offset mode, the full signed16 lane is used.
wire signed [15:0] lane0_s16 = s_head_data[15:0];
wire signed [15:0] lane1_s16 = s_head_data[31:16];
wire signed [15:0] lane2_s16 = s_head_data[47:32];
wire signed [15:0] lane3_s16 = s_head_data[63:48];

wire signed [7:0] lane0_s8 = s_head_data[7:0];
wire signed [7:0] lane1_s8 = s_head_data[23:16];
wire signed [7:0] lane2_s8 = s_head_data[39:32];
wire signed [7:0] lane3_s8 = s_head_data[55:48];

// Channel c = 4*group + lane.  Shift/concatenate avoids a multiplier/DSP.
wire [5:0] ch0 = {exp_group, 2'b00};
wire [5:0] ch1 = ch0 + 6'd1;
wire [5:0] ch2 = ch0 + 6'd2;
wire [5:0] ch3 = ch0 + 6'd3;

wire scanning_heat = (state == ST_HEAT_SCAN);
wire scanning_offset = (state == ST_OFFSET_SCAN);
wire expected_group_last = scanning_heat ? (exp_group == 4'd4) :
                           scanning_offset ? (exp_group == 4'd8) : 1'b0;
wire expected_input_last = (exp_row == 4'd15) &&
                           (exp_col == 4'd15) && expected_group_last;

wire [3:0] expected_mask = scanning_heat ?
                           ((exp_group == 4'd4) ? 4'b0001 : 4'b1111) :
                           scanning_offset ?
                           ((exp_group == 4'd8) ? 4'b0011 : 4'b1111) :
                           4'b0000;

// Common TAG64 checks used by this receiver.
// TAG64: col[7:0], row[15:8], batch[19:16], group[26:20],
// tap[30:27], pixel_last[32], op_id[37:33], frame_end[38],
// group_last[39], reserved[63:40].
wire tag_position_ok =
    (s_head_tag[7:0]   == {4'b0000, exp_col}) &&
    (s_head_tag[15:8]  == {4'b0000, exp_row}) &&
    (s_head_tag[19:16] == 4'd0) &&
    (s_head_tag[26:20] == {3'b000, exp_group}) &&
    (s_head_tag[37:33] == cfg_op_id_reg);

wire tag_control_ok =
    (s_head_tag[32]    == expected_group_last) &&
    (s_head_tag[38]    == expected_input_last) &&
    (s_head_tag[39]    == expected_group_last) &&
    (s_head_tag[63:40] == 24'd0);

// pointwise head contract says heatmap INT8 is sign-extended to signed16.
// Masked-off dummy lanes are intentionally ignored.
wire heat_signext_ok =
    (!s_head_mask[0] || (s_head_data[15:8]  == {8{s_head_data[7]}})) &&
    (!s_head_mask[1] || (s_head_data[31:24] == {8{s_head_data[23]}})) &&
    (!s_head_mask[2] || (s_head_data[47:40] == {8{s_head_data[39]}})) &&
    (!s_head_mask[3] || (s_head_data[63:56] == {8{s_head_data[55]}}));

wire beat_protocol_ok =
    ((scanning_heat || scanning_offset) &&
     (s_head_mask == expected_mask) &&
     tag_position_ok &&
     tag_control_ok &&
     (!scanning_heat || heat_signext_ok));

wire head_accept = s_head_valid && s_head_ready;
wire result_accept = m_result_valid && m_result_ready;

// Every joint must receive both y and x offsets before result emission.
reg all_offsets_captured;
always @* begin
    all_offsets_captured = 1'b1;
    for (j = 0; j < 17; j = j + 1) begin
        if (!got_x[j] || !got_y[j])
            all_offsets_captured = 1'b0;
    end
end

// Public handshakes.
assign cfg_ready = rst_n && !fault_reg && (state == ST_IDLE);
assign s_head_ready = rst_n && !fault_reg &&
                      ((state == ST_HEAT_SCAN) || (state == ST_OFFSET_SCAN));
assign done = done_reg;
assign fault = fault_reg;

assign m_result_valid = rst_n && !fault_reg && (state == ST_OUTPUT);
assign m_result_joint = m_result_valid ? out_joint : 5'd0;
assign m_result_last = m_result_valid && (out_joint == 5'd16);

// result_data64 = col[7:0], row[15:8], x[31:16], y[47:32],
//                 score[55:48], reserved[63:56].
wire [63:0] result_word = {
    8'h00,
    best_score[out_joint],
    offset_y[out_joint],
    offset_x[out_joint],
    4'h0, best_row[out_joint],
    4'h0, best_col[out_joint]
};
assign m_result_data = m_result_valid ? result_word : 64'd0;

always @(posedge clk) begin
    if (!rst_n) begin
        state <= ST_IDLE;
        done_reg <= 1'b0;
        fault_reg <= 1'b0;
        cfg_desc_reg <= 256'd0;
        cfg_mode_reg <= MODE_BODY;
        cfg_op_id_reg <= 5'd0;
        exp_row <= 4'd0;
        exp_col <= 4'd0;
        exp_group <= 4'd0;
        heatmap_complete <= 1'b0;
        out_joint <= 5'd0;
        // Array contents are intentionally not reset here.  Heatmap cfg
        // initializes best/seen/got state before any valid operation uses them.
    end else begin
        // done is a one-cycle pulse.  Relevant completion logic below sets it.
        done_reg <= 1'b0;

        case (state)
            ST_IDLE: begin
                if (cfg_valid && cfg_ready) begin
                    cfg_desc_reg <= cfg_desc;
                    cfg_mode_reg <= cfg_desc[CFG_MODE_LSB +: CFG_MODE_WIDTH];
                    cfg_op_id_reg <= cfg_desc[CFG_OP_ID_LSB +: CFG_OP_ID_WIDTH];
                    exp_row <= 4'd0;
                    exp_col <= 4'd0;
                    exp_group <= 4'd0;
                    out_joint <= 5'd0;

                    // Only fields owned/used by this module are validated here.
                    if ((cfg_desc[CFG_MODE_LSB +: CFG_MODE_WIDTH] == MODE_HEATMAP) &&
                        (cfg_desc[CFG_HOUT_LSB +: CFG_HOUT_WIDTH] == 9'd16) &&
                        (cfg_desc[CFG_WOUT_LSB +: CFG_WOUT_WIDTH] == 9'd16) &&
                        (cfg_desc[CFG_COUT_LSB +: CFG_COUT_WIDTH] == 9'd17)) begin

                        // A new heatmap starts a new head pair.
                        heatmap_complete <= 1'b0;
                        for (i = 0; i < 17; i = i + 1) begin
                            best_score[i] <= 8'h80; // -128 bits; seen controls first capture
                            best_row[i] <= 4'd0;
                            best_col[i] <= 4'd0;
                            seen[i] <= 1'b0;
                            offset_x[i] <= 16'd0;
                            offset_y[i] <= 16'd0;
                            got_x[i] <= 1'b0;
                            got_y[i] <= 1'b0;
                        end
                        state <= ST_HEAT_SCAN;
                    end else if ((cfg_desc[CFG_MODE_LSB +: CFG_MODE_WIDTH] == MODE_OFFSET) &&
                                 (cfg_desc[CFG_HOUT_LSB +: CFG_HOUT_WIDTH] == 9'd16) &&
                                 (cfg_desc[CFG_WOUT_LSB +: CFG_WOUT_WIDTH] == 9'd16) &&
                                 (cfg_desc[CFG_COUT_LSB +: CFG_COUT_WIDTH] == 9'd34)) begin

                        // Offset may only consume a completed heatmap of this frame.
                        if (!heatmap_complete) begin
                            fault_reg <= 1'b1;
                            state <= ST_FAULT;
                        end else begin
                            // Preserve heatmap best arrays; only offset capture flags reset.
                            for (i = 0; i < 17; i = i + 1) begin
                                got_x[i] <= 1'b0;
                                got_y[i] <= 1'b0;
                            end
                            state <= ST_OFFSET_SCAN;
                        end
                    end else begin
                        // Body mode or malformed head geometry is not a legal argmax op.
                        fault_reg <= 1'b1;
                        state <= ST_FAULT;
                    end
                end
            end

            ST_HEAT_SCAN: begin
                if (head_accept) begin
                    if (!beat_protocol_ok) begin
                        fault_reg <= 1'b1;
                        state <= ST_FAULT;
                    end else begin
                        // Strict greater only: equal scores keep the first row-major hit.
                        if (s_head_mask[0] && (ch0 < 6'd17)) begin
                            if (!seen[ch0] || ($signed(lane0_s8) > $signed(best_score[ch0]))) begin
                                best_score[ch0] <= lane0_s8;
                                best_row[ch0] <= exp_row;
                                best_col[ch0] <= exp_col;
                            end
                            seen[ch0] <= 1'b1;
                        end
                        if (s_head_mask[1] && (ch1 < 6'd17)) begin
                            if (!seen[ch1] || ($signed(lane1_s8) > $signed(best_score[ch1]))) begin
                                best_score[ch1] <= lane1_s8;
                                best_row[ch1] <= exp_row;
                                best_col[ch1] <= exp_col;
                            end
                            seen[ch1] <= 1'b1;
                        end
                        if (s_head_mask[2] && (ch2 < 6'd17)) begin
                            if (!seen[ch2] || ($signed(lane2_s8) > $signed(best_score[ch2]))) begin
                                best_score[ch2] <= lane2_s8;
                                best_row[ch2] <= exp_row;
                                best_col[ch2] <= exp_col;
                            end
                            seen[ch2] <= 1'b1;
                        end
                        if (s_head_mask[3] && (ch3 < 6'd17)) begin
                            if (!seen[ch3] || ($signed(lane3_s8) > $signed(best_score[ch3]))) begin
                                best_score[ch3] <= lane3_s8;
                                best_row[ch3] <= exp_row;
                                best_col[ch3] <= exp_col;
                            end
                            seen[ch3] <= 1'b1;
                        end

                        // Counter advances only on an accepted, protocol-valid beat.
                        if (expected_group_last) begin
                            exp_group <= 4'd0;
                            if (exp_col == 4'd15) begin
                                exp_col <= 4'd0;
                                if (exp_row == 4'd15)
                                    exp_row <= 4'd0;
                                else
                                    exp_row <= exp_row + 4'd1;
                            end else begin
                                exp_col <= exp_col + 4'd1;
                            end
                        end else begin
                            exp_group <= exp_group + 4'd1;
                        end

                        // ARG-01 resolved/frozen: heatmap completion is the final
                        // heatmap input accept, not a result output accept.
                        if (expected_input_last) begin
                            heatmap_complete <= 1'b1;
                            done_reg <= 1'b1;
                            state <= ST_IDLE;
                        end
                    end
                end
            end

            ST_OFFSET_SCAN: begin
                if (head_accept) begin
                    if (!beat_protocol_ok) begin
                        fault_reg <= 1'b1;
                        state <= ST_FAULT;
                    end else begin
                        // c<17 -> yoffset[joint=c]
                        // 17<=c<34 -> xoffset[joint=c-17]
                        if (s_head_mask[0]) begin
                            if (ch0 < 6'd17) begin
                                if ((exp_row == best_row[ch0]) && (exp_col == best_col[ch0])) begin
                                    offset_y[ch0] <= lane0_s16;
                                    got_y[ch0] <= 1'b1;
                                end
                            end else if (ch0 < 6'd34) begin
                                if ((exp_row == best_row[ch0-6'd17]) &&
                                    (exp_col == best_col[ch0-6'd17])) begin
                                    offset_x[ch0-6'd17] <= lane0_s16;
                                    got_x[ch0-6'd17] <= 1'b1;
                                end
                            end
                        end
                        if (s_head_mask[1]) begin
                            if (ch1 < 6'd17) begin
                                if ((exp_row == best_row[ch1]) && (exp_col == best_col[ch1])) begin
                                    offset_y[ch1] <= lane1_s16;
                                    got_y[ch1] <= 1'b1;
                                end
                            end else if (ch1 < 6'd34) begin
                                if ((exp_row == best_row[ch1-6'd17]) &&
                                    (exp_col == best_col[ch1-6'd17])) begin
                                    offset_x[ch1-6'd17] <= lane1_s16;
                                    got_x[ch1-6'd17] <= 1'b1;
                                end
                            end
                        end
                        if (s_head_mask[2]) begin
                            if (ch2 < 6'd17) begin
                                if ((exp_row == best_row[ch2]) && (exp_col == best_col[ch2])) begin
                                    offset_y[ch2] <= lane2_s16;
                                    got_y[ch2] <= 1'b1;
                                end
                            end else if (ch2 < 6'd34) begin
                                if ((exp_row == best_row[ch2-6'd17]) &&
                                    (exp_col == best_col[ch2-6'd17])) begin
                                    offset_x[ch2-6'd17] <= lane2_s16;
                                    got_x[ch2-6'd17] <= 1'b1;
                                end
                            end
                        end
                        if (s_head_mask[3]) begin
                            if (ch3 < 6'd17) begin
                                if ((exp_row == best_row[ch3]) && (exp_col == best_col[ch3])) begin
                                    offset_y[ch3] <= lane3_s16;
                                    got_y[ch3] <= 1'b1;
                                end
                            end else if (ch3 < 6'd34) begin
                                if ((exp_row == best_row[ch3-6'd17]) &&
                                    (exp_col == best_col[ch3-6'd17])) begin
                                    offset_x[ch3-6'd17] <= lane3_s16;
                                    got_x[ch3-6'd17] <= 1'b1;
                                end
                            end
                        end

                        if (expected_group_last) begin
                            exp_group <= 4'd0;
                            if (exp_col == 4'd15) begin
                                exp_col <= 4'd0;
                                if (exp_row == 4'd15)
                                    exp_row <= 4'd0;
                                else
                                    exp_row <= exp_row + 4'd1;
                            end else begin
                                exp_col <= exp_col + 4'd1;
                            end
                        end else begin
                            exp_group <= exp_group + 4'd1;
                        end

                        // Verify got_x/got_y on the following cycle so captures
                        // performed by this same final beat are included.
                        if (expected_input_last)
                            state <= ST_OFFSET_VERIFY;
                    end
                end
            end

            ST_OFFSET_VERIFY: begin
                if (!all_offsets_captured) begin
                    fault_reg <= 1'b1;
                    state <= ST_FAULT;
                end else begin
                    out_joint <= 5'd0;
                    state <= ST_OUTPUT;
                end
            end

            ST_OUTPUT: begin
                // Arrays and out_joint are unchanged during valid && !ready,
                // therefore data/joint/last remain stable under backpressure.
                if (result_accept) begin
                    if (out_joint == 5'd16) begin
                        // Offset-mode completion is joint16 result downstream accept.
                        done_reg <= 1'b1;
                        heatmap_complete <= 1'b0;
                        state <= ST_IDLE;
                    end else begin
                        out_joint <= out_joint + 5'd1;
                    end
                end
            end

            ST_FAULT: begin
                // Sticky until synchronous rst_n reset.  All ready/valid outputs
                // that could start/advance work are blocked combinationally.
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
