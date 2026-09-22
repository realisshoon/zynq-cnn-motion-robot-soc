`timescale 1ns / 1ps

// CNN-v4.0 line_buffer. Design freeze: 2026-09-17.
// Verilog-2001; synchronous active-low control reset. Data RAM is not reset.
module line_buffer (
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
    output wire [255:0] m_tap_data,
    output wire m_tap_valid,
    input wire m_tap_ready,
    output wire [31:0] m_tap_mask,
    output wire [63:0] m_tap_tag
);
    localparam [2:0] IDLE = 3'd0, LOAD_ROWS = 3'd1,
                     TAP_STREAM = 3'd2, DRAIN_INPUT = 3'd3,
                     DONE = 3'd4, FAULT = 3'd5;
    reg [2:0] state;
    reg fault_reg;
    reg [255:0] cfg_latched;
    reg [4:0] cfg_op_id;
    reg [8:0] cfg_hin, cfg_win, cfg_hout, cfg_wout, cfg_cin;
    reg [1:0] cfg_stride;
    reg [3:0] num_batches, last_batch;
    reg [8:0] row_words;
    reg [19:0] expected_input_beats, accepted_input_beats;
    reg [7:0] in_row, in_col;
    reg [3:0] in_batch;
    reg [8:0] next_input_row;
    reg [1:0] write_slot;
    reg [2:0] slot_valid;
    reg [7:0] slot_row0, slot_row1, slot_row2;
    reg [7:0] issue_oy, issue_ox;
    reg [9:0] center_y, center_x;
    reg [9:0] slot1_base, slot2_base;
    reg [13:0] x_left_base, x_center_base, x_right_base;
    reg [4:0] stride_batch_step;
    reg [3:0] issue_batch, issue_tap;
    reg row_issued, output_complete;

    // Widen BEFORE arithmetic, including descriptors that will be rejected.
    wire [9:0] desc_cin_rounded = {1'b0, cfg_desc[53:45]} + 10'd31;
    wire [4:0] desc_batches = desc_cin_rounded[9:5];
    wire [13:0] desc_row_words = {5'd0, cfg_desc[26:18]} *
                                  {9'd0, desc_batches};

    wire desc_ok = (cfg_desc[6:5] == 2'd1) &&
        ((cfg_desc[64:63] == 2'd1) || (cfg_desc[64:63] == 2'd2)) &&
        (cfg_desc[66:65] == 2'd1) && (cfg_desc[68:67] == 2'd1) &&
        (cfg_desc[17:9] >= 9'd1) && (cfg_desc[17:9] <= 9'd256) &&
        (cfg_desc[26:18] >= 9'd1) && (cfg_desc[26:18] <= 9'd256) &&
        (cfg_desc[35:27] >= 9'd1) && (cfg_desc[35:27] <= 9'd256) &&
        (cfg_desc[44:36] >= 9'd1) && (cfg_desc[44:36] <= 9'd256) &&
        (cfg_desc[53:45] >= 9'd1) && (cfg_desc[53:45] <= 9'd384) &&
        (desc_batches >= 5'd1) && (desc_batches <= 5'd12) &&
        (desc_row_words >= 14'd1) && (desc_row_words <= 14'd256);

    // A valid descriptor guarantees row_words<=256, so the product used by
    // the running count is exactly a 9x9 multiply. Invalid descriptors enter
    // FAULT and never consume this value; keep their derived count at zero.
    // The result is widened to the required 22-bit intermediate and mapped
    // to LUT/carry logic rather than a DSP48E1.
    (* use_dsp = "no" *) wire [17:0] desc_input_beats_valid =
        {9'd0, cfg_desc[17:9]} * {9'd0, desc_row_words[8:0]};
    wire [21:0] desc_input_beats = desc_ok ?
        {4'd0, desc_input_beats_valid} : 22'd0;

    function [31:0] batch_mask;
        input [8:0] channels;
        input [3:0] batch;
        reg [9:0] remaining;
        begin
            remaining = {1'b0, channels} - {1'b0, batch, 5'b0};
            if (remaining >= 10'd32)
                batch_mask = 32'hffffffff;
            else
                batch_mask = (32'h1 << remaining) - 32'h1;
        end
    endfunction

    wire input_complete = (accepted_input_beats == expected_input_beats);
    wire input_row_last = ({1'b0, in_col} == cfg_win - 9'd1) &&
                          (in_batch == last_batch);
    wire input_final = input_row_last && ({1'b0, in_row} == cfg_hin - 9'd1);
    reg [63:0] expected_input_tag;
    always @* begin
        expected_input_tag = 64'd0;
        expected_input_tag[7:0] = in_col;
        expected_input_tag[15:8] = in_row;
        expected_input_tag[19:16] = in_batch;
        expected_input_tag[31] = (in_batch == last_batch);
        expected_input_tag[32] = (in_batch == last_batch);
        expected_input_tag[37:33] = cfg_op_id;
        expected_input_tag[38] = input_final;
    end
    wire input_protocol_ok = (s_pixel_tag == expected_input_tag) &&
        (s_pixel_mask == batch_mask(cfg_cin, in_batch)) &&
        ({1'b0, in_row} < cfg_hin) && ({1'b0, in_col} < cfg_win) &&
        (in_batch < num_batches) && (accepted_input_beats < expected_input_beats);

    // N1: keep the source-row fill target aligned with center_y in a local
    // register.  The ready/progress cone consumes only this invariant value;
    // the next clamp is evaluated solely on a center-row advance edge.
    reg [8:0] fill_target;
    wire [9:0] center_y_next = center_y + {8'd0, cfg_stride};
    wire [9:0] bottom_y_next = center_y_next + 10'd1;
    wire [8:0] fill_target_next =
        (bottom_y_next >= {1'b0, cfg_hin}) ?
        cfg_hin - 9'd1 : bottom_y_next[8:0];
    reg [1:0] ky, kx;
    always @* begin
        case (issue_tap)
            4'd0: begin ky = 2'd0; kx = 2'd0; end
            4'd1: begin ky = 2'd0; kx = 2'd1; end
            4'd2: begin ky = 2'd0; kx = 2'd2; end
            4'd3: begin ky = 2'd1; kx = 2'd0; end
            4'd4: begin ky = 2'd1; kx = 2'd1; end
            4'd5: begin ky = 2'd1; kx = 2'd2; end
            4'd6: begin ky = 2'd2; kx = 2'd0; end
            4'd7: begin ky = 2'd2; kx = 2'd1; end
            default: begin ky = 2'd2; kx = 2'd2; end
        endcase
    end
    wire signed [10:0] iy = $signed({1'b0, center_y}) - 11'sd1 +
                             $signed({9'd0, ky});
    wire signed [10:0] ix = $signed({1'b0, center_x}) - 11'sd1 +
                             $signed({9'd0, kx});
    wire issue_oob = (iy < 11'sd0) || (ix < 11'sd0) ||
        (iy >= $signed({2'd0, cfg_hin})) || (ix >= $signed({2'd0, cfg_win}));
    wire [2:0] slot_match = {
        slot_valid[2] && (slot_row2 == iy[7:0]),
        slot_valid[1] && (slot_row1 == iy[7:0]),
        slot_valid[0] && (slot_row0 == iy[7:0])};
    wire lookup_found = |slot_match;
    wire [1:0] read_slot = slot_match[0] ? 2'd0 :
                          slot_match[1] ? 2'd1 : 2'd2;
    wire [13:0] write_addr_wide = {12'd0, write_slot} * {5'd0, row_words} +
        {6'd0, in_col} * {10'd0, num_batches} + {10'd0, in_batch};
    wire [9:0] selected_slot_base = slot_match[0] ? 10'd0 :
        slot_match[1] ? slot1_base : slot2_base;
    wire [13:0] selected_x_base = (kx == 2'd0) ? x_left_base :
        (kx == 2'd1) ? x_center_base : x_right_base;
    wire [13:0] read_addr_wide = {4'd0, selected_slot_base} +
        selected_x_base + {10'd0, issue_batch};
    wire [9:0] write_addr = write_addr_wide[9:0];
    wire issue_final_in_row = ({1'b0, issue_ox} == cfg_wout - 9'd1) &&
        (issue_batch == last_batch) && (issue_tap == 4'd8);
    wire issue_final_operation = issue_final_in_row &&
        ({1'b0, issue_oy} == cfg_hout - 9'd1);
    reg [63:0] issue_tag;
    always @* begin
        issue_tag = 64'd0;
        issue_tag[7:0] = issue_ox;
        issue_tag[15:8] = issue_oy;
        issue_tag[19:16] = issue_batch;
        issue_tag[30:27] = issue_tap;
        issue_tag[31] = (issue_batch == last_batch) && (issue_tap == 4'd8);
        issue_tag[32] = issue_tag[31];
        issue_tag[37:33] = cfg_op_id;
        issue_tag[38] = issue_final_operation;
    end

    // Edge N: registered address/metadata issue. Edge N+1: synchronous
    // bank response. Edge N+2: output register, including virtual zero taps.
    reg req_valid, req_zero, req_row_last;
    reg [9:0] req_addr;
    reg [31:0] req_mask;
    reg [63:0] req_tag;
    reg response_valid, response_zero, response_row_last;
    reg [31:0] response_mask;
    reg [63:0] response_tag;
    reg out_valid, out_row_last;
    reg [255:0] out_data;
    reg [31:0] out_mask;
    reg [63:0] out_tag;

    assign cfg_ready = rst_n && !fault_reg && (state == IDLE);
    assign done = rst_n && !fault_reg && (state == DONE);
    assign fault = rst_n && fault_reg;
    assign s_pixel_ready = rst_n && !fault_reg && !input_complete &&
        (((state == LOAD_ROWS) && (next_input_row <= fill_target)) ||
         (state == DRAIN_INPUT));
    assign m_tap_valid = rst_n && !fault_reg && out_valid;
    assign m_tap_data = out_data;
    assign m_tap_mask = out_mask;
    assign m_tap_tag = out_tag;

    wire pipeline_advance = rst_n && !fault_reg && (state == TAP_STREAM) &&
                            (!out_valid || m_tap_ready);
    wire tap_issue = pipeline_advance && !row_issued;
    wire input_accept = s_pixel_valid && s_pixel_ready;
    wire ram_we = input_accept && input_protocol_ok;
    wire ram_re = pipeline_advance && req_valid && !req_zero;
    wire output_accept = m_tap_valid && m_tap_ready;

    // Simple dual-port synchronous template; enables are phase-exclusive.
    // Four 64x768 banks target eight RAMB36; actual packing needs synthesis.
    (* ram_style = "block" *) reg [63:0] bank0 [0:767];
    (* ram_style = "block" *) reg [63:0] bank1 [0:767];
    (* ram_style = "block" *) reg [63:0] bank2 [0:767];
    (* ram_style = "block" *) reg [63:0] bank3 [0:767];
    reg [63:0] ram_q0, ram_q1, ram_q2, ram_q3;
    always @(posedge clk) begin
        if (ram_we) begin
            bank0[write_addr] <= s_pixel_data[63:0];
            bank1[write_addr] <= s_pixel_data[127:64];
            bank2[write_addr] <= s_pixel_data[191:128];
            bank3[write_addr] <= s_pixel_data[255:192];
        end
        if (ram_re) begin
            ram_q0 <= bank0[req_addr];
            ram_q1 <= bank1[req_addr];
            ram_q2 <= bank2[req_addr];
            ram_q3 <= bank3[req_addr];
        end
    end

    always @(posedge clk) begin
        if (!rst_n) begin
            req_valid <= 1'b0;
            req_zero <= 1'b0;
            req_row_last <= 1'b0;
            req_addr <= 10'd0;
            req_mask <= 32'd0;
            req_tag <= 64'd0;
            response_valid <= 1'b0;
            response_zero <= 1'b0;
            response_row_last <= 1'b0;
            response_mask <= 32'd0;
            response_tag <= 64'd0;
            out_valid <= 1'b0;
            out_row_last <= 1'b0;
            out_data <= 256'd0;
            out_mask <= 32'd0;
            out_tag <= 64'd0;
        end else if (state != TAP_STREAM || fault_reg) begin
            req_valid <= 1'b0;
            response_valid <= 1'b0;
            out_valid <= 1'b0;
        end else if (pipeline_advance) begin
            req_valid <= tap_issue;
            if (tap_issue) begin
                // Lookup miss is not a new public fault; suppress stale reads.
                // The testbench asserts an in-bound lookup has exactly one hit.
                req_zero <= issue_oob || !lookup_found;
                req_addr <= read_addr_wide[9:0];
                req_mask <= batch_mask(cfg_cin, issue_batch);
                req_tag <= issue_tag;
                req_row_last <= issue_final_in_row;
            end
            response_valid <= req_valid;
            if (req_valid) begin
                response_zero <= req_zero;
                response_mask <= req_mask;
                response_tag <= req_tag;
                response_row_last <= req_row_last;
            end
            out_valid <= response_valid;
            if (response_valid) begin
                out_data <= response_zero ? 256'd0 : {ram_q3, ram_q2, ram_q1, ram_q0};
                out_mask <= response_mask;
                out_tag <= response_tag;
                out_row_last <= response_row_last;
            end
        end
    end

    always @(posedge clk) begin
        if (!rst_n) begin
            state <= IDLE;
            fault_reg <= 1'b0;
            cfg_latched <= 256'd0;
            cfg_op_id <= 5'd0;
            cfg_hin <= 9'd0; cfg_win <= 9'd0;
            cfg_hout <= 9'd0; cfg_wout <= 9'd0; cfg_cin <= 9'd0;
            cfg_stride <= 2'd0;
            num_batches <= 4'd0; last_batch <= 4'd0;
            row_words <= 9'd0;
            expected_input_beats <= 20'd0;
            accepted_input_beats <= 20'd0;
            in_row <= 8'd0; in_col <= 8'd0; in_batch <= 4'd0;
            next_input_row <= 9'd0;
            write_slot <= 2'd0;
            slot_valid <= 3'd0;
            slot_row0 <= 8'd0; slot_row1 <= 8'd0; slot_row2 <= 8'd0;
            issue_oy <= 8'd0; issue_ox <= 8'd0;
            center_y <= 10'd0; center_x <= 10'd0;
            fill_target <= 9'd0;
            slot1_base <= 10'd0; slot2_base <= 10'd0;
            x_left_base <= 14'd0; x_center_base <= 14'd0;
            x_right_base <= 14'd0; stride_batch_step <= 5'd0;
            issue_batch <= 4'd0; issue_tap <= 4'd0;
            row_issued <= 1'b0;
            output_complete <= 1'b0;
        end else begin
            case (state)
                IDLE: if (cfg_valid && cfg_ready) begin
                    cfg_latched <= cfg_desc;
                    cfg_op_id <= cfg_desc[4:0];
                    cfg_hin <= cfg_desc[17:9]; cfg_win <= cfg_desc[26:18];
                    cfg_hout <= cfg_desc[35:27]; cfg_wout <= cfg_desc[44:36];
                    cfg_cin <= cfg_desc[53:45]; cfg_stride <= cfg_desc[64:63];
                    num_batches <= desc_batches[3:0];
                    last_batch <= desc_batches[3:0] - 4'd1;
                    row_words <= desc_row_words[8:0];
                    slot1_base <= {1'b0, desc_row_words[8:0]};
                    slot2_base <= {desc_row_words[8:0], 1'b0};
                    stride_batch_step <= (cfg_desc[64:63] == 2'd2) ?
                        {desc_batches[3:0], 1'b0} : {1'b0, desc_batches[3:0]};
                    expected_input_beats <= desc_input_beats[19:0];
                    accepted_input_beats <= 20'd0;
                    in_row <= 8'd0; in_col <= 8'd0; in_batch <= 4'd0;
                    next_input_row <= 9'd0;
                    write_slot <= 2'd0;
                    slot_valid <= 3'd0;
                    issue_oy <= 8'd0; issue_ox <= 8'd0;
                    center_y <= 10'd0; center_x <= 10'd0;
                    fill_target <= (10'd1 >= {1'b0, cfg_desc[17:9]}) ?
                        cfg_desc[17:9] - 9'd1 : 9'd1;
                    x_left_base <= 14'd0 - {10'd0, desc_batches[3:0]};
                    x_center_base <= 14'd0;
                    x_right_base <= {10'd0, desc_batches[3:0]};
                    issue_batch <= 4'd0; issue_tap <= 4'd0;
                    row_issued <= 1'b0;
                    output_complete <= 1'b0;
                    if (desc_ok)
                        state <= LOAD_ROWS;
                    else begin
                        fault_reg <= 1'b1;
                        state <= FAULT;
                    end
                end
                LOAD_ROWS, DRAIN_INPUT: begin
                    if ((state == LOAD_ROWS) && (next_input_row > fill_target))
                        state <= TAP_STREAM;
                    if (input_accept) begin
                        if (!input_protocol_ok) begin
                            fault_reg <= 1'b1;
                            state <= FAULT;
                        end else begin
                            accepted_input_beats <= accepted_input_beats + 20'd1;
                            if ((in_col == 8'd0) && (in_batch == 4'd0))
                                slot_valid[write_slot] <= 1'b0;
                            if (input_row_last) begin
                                slot_valid[write_slot] <= 1'b1;
                                case (write_slot)
                                    2'd0: slot_row0 <= in_row;
                                    2'd1: slot_row1 <= in_row;
                                    default: slot_row2 <= in_row;
                                endcase
                                write_slot <= (write_slot == 2'd2) ? 2'd0 : write_slot + 2'd1;
                                next_input_row <= {1'b0, in_row} + 9'd1;
                                // Keep final 8-bit coordinate at 255; use the
                                // independent 9-bit next_input_row sentinel 256.
                                if (!input_final) in_row <= in_row + 8'd1;
                                in_col <= 8'd0;
                                in_batch <= 4'd0;
                                if ((state == LOAD_ROWS) && ({1'b0, in_row} == fill_target))
                                    state <= TAP_STREAM;
                            end else if (in_batch == last_batch) begin
                                in_batch <= 4'd0;
                                in_col <= in_col + 8'd1;
                            end else
                                in_batch <= in_batch + 4'd1;
                            if ((state == DRAIN_INPUT) && input_final && output_complete)
                                state <= DONE;
                        end
                    end
                end
                TAP_STREAM: begin
                    if (tap_issue) begin
                        if (issue_final_in_row)
                            row_issued <= 1'b1;
                        else if (issue_tap == 4'd8) begin
                            issue_tap <= 4'd0;
                            if (issue_batch == last_batch) begin
                                issue_batch <= 4'd0;
                                issue_ox <= issue_ox + 8'd1;
                                center_x <= center_x + {8'd0, cfg_stride};
                                x_left_base <= x_left_base + {9'd0, stride_batch_step};
                                x_center_base <= x_center_base + {9'd0, stride_batch_step};
                                x_right_base <= x_right_base + {9'd0, stride_batch_step};
                            end else
                                issue_batch <= issue_batch + 4'd1;
                        end else
                            issue_tap <= issue_tap + 4'd1;
                    end
                    if (output_accept && out_row_last) begin
                        if (out_tag[38]) begin
                            output_complete <= 1'b1;
                            state <= input_complete ? DONE : DRAIN_INPUT;
                        end else begin
                            issue_oy <= issue_oy + 8'd1;
                            issue_ox <= 8'd0;
                            center_y <= center_y_next;
                            fill_target <= fill_target_next;
                            center_x <= 10'd0;
                            x_left_base <= 14'd0 - {10'd0, num_batches};
                            x_center_base <= 14'd0;
                            x_right_base <= {10'd0, num_batches};
                            issue_batch <= 4'd0;
                            issue_tap <= 4'd0;
                            row_issued <= 1'b0;
                            state <= LOAD_ROWS;
                        end
                    end
                end
                DONE: state <= IDLE;
                FAULT: state <= FAULT;
                default: state <= IDLE;
            endcase
        end
    end
endmodule
