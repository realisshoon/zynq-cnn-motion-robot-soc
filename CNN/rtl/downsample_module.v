`timescale 1ns / 1ps
// CNN-v4.0 downsample. All state changes occur on the rising clock edge.
module downsample_module (
    input wire clk,
    input wire rst_n,
    input wire cfg_valid,
    output wire cfg_ready,
    input wire [255:0] cfg_desc,
    output wire done,
    output wire fault,
    input wire [63:0] s_dma_data,
    input wire s_dma_valid,
    output wire s_dma_ready,
    input wire s_dma_last,
    input wire [7:0] s_dma_keep,
    output wire [23:0] m_rgb_data,
    output wire m_rgb_valid,
    input wire m_rgb_ready,
    output wire [63:0] m_rgb_tag,
    output wire [23:0] tap_data,
    output wire [9:0] tap_row,
    output wire [10:0] tap_col,
    output wire tap_accept,
    output wire tap_last
);
    localparam [2:0] IDLE=3'd0, PAD_TOP=3'd1, REAL=3'd2,
                 PAD_BOTTOM=3'd3, DONE=3'd4, FAULT=3'd5;
    reg [  2:0] state;
    reg [255:0] cfg_latched;  // All descriptor bits retained until completion.
    reg [  4:0] op_id;
    reg [  7:0] dma_row;  // Advances on accepted beat 479.
    reg [  8:0] dma_beat;  // Advances on every other accepted beat.
    reg [ 16:0] dma_total;  // Advances on every accepted DMA beat.
    reg [8:0] wr_ptr, rd_ptr;  // Advance on FIFO write/read respectively.
    reg [9:0] fifo_count;  // Updated for simultaneous write and read.
    // Frozen contract: one 512 x 65 ingress FIFO stores {last,data}.
    reg [64:0] fifo_mem[0:511];
    reg [64:0] read_data;
    reg read_pending;  // Synchronous FIFO read response, appended next cycle.
    reg [8:0] fifo_pop_beat; // Validated stored-last position, 0..479.
    reg [127:0] hold_data;
    reg [4:0] hold_bytes;  // Valid bytes, 0..16.
    reg [7:0] source_row;  // Advances at source column 1279 pop.
    reg [10:0] source_col;  // Advances on each other source RGB pop.
    reg [2:0] col_mod5;  // Advances with source_col, wraps at 5.
    reg [7:0] output_row;  // Advances on accepted output column 255.
    reg [7:0] output_col;  // Advances on every other output accept.
    reg [17:0] real_count;  // Advances on each RGB pop, expected 184320.
    reg [16:0] output_count;  // Advances on each output accept, expected 65536.
    reg sticky_fault, done_r;
    reg fault_payload_valid;
    reg [23:0] fault_payload_data;
    reg [63:0] fault_payload_tag;

    wire active = (state == PAD_TOP || state == REAL || state == PAD_BOTTOM);
    assign cfg_ready = rst_n && state == IDLE && !sticky_fault;
    wire cfg_accept = cfg_valid && cfg_ready;
    // Only fields whose legality is unambiguous at this boundary are checked.
    // Other descriptor fields remain latched context owned by their consumers.
    wire cfg_protocol_error = cfg_accept &&
        (cfg_desc[4:0] > 5'd28 || |cfg_desc[255:149]);
    assign s_dma_ready = rst_n && active && !sticky_fault &&
                     fifo_count!=10'd512;
    wire dma_accept = s_dma_valid && s_dma_ready;
    wire protocol_error = dma_accept &&
    (s_dma_keep!=8'hff ||
     s_dma_last!=(dma_beat==9'd479) ||
     dma_row>=8'd144 || dma_total>=17'd69120);
    wire internal_error = (fifo_count>10'd512 || hold_bytes>5'd16 ||
    dma_beat>9'd479 || fifo_pop_beat>9'd479 ||
    dma_row>8'd144 || dma_total>17'd69120 ||
    source_row>8'd144 || source_col>11'd1279 || col_mod5>3'd4 ||
    real_count>18'd184320 || output_count>17'd65536);
    wire append_candidate = read_pending && active && !sticky_fault &&
                            !internal_error;
    wire stored_last_error = append_candidate &&
        (read_data[64] != (fifo_pop_beat == 9'd479));
    wire error_now = protocol_error || stored_last_error || internal_error;
    wire selected = col_mod5 == 3'd0;
    wire rgb_available = hold_bytes >= 5'd3;
    wire [63:0] live_tag = {
        24'd0, 1'b0, // reserved, group_last
        (output_row == 8'd255 && output_col == 8'd255),
        op_id,
        1'b1,
        16'd0,
        output_row,
        output_col
    };
    wire live_valid = rst_n && !sticky_fault && active &&
    (state!=REAL || (selected && rgb_available));
    assign m_rgb_valid = fault_payload_valid || live_valid;
    assign m_rgb_data = fault_payload_valid ? fault_payload_data :
                    (state==REAL ? hold_data[23:0] : 24'd0);
    assign m_rgb_tag = !rst_n ? 64'd0 :
                      (fault_payload_valid ? fault_payload_tag :
                       (live_valid ? live_tag : 64'd0));
    wire output_accept = m_rgb_valid && m_rgb_ready;
    wire rgb_pop = rst_n && state==REAL && !sticky_fault &&
               rgb_available && (!selected || output_accept);
    assign tap_accept = rgb_pop;
    assign tap_data = hold_data[23:0];
    // 5*y is shift/add, leaving DSP resources unused.
    assign tap_row = {source_row, 2'b00} + source_row;
    assign tap_col = source_col;
    assign tap_last = rgb_pop && source_row == 8'd143 && source_col == 11'd1279;
    assign done = done_r;
    assign fault = sticky_fault;

    // Read port is synchronous. keep is checked only on DMA acceptance. last
    // is checked at acceptance and preserved with data for an ordered pop-side
    // check, so the ingress RAM remains exactly one 512 x 65 FIFO.
    // An invalid input is discarded. Independent transactions already offered
    // on this edge still commit, including a read/append of earlier valid data.
    wire fifo_write = dma_accept && !protocol_error && !internal_error;
    wire fifo_read = rst_n && active && !sticky_fault && !internal_error &&
                 !read_pending && fifo_count!=0 && hold_bytes<=5'd8;
    wire append = append_candidate && !stored_last_error;
    wire [127:0] shifted = rgb_pop ? (hold_data >> 24) : hold_data;
    wire [4:0] append_base = rgb_pop ? hold_bytes - 5'd3 : hold_bytes;
    wire [7:0] append_shift = {append_base, 3'b000};
    always @(posedge clk) begin
        if (fifo_write) fifo_mem[wr_ptr] <= {s_dma_last, s_dma_data};
        if (fifo_read) read_data <= fifo_mem[rd_ptr];
    end

    always @(posedge clk) begin
        if (!rst_n) begin
            state <= IDLE;
            cfg_latched <= 0;
            op_id <= 0;
            dma_row <= 0;
            dma_beat <= 0;
            dma_total <= 0;
            wr_ptr <= 0;
            rd_ptr <= 0;
            fifo_count <= 0;
            read_pending <= 0;
            fifo_pop_beat <= 0;
            hold_data <= 0;
            hold_bytes <= 0;
            source_row <= 0;
            source_col <= 0;
            col_mod5 <= 0;
            output_row <= 0;
            output_col <= 0;
            real_count <= 0;
            output_count <= 0;
            sticky_fault <= 0;
            done_r <= 0;
            fault_payload_valid <= 0;
            fault_payload_data <= 0;
            fault_payload_tag <= 0;
        end else begin
            done_r <= 0;
            if (fault_payload_valid && m_rgb_ready) fault_payload_valid <= 0;
            begin
                if (cfg_accept && !cfg_protocol_error) begin
                    cfg_latched <= cfg_desc;
                    op_id <= cfg_desc[4:0];
                    dma_row <= 0;
                    dma_beat <= 0;
                    dma_total <= 0;
                    wr_ptr <= 0;
                    rd_ptr <= 0;
                    fifo_count <= 0;
                    read_pending <= 0;
                    fifo_pop_beat <= 0;
                    hold_data <= 0;
                    hold_bytes <= 0;
                    source_row <= 0;
                    source_col <= 0;
                    col_mod5 <= 0;
                    output_row <= 0;
                    output_col <= 0;
                    real_count <= 0;
                    output_count <= 0;
                    state <= PAD_TOP;
                end
                if (cfg_protocol_error) begin
                    cfg_latched <= cfg_desc;
                    op_id <= cfg_desc[4:0];
                    sticky_fault <= 1;
                    state <= FAULT;
                    done_r <= 0;
                end
                if (dma_accept) begin
                    dma_total <= dma_total + 17'd1;
                    if (dma_beat == 9'd479) begin
                        dma_beat <= 0;
                        dma_row  <= dma_row + 8'd1;
                    end else dma_beat <= dma_beat + 9'd1;
                end
                if (fifo_write) wr_ptr <= wr_ptr + 9'd1;
                if (fifo_read) begin
                    rd_ptr <= rd_ptr + 9'd1;
                    read_pending <= 1;
                end else if (append) read_pending <= 0;
                if (append) begin
                    if (fifo_pop_beat == 9'd479) fifo_pop_beat <= 0;
                    else fifo_pop_beat <= fifo_pop_beat + 9'd1;
                end
                case ({
                    fifo_write, fifo_read
                })
                    2'b10:   fifo_count <= fifo_count + 10'd1;
                    2'b01:   fifo_count <= fifo_count - 10'd1;
                    default: ;
                endcase
                // Append-only, pop-only, simultaneous and idle all preserve byte order.
                case ({
                    append, rgb_pop
                })
                    2'b10: begin
                        hold_data<=hold_data | ({64'd0,read_data[63:0]} << {hold_bytes,3'b000});
                        hold_bytes <= hold_bytes + 5'd8;
                    end
                    2'b01: begin
                        hold_data  <= shifted;
                        hold_bytes <= hold_bytes - 5'd3;
                    end
                    2'b11: begin
                        hold_data<=shifted | ({64'd0,read_data[63:0]} << append_shift);
                        hold_bytes <= hold_bytes + 5'd5;
                    end
                    default: ;
                endcase
                if (rgb_pop) begin
                    real_count <= real_count + 18'd1;
                    if (source_col == 11'd1279) begin
                        source_col <= 0;
                        col_mod5   <= 0;
                        source_row <= source_row + 8'd1;
                        if (source_row == 8'd143) state <= PAD_BOTTOM;
                    end else begin
                        source_col <= source_col + 11'd1;
                        col_mod5<=selected ? 3'd1 :
                              (col_mod5==3'd4 ? 3'd0 : col_mod5+3'd1);
                    end
                end
                if (output_accept) begin
                    output_count <= output_count + 17'd1;
                    if (output_col == 8'd255) begin
                        output_col <= 0;
                        output_row <= output_row + 8'd1;
                    end else output_col <= output_col + 8'd1;
                    if (state==PAD_TOP && output_row==8'd55 && output_col==8'd255)
                        state <= REAL;
                    if ((state==PAD_BOTTOM || fault_payload_valid) &&
                        output_row==8'd255 && output_col==8'd255) begin
                        done_r <= 1;
                        if (!sticky_fault) state <= DONE;
                    end
                end
                if (state == DONE) begin
                    state <= IDLE;
                end
                // Fault overrides the next control state, never the commits
                // above. A held payload can drain once in FAULT; no new tap or
                // source pop is generated after the detecting edge.
                if (error_now && active) begin
                    // Fault has priority over normal completion. Transaction
                    // side effects already accepted on this edge still commit,
                    // but a completion pulse is suppressed.
                    done_r <= 0;
                    sticky_fault <= 1;
                    state <= FAULT;
                    if (m_rgb_valid && !m_rgb_ready) begin
                        fault_payload_valid <= 1;
                        fault_payload_data <= m_rgb_data;
                        fault_payload_tag <= m_rgb_tag;
                    end
                end
            end
        end
    end
endmodule
