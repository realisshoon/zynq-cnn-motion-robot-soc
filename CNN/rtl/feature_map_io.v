`timescale 1ns / 1ps

module feature_map_io (
    input wire clk,
    input wire rst_n,
    input wire cfg_valid,
    output wire cfg_ready,
    input wire [255:0] cfg_desc,
    output wire done,
    output wire fault,
    input wire [63:0] s_body_data,
    input wire s_body_valid,
    output wire s_body_ready,
    input wire [3:0] s_body_mask,
    input wire [63:0] s_body_tag,
    output wire [255:0] m_pixel_data,
    output wire m_pixel_valid,
    input wire m_pixel_ready,
    output wire [31:0] m_pixel_mask,
    output wire [63:0] m_pixel_tag,
    input wire [31:0] src_addr,
    input wire [31:0] dst_addr,
    input wire [19:0] src_bytes,
    input wire [19:0] dst_bytes,
    input wire read_en,
    input wire write_en,
    input wire read_dma_done,
    input wire write_dma_done,
    input wire dma_error,
    output wire read_done,
    output wire write_done,
    input wire [63:0] s_dma_read_data,
    input wire s_dma_read_valid,
    output wire s_dma_read_ready,
    input wire s_dma_read_last,
    input wire [7:0] s_dma_read_keep,
    output wire [63:0] m_dma_write_data,
    output wire m_dma_write_valid,
    input wire m_dma_write_ready,
    output wire m_dma_write_last,
    output wire [7:0] m_dma_write_keep,
    output wire [9:0] debug_fault_reason,
    output wire [20:0] debug_read_input_bytes,
    output wire [20:0] debug_read_parsed_bytes,
    output wire [20:0] debug_write_input_bytes,
    output wire [20:0] debug_write_packed_bytes,
    output wire [20:0] debug_write_output_bytes,
    output wire [31:0] debug_stream_status,
    output wire [63:0] debug_last_body_tag,
    output wire [31:0] debug_protocol_misc,
    output wire [19:0] debug_expected_src_bytes,
    output wire [19:0] debug_expected_dst_bytes
);

    (* ram_style = "block" *) reg [63:0] read_fifo [0:511];
    (* ram_style = "block" *) reg [63:0] write_fifo [0:511];

    reg active, fault_reg, done_reg;
    reg read_done_reg, write_done_reg, operation_complete_seen;
    reg read_path_en, write_path_en;
    reg [255:0] desc_reg;
    reg [31:0] src_addr_reg, dst_addr_reg;
    reg [19:0] src_bytes_reg, dst_bytes_reg;
    reg [8:0] hin_reg, win_reg, cin_reg;
    reg [4:0] op_id_reg;

    reg [8:0] read_wr_ptr, read_rd_ptr;
    reg [9:0] read_count;
    reg [20:0] read_input_bytes, read_parsed_bytes;
    reg read_last_seen;
    reg [63:0] read_word;
    reg read_word_valid;
    reg [2:0] read_byte_lane;
    reg [8:0] read_channel, read_row, read_col;
    reg read_frame_generated;
    reg [255:0] pixel_data_reg;
    reg [31:0] pixel_mask_reg;
    reg [63:0] pixel_tag_reg;
    reg pixel_valid_reg, final_pixel_accept_seen, read_dma_done_seen;

    reg [8:0] write_wr_ptr, write_rd_ptr;
    reg [9:0] write_count;
    reg [20:0] write_input_bytes, write_packed_bytes, write_output_bytes;
    reg [63:0] body_data_reg;
    reg [3:0] body_mask_reg;
    reg [2:0] body_lane;
    reg body_busy;
    reg [63:0] write_pack, write_word;
    reg [2:0] write_fill;
    reg [7:0] write_keep_reg;
    reg write_last_reg, write_valid_reg;
    reg final_write_accept_seen, write_dma_done_seen;

    // Diagnostic-only sticky context. These registers do not participate in
    // datapath control and are cleared for every accepted configuration.
    reg [9:0] debug_fault_reason_reg;
    reg [63:0] debug_last_body_tag_reg;
    reg [3:0] debug_last_body_mask_reg;
    reg [7:0] debug_last_read_keep_reg;
    reg debug_last_read_last_reg;

    wire cfg_accept = cfg_valid && cfg_ready;
    wire read_push = s_dma_read_valid && s_dma_read_ready;
    wire read_pop = active && read_path_en && !fault_reg &&
                    !read_word_valid && read_count != 0;
    wire read_byte_step = active && read_path_en && !fault_reg &&
                          read_word_valid && !pixel_valid_reg;
    wire [7:0] read_byte = read_word[read_byte_lane * 8 +: 8];
    wire read_batch_end = (read_channel[4:0] == 5'd31) ||
                          (read_channel == cin_reg - 1'b1);
    wire read_pixel_end = read_channel == cin_reg - 1'b1;
    wire read_final_pixel = read_pixel_end &&
                            (read_col == win_reg - 1'b1) &&
                            (read_row == hin_reg - 1'b1);
    wire pixel_accept = pixel_valid_reg && m_pixel_ready;
    wire write_pop = active && write_path_en && !fault_reg &&
                     !write_valid_reg && write_count != 0;
    wire write_accept = write_valid_reg && m_dma_write_ready;
    wire body_accept = s_body_valid && s_body_ready;
    wire [4:0] expected_body_op_id =
        (op_id_reg == 5'd0) ? 5'd0 : (op_id_reg + 5'd1);
    wire body_tag_mismatch = body_accept &&
        (s_body_tag[37:33] != expected_body_op_id);
    wire [2:0] body_bytes = {2'd0, s_body_mask[0]} +
                            {2'd0, s_body_mask[1]} +
                            {2'd0, s_body_mask[2]} +
                            {2'd0, s_body_mask[3]};
    wire write_lane_step = active && write_path_en && !fault_reg &&
                           !body_tag_mismatch && body_busy &&
                           write_count != 10'd512;
    wire write_byte_step = write_lane_step && body_mask_reg[body_lane];
    wire [7:0] write_byte = body_data_reg[body_lane * 16 +: 8];
    reg [63:0] packed_word;
    always @* begin
        packed_word = write_pack;
        packed_word[write_fill * 8 +: 8] = write_byte;
    end
    wire write_push = write_byte_step &&
                      ((write_fill == 3'd7) ||
                       (write_packed_bytes + 1'b1 == dst_bytes_reg));

    wire read_dma_sample = active && read_path_en && read_dma_done;
    wire write_dma_sample = active && write_path_en && write_dma_done;
    wire [20:0] read_beat_end = read_input_bytes + 21'd8;
    wire read_complete = !read_path_en ||
                         (final_pixel_accept_seen && read_dma_done_seen);
    wire write_complete = !write_path_en ||
                          (final_write_accept_seen && write_dma_done_seen);
    wire read_underflow = active && read_path_en && read_last_seen &&
                          (read_parsed_bytes == read_input_bytes) &&
                          (read_count == 0) && !read_word_valid &&
                          !pixel_valid_reg && !read_frame_generated;
    wire dma_fault = active && dma_error;
    wire read_accept_fault = read_push &&
        (s_dma_read_keep != 8'hff ||
         read_beat_end > {1'b0, src_bytes_reg} ||
         s_dma_read_last != (read_beat_end == {1'b0, src_bytes_reg}));
    wire read_extra_input_fault = read_path_en && read_last_seen &&
                                  s_dma_read_valid;
    // AXI DMA MM2S Idle may assert after the memory mover has filled its
    // internal stream FIFO, before those buffered AXIS beats are accepted.
    // Keep DMA completion ordering-independent; validate TLAST/keep/count on
    // each real AXIS accept edge instead.
    wire read_dma_done_fault = read_dma_sample && read_last_seen &&
        (read_input_bytes != {1'b0, src_bytes_reg});
    wire read_after_frame_fault = read_byte_step && read_frame_generated;
    wire read_protocol_fault = active &&
        (read_accept_fault || read_extra_input_fault ||
         read_dma_done_fault || read_underflow || read_after_frame_fault);
    wire write_body_count_fault = body_accept &&
        (body_bytes == 0 ||
         write_input_bytes + body_bytes > {1'b0, dst_bytes_reg});
    wire write_extra_input_fault = write_path_en &&
        write_input_bytes == {1'b0, dst_bytes_reg} && s_body_valid;
    wire write_protocol_fault = active &&
        (body_tag_mismatch || write_body_count_fault ||
         write_extra_input_fault);
    wire protocol_fault = dma_fault || read_protocol_fault ||
                          write_protocol_fault;
    wire fault_accept = protocol_fault && !fault_reg;
    wire read_state_commit_ok = !dma_fault && !read_protocol_fault;
    wire write_state_commit_ok = !dma_fault;
    wire write_body_commit_ok = write_state_commit_ok &&
                                !body_tag_mismatch &&
                                !write_body_count_fault;

    assign cfg_ready = rst_n && !active && !fault_reg;
    assign fault = fault_reg;
    assign done = done_reg;
    assign read_done = read_path_en && read_done_reg;
    assign write_done = write_path_en && write_done_reg;
    assign s_dma_read_ready = active && read_path_en && !fault_reg &&
                              !read_last_seen && read_count != 10'd512 &&
                              read_input_bytes < {1'b0, src_bytes_reg};
    assign m_pixel_data = pixel_data_reg;
    assign m_pixel_mask = pixel_mask_reg;
    assign m_pixel_tag = pixel_tag_reg;
    assign m_pixel_valid = pixel_valid_reg;
    assign s_body_ready = active && write_path_en && !fault_reg &&
                          !body_busy && write_count != 10'd512 &&
                          write_input_bytes < {1'b0, dst_bytes_reg};
    assign m_dma_write_data = (rst_n && write_valid_reg) ? write_word : 64'd0;
    assign m_dma_write_keep = write_keep_reg;
    assign m_dma_write_last = write_last_reg;
    assign m_dma_write_valid = write_valid_reg;
    assign debug_fault_reason = debug_fault_reason_reg;
    assign debug_read_input_bytes = read_input_bytes;
    assign debug_read_parsed_bytes = read_parsed_bytes;
    assign debug_write_input_bytes = write_input_bytes;
    assign debug_write_packed_bytes = write_packed_bytes;
    assign debug_write_output_bytes = write_output_bytes;
    assign debug_stream_status = {fault_reg, active,
        final_write_accept_seen, final_pixel_accept_seen,
        write_dma_done_seen, read_dma_done_seen, write_valid_reg, body_busy,
        read_frame_generated, pixel_valid_reg, read_word_valid, read_last_seen,
        write_count, read_count};
    assign debug_last_body_tag = debug_last_body_tag_reg;
    assign debug_protocol_misc = {7'd0, op_id_reg, 2'd0,
        expected_body_op_id, debug_last_body_mask_reg,
        debug_last_read_last_reg, debug_last_read_keep_reg};
    assign debug_expected_src_bytes = src_bytes_reg;
    assign debug_expected_dst_bytes = dst_bytes_reg;

    // BRAM data arrays retain contents across reset; pointers and valid bits do not.
    always @(posedge clk) begin
        if (read_push && read_state_commit_ok)
            read_fifo[read_wr_ptr] <= s_dma_read_data;
        if (read_pop && read_state_commit_ok)
            read_word <= read_fifo[read_rd_ptr];
        if (write_push && write_state_commit_ok)
            write_fifo[write_wr_ptr] <= packed_word;
        if (write_pop && write_state_commit_ok)
            write_word <= write_fifo[write_rd_ptr];
    end

    always @(posedge clk) begin
        if (!rst_n) begin
            active <= 1'b0;
            fault_reg <= 1'b0;
            done_reg <= 1'b0;
            read_done_reg <= 1'b0;
            write_done_reg <= 1'b0;
            operation_complete_seen <= 1'b0;
            read_path_en <= 1'b0;
            write_path_en <= 1'b0;
            desc_reg <= 256'd0;
            src_addr_reg <= 32'd0;
            dst_addr_reg <= 32'd0;
            src_bytes_reg <= 20'd0;
            dst_bytes_reg <= 20'd0;
            hin_reg <= 9'd0;
            win_reg <= 9'd0;
            cin_reg <= 9'd0;
            op_id_reg <= 5'd0;
            read_wr_ptr <= 9'd0;
            read_rd_ptr <= 9'd0;
            read_count <= 10'd0;
            read_input_bytes <= 21'd0;
            read_parsed_bytes <= 21'd0;
            read_last_seen <= 1'b0;
            read_word_valid <= 1'b0;
            read_byte_lane <= 3'd0;
            read_channel <= 9'd0;
            read_row <= 9'd0;
            read_col <= 9'd0;
            read_frame_generated <= 1'b0;
            pixel_data_reg <= 256'd0;
            pixel_mask_reg <= 32'd0;
            pixel_tag_reg <= 64'd0;
            pixel_valid_reg <= 1'b0;
            final_pixel_accept_seen <= 1'b0;
            read_dma_done_seen <= 1'b0;
            write_wr_ptr <= 9'd0;
            write_rd_ptr <= 9'd0;
            write_count <= 10'd0;
            write_input_bytes <= 21'd0;
            write_packed_bytes <= 21'd0;
            write_output_bytes <= 21'd0;
            body_data_reg <= 64'd0;
            body_mask_reg <= 4'd0;
            body_lane <= 3'd0;
            body_busy <= 1'b0;
            write_pack <= 64'd0;
            write_fill <= 3'd0;
            write_keep_reg <= 8'd0;
            write_last_reg <= 1'b0;
            write_valid_reg <= 1'b0;
            final_write_accept_seen <= 1'b0;
            write_dma_done_seen <= 1'b0;
            debug_fault_reason_reg <= 10'd0;
            debug_last_body_tag_reg <= 64'd0;
            debug_last_body_mask_reg <= 4'd0;
            debug_last_read_keep_reg <= 8'd0;
            debug_last_read_last_reg <= 1'b0;
        end else if (cfg_accept) begin
            done_reg <= 1'b0;
            read_done_reg <= 1'b0;
            write_done_reg <= 1'b0;
            operation_complete_seen <= 1'b0;
            read_path_en <= read_en;
            write_path_en <= write_en;
            desc_reg <= cfg_desc;
            src_addr_reg <= src_addr;
            dst_addr_reg <= dst_addr;
            src_bytes_reg <= src_bytes;
            dst_bytes_reg <= dst_bytes;
            hin_reg <= cfg_desc[17:9];
            win_reg <= cfg_desc[26:18];
            cin_reg <= cfg_desc[53:45];
            op_id_reg <= cfg_desc[4:0];
            read_wr_ptr <= 9'd0;
            read_rd_ptr <= 9'd0;
            read_count <= 10'd0;
            read_input_bytes <= 21'd0;
            read_parsed_bytes <= 21'd0;
            read_last_seen <= 1'b0;
            read_word_valid <= 1'b0;
            read_byte_lane <= 3'd0;
            read_channel <= 9'd0;
            read_row <= 9'd0;
            read_col <= 9'd0;
            read_frame_generated <= 1'b0;
            pixel_data_reg <= 256'd0;
            pixel_mask_reg <= 32'd0;
            pixel_tag_reg <= 64'd0;
            pixel_valid_reg <= 1'b0;
            final_pixel_accept_seen <= 1'b0;
            read_dma_done_seen <= 1'b0;
            write_wr_ptr <= 9'd0;
            write_rd_ptr <= 9'd0;
            write_count <= 10'd0;
            write_input_bytes <= 21'd0;
            write_packed_bytes <= 21'd0;
            write_output_bytes <= 21'd0;
            body_data_reg <= 64'd0;
            body_mask_reg <= 4'd0;
            body_lane <= 3'd0;
            body_busy <= 1'b0;
            write_pack <= 64'd0;
            write_fill <= 3'd0;
            write_keep_reg <= 8'd0;
            write_last_reg <= 1'b0;
            write_valid_reg <= 1'b0;
            final_write_accept_seen <= 1'b0;
            write_dma_done_seen <= 1'b0;
            debug_fault_reason_reg <= 10'd0;
            debug_last_body_tag_reg <= 64'd0;
            debug_last_body_mask_reg <= 4'd0;
            debug_last_read_keep_reg <= 8'd0;
            debug_last_read_last_reg <= 1'b0;
            if ((!read_en && !write_en) ||
                (read_en && (cfg_desc[17:9] == 0 || cfg_desc[26:18] == 0 ||
                             cfg_desc[17:9] > 9'd256 ||
                             cfg_desc[26:18] > 9'd256 ||
                             cfg_desc[53:45] == 0 || src_bytes == 0 ||
                             src_bytes[2:0] != 0)) ||
                (write_en && dst_bytes == 0)) begin
                active <= 1'b0;
                fault_reg <= 1'b1;
                debug_fault_reason_reg <= 10'b0000000001;
            end else begin
                active <= 1'b1;
            end
        end else begin
            done_reg <= 1'b0;
            if (!fault_reg) begin
                if (active && !operation_complete_seen &&
                    read_complete && write_complete) begin
                    done_reg <= 1'b1;
                    operation_complete_seen <= 1'b1;
                    active <= 1'b0;
                end
                if (active && read_path_en && final_pixel_accept_seen &&
                    read_dma_done_seen)
                    read_done_reg <= 1'b1;
                if (active && write_path_en && final_write_accept_seen &&
                    write_dma_done_seen)
                    write_done_reg <= 1'b1;
                if (read_dma_sample)
                    read_dma_done_seen <= 1'b1;
                if (write_dma_sample)
                    write_dma_done_seen <= 1'b1;
                if (read_push && read_state_commit_ok) begin
                    debug_last_read_keep_reg <= s_dma_read_keep;
                    debug_last_read_last_reg <= s_dma_read_last;
                    read_wr_ptr <= read_wr_ptr + 1'b1;
                    read_input_bytes <= read_input_bytes + 21'd8;
                    if (s_dma_read_last)
                        read_last_seen <= 1'b1;
                end
                if (read_pop && read_state_commit_ok) begin
                    read_rd_ptr <= read_rd_ptr + 1'b1;
                    read_word_valid <= 1'b1;
                    read_byte_lane <= 3'd0;
                end
                case ({read_push && read_state_commit_ok,
                       read_pop && read_state_commit_ok})
                    2'b10: read_count <= read_count + 1'b1;
                    2'b01: read_count <= read_count - 1'b1;
                    default: ;
                endcase
                if (pixel_accept) begin
                    pixel_valid_reg <= 1'b0;
                    if (pixel_tag_reg[38])
                        final_pixel_accept_seen <= 1'b1;
                end
                if (read_byte_step && !read_after_frame_fault) begin
                    read_parsed_bytes <= read_parsed_bytes + 1'b1;
                    if (read_channel[4:0] == 0) begin
                        pixel_data_reg <= {248'd0, read_byte};
                        pixel_mask_reg <= 32'd1;
                    end else begin
                        pixel_data_reg[read_channel[4:0] * 8 +: 8] <= read_byte;
                        pixel_mask_reg[read_channel[4:0]] <= 1'b1;
                    end
                    if (read_byte_lane == 3'd7)
                        read_word_valid <= 1'b0;
                    else
                        read_byte_lane <= read_byte_lane + 1'b1;
                    if (read_batch_end) begin
                        pixel_valid_reg <= 1'b1;
                        pixel_tag_reg <= {24'd0, 1'b0, read_final_pixel,
                                          op_id_reg, read_pixel_end,
                                          read_pixel_end, 4'd0, 7'd0,
                                          read_channel[8:5],
                                          read_row[7:0], read_col[7:0]};
                    end
                    if (read_pixel_end) begin
                        read_channel <= 9'd0;
                        if (read_final_pixel)
                            read_frame_generated <= 1'b1;
                        if (read_col == win_reg - 1'b1) begin
                            read_col <= 9'd0;
                            read_row <= read_row + 1'b1;
                        end else
                            read_col <= read_col + 1'b1;
                    end else
                        read_channel <= read_channel + 1'b1;
                end
                if (body_accept && write_body_commit_ok) begin
                    body_data_reg <= s_body_data;
                    body_mask_reg <= s_body_mask;
                    body_lane <= 3'd0;
                    body_busy <= 1'b1;
                    write_input_bytes <= write_input_bytes + body_bytes;
                end
                if (body_accept) begin
                    debug_last_body_tag_reg <= s_body_tag;
                    debug_last_body_mask_reg <= s_body_mask;
                end
                if (write_lane_step) begin
                    if (body_lane == 3'd3)
                        body_busy <= 1'b0;
                    else
                        body_lane <= body_lane + 1'b1;
                    if (write_byte_step) begin
                        write_packed_bytes <= write_packed_bytes + 1'b1;
                        if (write_push) begin
                            if (write_state_commit_ok)
                                write_wr_ptr <= write_wr_ptr + 1'b1;
                            write_pack <= 64'd0;
                            write_fill <= 3'd0;
                        end else begin
                            write_pack <= packed_word;
                            write_fill <= write_fill + 1'b1;
                        end
                    end
                end
                if (write_pop && write_state_commit_ok) begin
                    write_rd_ptr <= write_rd_ptr + 1'b1;
                    write_valid_reg <= 1'b1;
                    write_last_reg <= write_output_bytes + 21'd8 >=
                                      {1'b0, dst_bytes_reg};
                    if (write_output_bytes + 21'd8 >= {1'b0, dst_bytes_reg})
                        write_keep_reg <= 8'hff >>
                            (21'd8 - ({1'b0, dst_bytes_reg} - write_output_bytes));
                    else
                        write_keep_reg <= 8'hff;
                end
                case ({write_push && write_state_commit_ok,
                       write_pop && write_state_commit_ok})
                    2'b10: write_count <= write_count + 1'b1;
                    2'b01: write_count <= write_count - 1'b1;
                    default: ;
                endcase
                if (write_accept) begin
                    write_valid_reg <= 1'b0;
                    if (write_last_reg) begin
                        write_output_bytes <= {1'b0, dst_bytes_reg};
                        final_write_accept_seen <= 1'b1;
                    end else
                        write_output_bytes <= write_output_bytes + 21'd8;
                end
            end
            if (fault_accept) begin
                debug_fault_reason_reg <= {
                    write_extra_input_fault,
                    write_body_count_fault,
                    body_tag_mismatch,
                    read_after_frame_fault,
                    read_underflow,
                    read_dma_done_fault,
                    read_extra_input_fault,
                    read_accept_fault,
                    dma_fault,
                    1'b0};
                fault_reg <= 1'b1;
                active <= 1'b0;
                done_reg <= 1'b0;
                pixel_valid_reg <= 1'b0;
                write_valid_reg <= 1'b0;
                read_done_reg <= read_done_reg;
                write_done_reg <= write_done_reg;
                operation_complete_seen <= operation_complete_seen;
                final_pixel_accept_seen <= final_pixel_accept_seen;
                final_write_accept_seen <= final_write_accept_seen;
                read_dma_done_seen <= read_dma_done_seen;
                write_dma_done_seen <= write_dma_done_seen;
            end
        end
    end
endmodule
