`timescale 1ns/1ps
`include "cnn_common_params.vh"

module weight_bram_swap_fsm (
    input  wire          clk,
    input  wire          rst_n,
    output reg           fault,

    input  wire          load_valid,
    output wire          load_ready,
    input  wire [255:0]  load_desc,
    output reg           load_done,

    input  wire [63:0]   s_dma_data,
    input  wire          s_dma_valid,
    output wire          s_dma_ready,
    input  wire          s_dma_last,
    input  wire [7:0]    s_dma_keep,

    input  wire          conv0_req_valid,
    output wire          conv0_req_ready,
    input  wire [4:0]    conv0_req_addr,
    output reg           conv0_rsp_valid,
    input  wire          conv0_rsp_ready,
    output reg  [215:0]  conv0_rsp_data,
    output reg  [63:0]   conv0_rsp_params,

    input  wire          dw_w_req_valid,
    output wire          dw_w_req_ready,
    input  wire [6:0]    dw_w_req_addr,
    output reg           dw_w_rsp_valid,
    input  wire          dw_w_rsp_ready,
    output reg  [255:0]  dw_w_rsp_data,

    input  wire          dw_p_req_valid,
    output wire          dw_p_req_ready,
    input  wire [8:0]    dw_p_req_addr,
    output reg           dw_p_rsp_valid,
    input  wire          dw_p_rsp_ready,
    output reg  [63:0]   dw_p_rsp_data,

    input  wire          pw_req_valid,
    output wire          pw_req_ready,
    input  wire [10:0]   pw_req_addr,
    output reg           pw_rsp_valid,
    input  wire          pw_rsp_ready,
    output reg  [1023:0] pw_rsp_data,
    output reg  [255:0]  pw_rsp_params,
    input  wire [6:0]    pw_req_group
);

    function [19:0] mul10x10_no_dsp;
        input [9:0] a;
        input [9:0] b;
        integer bit_index;
        reg [19:0] product;
        begin
            product = 20'd0;
            for (bit_index = 0; bit_index < 10; bit_index = bit_index + 1) begin
                if (b[bit_index])
                    product = product + ({10'd0, a} << bit_index);
            end
            mul10x10_no_dsp = product;
        end
    endfunction

    function [10:0] mul_batches_groups;
        input [3:0] batches;
        input [6:0] groups;
        reg [10:0] g;
        begin
            g = {4'd0, groups};
            case (batches)
                4'd0:  mul_batches_groups = 11'd0;
                4'd1:  mul_batches_groups = g;
                4'd2:  mul_batches_groups = g << 1;
                4'd3:  mul_batches_groups = (g << 1) + g;
                4'd4:  mul_batches_groups = g << 2;
                4'd5:  mul_batches_groups = (g << 2) + g;
                4'd6:  mul_batches_groups = (g << 2) + (g << 1);
                4'd7:  mul_batches_groups = (g << 3) - g;
                4'd8:  mul_batches_groups = g << 3;
                4'd9:  mul_batches_groups = (g << 3) + g;
                4'd10: mul_batches_groups = (g << 3) + (g << 1);
                4'd11: mul_batches_groups = (g << 3) + (g << 1) + g;
                4'd12: mul_batches_groups = (g << 3) + (g << 2);
                default: mul_batches_groups = 11'd0;
            endcase
        end
    endfunction

    localparam [2:0] STATE_IDLE   = 3'd0;
    localparam [2:0] STATE_CHECK  = 3'd1;
    localparam [2:0] STATE_LOAD   = 3'd2;
    localparam [2:0] STATE_COMMIT = 3'd3;
    localparam [2:0] STATE_FAULT  = 3'd4;

    localparam [1:0] LOAD_PHASE_WEIGHT  = 2'd0;
    localparam [1:0] LOAD_PHASE_PADDING = 2'd1;
    localparam [1:0] LOAD_PHASE_PARAM   = 2'd2;
    localparam [1:0] LOAD_PHASE_DONE    = 2'd3;

    localparam [19:0] CONV0_WEIGHT_BYTES = 20'd768;
    localparam [19:0] CONV0_PARAM_BYTES  = 20'd192;

    reg [2:0] state;
    /* The complete descriptor is retained by contract even though this
       module consumes only its load-related fields. */
    /* verilator lint_off UNUSEDSIGNAL */
    reg [255:0] desc_reg;
    reg [19:0] byte_count;
    reg [19:0] expected_weight_bytes;
    reg [19:0] expected_param_bytes;
    reg [19:0] expected_param_offset;
    reg [19:0] expected_dma_bytes;
    reg [10:0] expected_pw_word_count;
    reg [1:0] load_phase;
    reg [3:0] local_weight_bank;
    reg [10:0] local_weight_word_addr;
    reg [1:0] local_param_bank;
    reg [8:0] local_param_word_addr;
    reg [16:0] total_beats_remaining;
    reg [16:0] region_beats_remaining;
    reg [16:0] param_beats_total;
    reg [16:0] pre_padding_beats_total;
    reg padding_before_param;

    reg conv0_storage_valid;
    reg dw_storage_valid;
    reg pw_storage_valid;
    reg [4:0] conv0_loaded_op_id;
    reg [4:0] dw_loaded_op_id;
    reg [4:0] pw_loaded_op_id;
    reg [8:0] conv0_loaded_cin;
    reg [8:0] conv0_loaded_cout;
    reg [8:0] dw_loaded_cin;
    reg [8:0] dw_loaded_cout;
    reg [8:0] pw_loaded_cin;
    reg [8:0] pw_loaded_cout;
    reg [3:0] pw_loaded_batches;
    reg [6:0] pw_loaded_groups;
    reg [10:0] pw_weight_limit;
    /* verilator lint_on UNUSEDSIGNAL */

    reg [63:0] conv0_w_bank0 [0:23];
    reg [63:0] conv0_w_bank1 [0:23];
    reg [63:0] conv0_w_bank2 [0:23];
    reg [63:0] conv0_w_bank3 [0:23];
    reg [63:0] conv0_param_mem [0:23];

    reg [63:0] dw_w_bank0 [0:107];
    reg [63:0] dw_w_bank1 [0:107];
    reg [63:0] dw_w_bank2 [0:107];
    reg [63:0] dw_w_bank3 [0:107];
    reg [63:0] dw_param_mem [0:383];

    reg [63:0] pw_w_bank0  [0:1151];
    reg [63:0] pw_w_bank1  [0:1151];
    reg [63:0] pw_w_bank2  [0:1151];
    reg [63:0] pw_w_bank3  [0:1151];
    reg [63:0] pw_w_bank4  [0:1151];
    reg [63:0] pw_w_bank5  [0:1151];
    reg [63:0] pw_w_bank6  [0:1151];
    reg [63:0] pw_w_bank7  [0:1151];
    reg [63:0] pw_w_bank8  [0:1151];
    reg [63:0] pw_w_bank9  [0:1151];
    reg [63:0] pw_w_bank10 [0:1151];
    reg [63:0] pw_w_bank11 [0:1151];
    reg [63:0] pw_w_bank12 [0:1151];
    reg [63:0] pw_w_bank13 [0:1151];
    reg [63:0] pw_w_bank14 [0:1151];
    reg [63:0] pw_w_bank15 [0:1151];
    reg [63:0] pw_param_bank0 [0:95];
    reg [63:0] pw_param_bank1 [0:95];
    reg [63:0] pw_param_bank2 [0:95];
    reg [63:0] pw_param_bank3 [0:95];

    wire [4:0] desc_op_id = desc_reg[`CFG_OP_ID_MSB:`CFG_OP_ID_LSB];
    wire [1:0] desc_kind = desc_reg[`CFG_KIND_MSB:`CFG_KIND_LSB];
    wire [8:0] desc_cin = desc_reg[`CFG_CIN_MSB:`CFG_CIN_LSB];
    wire [8:0] desc_cout = desc_reg[`CFG_COUT_MSB:`CFG_COUT_LSB];
    wire [17:0] desc_param_offset = desc_reg[`CFG_PARAM_OFFSET_MSB:`CFG_PARAM_OFFSET_LSB];
    wire [19:0] desc_dma_bytes = desc_reg[`CFG_DMA_BYTES_MSB:`CFG_DMA_BYTES_LSB];
    wire [106:0] desc_reserved = desc_reg[`CFG_RESERVED_MSB:`CFG_RESERVED_LSB];

    /* Ceiling divisions use the already-aligned high field plus a low-bit
       nonzero flag.  This avoids carrying through ten bits before the
       descriptor-local multiply. */
    wire [9:0] desc_in_batches =
        {5'd0, desc_cin[8:5]} + {9'd0, (|desc_cin[4:0])};
    wire [9:0] desc_out_groups =
        {2'd0, desc_cout[8:2]} + {9'd0, (|desc_cout[1:0])};

    /* Descriptor geometry is derived only after load_desc has been captured
       in desc_reg.  STATE_CHECK validates and captures these values on the
       same edge that advances to STATE_LOAD, preserving the external load
       latency while removing the direct top descriptor path. */
    /* A small balanced LUT multiplier is shallower here than the historical
       13-way case mux; keep it out of DSP48 resources. */
    (* use_dsp = "no" *) wire [10:0] check_pw_word_count =
        desc_in_batches[3:0] * desc_out_groups[6:0];
    wire [19:0] check_dw_weight_bytes =
        ({10'd0, desc_in_batches} << 8) +
        ({10'd0, desc_in_batches} << 5);
    wire [19:0] check_pw_weight_bytes =
        {2'd0, check_pw_word_count, 7'd0};
    wire [19:0] check_dw_param_bytes = {11'd0, desc_cout} << 3;
    wire [19:0] check_pw_param_bytes = {10'd0, desc_out_groups} << 5;

    wire [7:0] check_dw_nine_batches =
        {4'd0, desc_in_batches[3:0]} +
        ({4'd0, desc_in_batches[3:0]} << 3);
    wire [7:0] check_dw_param_offset_units =
        (check_dw_nine_batches + 8'd1) >> 1;
    wire [9:0] check_dw_param_units =
        {3'd0, desc_cout[8:3]} + {9'd0, (|desc_cout[2:0])};
    wire [10:0] check_dw_dma_units =
        {3'd0, check_dw_param_offset_units} + {1'b0, check_dw_param_units};
    wire [19:0] check_dw_param_offset =
        {6'd0, check_dw_param_offset_units, 6'd0};
    wire [19:0] check_dw_dma_bytes = {3'd0, check_dw_dma_units, 6'd0};

    wire [7:0] check_pw_param_units =
        {2'd0, desc_out_groups[6:1]} + {7'd0, desc_out_groups[0]};
    wire [11:0] check_pw_dma_units =
        {check_pw_word_count, 1'b0} + {4'd0, check_pw_param_units};
    wire [19:0] check_pw_dma_bytes = {2'd0, check_pw_dma_units, 6'd0};

    wire [19:0] check_calc_weight_bytes =
        (desc_kind == `CNN_KIND_CONV0) ? CONV0_WEIGHT_BYTES :
        (desc_kind == `CNN_KIND_DEPTHWISE) ? check_dw_weight_bytes :
        (desc_kind == `CNN_KIND_POINTWISE) ? check_pw_weight_bytes : 20'd0;
    wire [19:0] check_calc_param_bytes =
        (desc_kind == `CNN_KIND_CONV0) ? CONV0_PARAM_BYTES :
        (desc_kind == `CNN_KIND_DEPTHWISE) ? check_dw_param_bytes :
        (desc_kind == `CNN_KIND_POINTWISE) ? check_pw_param_bytes : 20'd0;
    wire [19:0] check_calc_param_offset =
        (desc_kind == `CNN_KIND_CONV0) ? CONV0_WEIGHT_BYTES :
        (desc_kind == `CNN_KIND_DEPTHWISE) ? check_dw_param_offset :
        (desc_kind == `CNN_KIND_POINTWISE) ? check_pw_weight_bytes : 20'd0;
    wire [19:0] check_calc_dma_bytes =
        (desc_kind == `CNN_KIND_CONV0) ? 20'd960 :
        (desc_kind == `CNN_KIND_DEPTHWISE) ? check_dw_dma_bytes :
        (desc_kind == `CNN_KIND_POINTWISE) ? check_pw_dma_bytes : 20'd0;
    wire [19:0] check_pre_padding_bytes =
        check_calc_param_offset - check_calc_weight_bytes;
    wire [16:0] check_weight_beats = check_calc_weight_bytes[19:3];
    wire [16:0] check_param_beats = check_calc_param_bytes[19:3];
    wire [16:0] check_total_beats = check_calc_dma_bytes[19:3];
    wire [16:0] check_pre_padding_beats = check_pre_padding_bytes[19:3];
    wire [13:0] check_dw_dma_units_from_desc_offset =
        {2'd0, desc_param_offset[17:6]} + {4'd0, check_dw_param_units};
    wire [13:0] check_pw_dma_units_from_desc_offset =
        {2'd0, desc_param_offset[17:6]} + {6'd0, check_pw_param_units};

    reg desc_error;

    always @(*) begin
        desc_error = 1'b0;
        if (desc_op_id >= `CNN_OPS)
            desc_error = 1'b1;
        if (desc_reserved != 107'd0)
            desc_error = 1'b1;
        if ((desc_kind != `CNN_KIND_CONV0) &&
            (desc_kind != `CNN_KIND_DEPTHWISE) &&
            (desc_kind != `CNN_KIND_POINTWISE))
            desc_error = 1'b1;
        case (desc_kind)
            `CNN_KIND_CONV0: begin
                if ((desc_op_id != `CNN_OP_CONV0) ||
                    (desc_cin != 9'd3) || (desc_cout != 9'd24) ||
                    ({2'b00, desc_param_offset} != 20'd768) ||
                    (desc_dma_bytes != 20'd960))
                    desc_error = 1'b1;
            end
            `CNN_KIND_DEPTHWISE: begin
                if ((desc_op_id[0] != 1'b1) || (desc_op_id > `CNN_OP_CONV13_DW) ||
                    (desc_cin == 9'd0) || (desc_cin > 9'd384) ||
                    (desc_cout != desc_cin) || (desc_in_batches > 10'd12) ||
                    (desc_param_offset[5:0] != 6'd0) ||
                    (desc_param_offset[17:6] != check_dw_param_offset_units) ||
                    (desc_dma_bytes[5:0] != 6'd0) ||
                    (desc_dma_bytes[19:6] != check_dw_dma_units_from_desc_offset))
                    desc_error = 1'b1;
            end
            `CNN_KIND_POINTWISE: begin
                if (((desc_op_id < `CNN_OP_CONV1_PW) ||
                     ((desc_op_id <= `CNN_OP_CONV13_PW) && (desc_op_id[0] != 1'b0))) ||
                    (desc_cin == 9'd0) || (desc_cin > 9'd384) ||
                    (desc_cout == 9'd0) || (desc_cout > 9'd384) ||
                    (desc_in_batches > 10'd12) || (desc_out_groups > 10'd96) ||
                    /* The two dimension bounds imply word_count<=12*96,
                       so a second wide magnitude comparator is redundant. */
                    (desc_param_offset[6:0] != 7'd0) ||
                    (desc_param_offset[17:7] != check_pw_word_count) ||
                    (desc_dma_bytes[5:0] != 6'd0) ||
                    (desc_dma_bytes[19:6] != check_pw_dma_units_from_desc_offset))
                    desc_error = 1'b1;
            end
            default: desc_error = 1'b1;
        endcase
    end

    wire any_req_present = conv0_req_valid | dw_w_req_valid |
                           dw_p_req_valid | pw_req_valid;
    wire any_rsp_pending = conv0_rsp_valid | dw_w_rsp_valid |
                           dw_p_rsp_valid | pw_rsp_valid;

    assign load_ready = rst_n && !fault && (state == STATE_IDLE) &&
                        !any_req_present && !any_rsp_pending;
    assign s_dma_ready = rst_n && !fault && (state == STATE_LOAD);

    wire conv0_slot_available = !conv0_rsp_valid || conv0_rsp_ready;
    wire dw_w_slot_available = !dw_w_rsp_valid || dw_w_rsp_ready;
    wire dw_p_slot_available = !dw_p_rsp_valid || dw_p_rsp_ready;
    wire pw_slot_available = !pw_rsp_valid || pw_rsp_ready;

    assign conv0_req_ready = rst_n && !fault && (state == STATE_IDLE) &&
                             conv0_storage_valid && conv0_slot_available;
    assign dw_w_req_ready = rst_n && !fault && (state == STATE_IDLE) &&
                            dw_storage_valid && dw_w_slot_available;
    assign dw_p_req_ready = rst_n && !fault && (state == STATE_IDLE) &&
                            dw_storage_valid && dw_p_slot_available;
    assign pw_req_ready = rst_n && !fault && (state == STATE_IDLE) &&
                          pw_storage_valid && pw_slot_available;

    wire load_accept = load_valid && load_ready;
    wire dma_accept = s_dma_valid && s_dma_ready;
    wire conv0_req_accept = conv0_req_valid && conv0_req_ready;
    wire dw_w_req_accept = dw_w_req_valid && dw_w_req_ready;
    wire dw_p_req_accept = dw_p_req_valid && dw_p_req_ready;
    wire pw_req_accept = pw_req_valid && pw_req_ready;

    wire [9:0] dw_loaded_batches = ({1'b0, dw_loaded_cin} + 10'd31) >> 5;
    wire [9:0] dw_dummy_limit = dw_loaded_batches << 5;
    wire [19:0] dw_weight_limit = {10'd0, dw_loaded_batches} +
                                  ({10'd0, dw_loaded_batches} << 3);
    wire [10:0] pw_group_base =
        mul10x10_no_dsp({3'd0, pw_req_group}, {6'd0, pw_loaded_batches});
    wire [10:0] pw_group_limit = pw_group_base + {7'd0, pw_loaded_batches};

    wire conv0_addr_error = conv0_req_accept &&
                            ({4'd0, conv0_req_addr} >= conv0_loaded_cout);
    wire dw_w_addr_error = dw_w_req_accept &&
                           ({13'd0, dw_w_req_addr} >= dw_weight_limit);
    wire dw_p_addr_error = dw_p_req_accept &&
                           ({1'b0, dw_p_req_addr} >= dw_dummy_limit);
    wire pw_addr_error = pw_req_accept &&
                         ((pw_req_addr >= pw_weight_limit) ||
                          (pw_req_group >= pw_loaded_groups) ||
                          (pw_req_addr < pw_group_base) ||
                          (pw_req_addr >= pw_group_limit));
    /* P1-B2: defensive physical bound for the pw_param_bank0..3 array read.
       Each bank is declared [0:95] but pw_req_group is 7 bits (0..127), so
       this guards the array index independently of pw_addr_error. Because
       pw_loaded_groups is always <=96 (STATE_CHECK rejects
       desc_out_groups>96), pw_req_group<pw_loaded_groups already implies
       pw_req_group<96, so this bound never disagrees with the existing
       pw_addr_error group check - it only removes the pw_group_base/
       pw_group_limit (mul10x10_no_dsp) compare chain from the parameter
       bank's read-enable fan-in, matching the split already used for
       pw_w_bank0..15 above. */
    wire pw_param_index_in_range = (pw_req_group < 7'd96);
    wire read_error = conv0_addr_error | dw_w_addr_error |
                      dw_p_addr_error | pw_addr_error;

    wire [20:0] dma_next_count = {1'b0, byte_count} + 21'd8;
    wire dma_in_weight = (load_phase == LOAD_PHASE_WEIGHT);
    wire dma_in_param = (load_phase == LOAD_PHASE_PARAM);
    wire dma_write_phase = dma_in_weight || dma_in_param;
    wire dma_local_final = (total_beats_remaining == 17'd1);
    wire dma_local_overflow = (total_beats_remaining == 17'd0);
    wire dma_param_format_ok = !dma_in_param ||
        ((s_dma_data[31:24] == (s_dma_data[23] ? 8'hff : 8'h00)) &&
         (s_dma_data[63:49] == 15'd0));
    wire dma_current_beat_legal = (s_dma_keep == 8'hff) &&
        !dma_local_overflow && (s_dma_last == dma_local_final) &&
        dma_param_format_ok;
    wire dma_wrong_keep = dma_accept && (s_dma_keep != 8'hff);
    /* Keep byte_count as an independent status/evidence counter.  A mismatch
       can only arise from internal state corruption (normal progress updates
       both counters together), and remains sticky-fault visible for legacy
       diagnostics.  It is deliberately excluded from dma_write_enable and
       every physical bank/address path. */
    wire dma_status_count_overflow = dma_accept &&
        (dma_next_count > {1'b0, expected_dma_bytes});
    wire dma_count_overflow = (dma_accept && dma_local_overflow) ||
                              dma_status_count_overflow;
    wire dma_early_last = dma_accept && s_dma_last && !dma_local_final;
    wire dma_missing_last = dma_accept && !s_dma_last && dma_local_final;
    wire dma_bias_error = dma_accept && dma_in_param &&
                          (s_dma_data[31:24] != (s_dma_data[23] ? 8'hff : 8'h00));
    wire dma_m_error = dma_accept && dma_in_param &&
                       (s_dma_data[63:49] != 15'd0);
    wire dma_error = (dma_accept && !dma_current_beat_legal) ||
                     dma_status_count_overflow;
    wire dma_final_good = dma_accept && dma_current_beat_legal &&
                          dma_local_final;
    wire dma_write_enable = dma_accept && dma_current_beat_legal &&
                            dma_write_phase;

    always @(posedge clk) begin
        if (!rst_n) begin
            state <= STATE_IDLE;
            fault <= 1'b0;
            load_done <= 1'b0;
            desc_reg <= 256'd0;
            byte_count <= 20'd0;
            expected_weight_bytes <= 20'd0;
            expected_param_bytes <= 20'd0;
            expected_param_offset <= 20'd0;
            expected_dma_bytes <= 20'd0;
            expected_pw_word_count <= 11'd0;
            load_phase <= LOAD_PHASE_DONE;
            local_weight_bank <= 4'd0;
            local_weight_word_addr <= 11'd0;
            local_param_bank <= 2'd0;
            local_param_word_addr <= 9'd0;
            total_beats_remaining <= 17'd0;
            region_beats_remaining <= 17'd0;
            param_beats_total <= 17'd0;
            pre_padding_beats_total <= 17'd0;
            padding_before_param <= 1'b0;
            conv0_storage_valid <= 1'b0;
            dw_storage_valid <= 1'b0;
            pw_storage_valid <= 1'b0;
            conv0_loaded_op_id <= 5'd0;
            dw_loaded_op_id <= 5'd0;
            pw_loaded_op_id <= 5'd0;
            conv0_loaded_cin <= 9'd0;
            conv0_loaded_cout <= 9'd0;
            dw_loaded_cin <= 9'd0;
            dw_loaded_cout <= 9'd0;
            pw_loaded_cin <= 9'd0;
            pw_loaded_cout <= 9'd0;
            pw_loaded_batches <= 4'd0;
            pw_loaded_groups <= 7'd0;
            pw_weight_limit <= 11'd0;
            conv0_rsp_valid <= 1'b0;
            dw_w_rsp_valid <= 1'b0;
            dw_p_rsp_valid <= 1'b0;
            pw_rsp_valid <= 1'b0;
            conv0_rsp_data <= 216'd0;
            conv0_rsp_params <= 64'd0;
            dw_w_rsp_data <= 256'd0;
            dw_p_rsp_data <= 64'd0;
            pw_rsp_data <= 1024'd0;
            pw_rsp_params <= 256'd0;
        end else begin
            load_done <= 1'b0;

            if (conv0_req_accept && !conv0_addr_error) begin
                conv0_rsp_valid <= 1'b1;
                conv0_rsp_data <= {conv0_w_bank3[conv0_req_addr][23:0],
                                   conv0_w_bank2[conv0_req_addr],
                                   conv0_w_bank1[conv0_req_addr],
                                   conv0_w_bank0[conv0_req_addr]};
                conv0_rsp_params <= conv0_param_mem[conv0_req_addr];
            end else if (conv0_rsp_valid && conv0_rsp_ready) begin
                conv0_rsp_valid <= 1'b0;
            end

            if (dw_w_req_accept && !dw_w_addr_error) begin
                dw_w_rsp_valid <= 1'b1;
                dw_w_rsp_data <= {dw_w_bank3[dw_w_req_addr],
                                  dw_w_bank2[dw_w_req_addr],
                                  dw_w_bank1[dw_w_req_addr],
                                  dw_w_bank0[dw_w_req_addr]};
            end else if (dw_w_rsp_valid && dw_w_rsp_ready) begin
                dw_w_rsp_valid <= 1'b0;
            end

            if (dw_p_req_accept && !dw_p_addr_error) begin
                dw_p_rsp_valid <= 1'b1;
                if ({1'b0, dw_p_req_addr} < {1'b0, dw_loaded_cin})
                    dw_p_rsp_data <= dw_param_mem[dw_p_req_addr];
                else
                    dw_p_rsp_data <= 64'd0;
            end else if (dw_p_rsp_valid && dw_p_rsp_ready) begin
                dw_p_rsp_valid <= 1'b0;
            end

            /* Issue the BRAM data read from the accepted request alone.  Address
               validation still gates the public response and raises the same
               sticky fault, but no longer feeds every PW BRAM enable pin. */
            if (pw_req_accept) begin
                pw_rsp_data <= {pw_w_bank15[pw_req_addr], pw_w_bank14[pw_req_addr],
                                pw_w_bank13[pw_req_addr], pw_w_bank12[pw_req_addr],
                                pw_w_bank11[pw_req_addr], pw_w_bank10[pw_req_addr],
                                pw_w_bank9[pw_req_addr],  pw_w_bank8[pw_req_addr],
                                pw_w_bank7[pw_req_addr],  pw_w_bank6[pw_req_addr],
                                pw_w_bank5[pw_req_addr],  pw_w_bank4[pw_req_addr],
                                pw_w_bank3[pw_req_addr],  pw_w_bank2[pw_req_addr],
                                pw_w_bank1[pw_req_addr],  pw_w_bank0[pw_req_addr]};
            end

            /* P1-B2: parameter-bank array read is gated by pw_req_accept
               and the defensive pw_param_index_in_range bound ALONE, so the
               pw_addr_error (pw_group_base/pw_group_limit, mul10x10_no_dsp)
               compare chain is removed from this array's read-enable/
               ENARDEN fan-in - mirroring the pw_w_bank* split above
               (L426-438). Public response validity keeps the original,
               unchanged pw_addr_error-gated contract below, so invalid
               requests (including group 96..127, which always also fails
               pw_addr_error) never assert pw_rsp_valid. */
            if (pw_req_accept && pw_param_index_in_range) begin
                pw_rsp_params <= {pw_param_bank3[pw_req_group],
                                  pw_param_bank2[pw_req_group],
                                  pw_param_bank1[pw_req_group],
                                  pw_param_bank0[pw_req_group]};
            end

            if (pw_req_accept && !pw_addr_error) begin
                pw_rsp_valid <= 1'b1;
            end else if (pw_rsp_valid && pw_rsp_ready) begin
                pw_rsp_valid <= 1'b0;
            end

                case (state)
                    STATE_IDLE: begin
                        if (read_error) begin
                            fault <= 1'b1;
                            state <= STATE_FAULT;
                        end else if (load_accept) begin
                            desc_reg <= load_desc;
                            byte_count <= 20'd0;
                            case (load_desc[`CFG_KIND_MSB:`CFG_KIND_LSB])
                                `CNN_KIND_CONV0: conv0_storage_valid <= 1'b0;
                                `CNN_KIND_DEPTHWISE: dw_storage_valid <= 1'b0;
                                `CNN_KIND_POINTWISE: pw_storage_valid <= 1'b0;
                                default: begin end
                            endcase
                            state <= STATE_CHECK;
                        end
                    end

                    STATE_CHECK: begin
                        /* Capture the descriptor-local geometry on every CHECK
                           edge.  For an invalid descriptor these private values
                           are unreachable after the simultaneous FAULT entry;
                           keeping capture unconditional prevents the full
                           desc_error reduction from becoming a CE path into
                           every local-state register. */
                        expected_weight_bytes <= check_calc_weight_bytes;
                        expected_param_bytes <= check_calc_param_bytes;
                        expected_param_offset <= check_calc_param_offset;
                        expected_dma_bytes <= check_calc_dma_bytes;
                        expected_pw_word_count <= check_pw_word_count;
                        load_phase <= LOAD_PHASE_WEIGHT;
                        local_weight_bank <= 4'd0;
                        local_weight_word_addr <= 11'd0;
                        local_param_bank <= 2'd0;
                        local_param_word_addr <= 9'd0;
                        total_beats_remaining <= check_total_beats;
                        region_beats_remaining <= check_weight_beats;
                        param_beats_total <= check_param_beats;
                        pre_padding_beats_total <= check_pre_padding_beats;
                        padding_before_param <= 1'b0;
                        if (desc_error) begin
                            fault <= 1'b1;
                            state <= STATE_FAULT;
                        end else begin
                            state <= STATE_LOAD;
                        end
                    end

                    STATE_LOAD: begin
                        if (dma_accept)
                            byte_count <= dma_next_count[19:0];
                        if (dma_error) begin
                            fault <= 1'b1;
                            state <= STATE_FAULT;
                        end else if (dma_final_good) begin
                            state <= STATE_COMMIT;
                        end

                        if (dma_accept && dma_current_beat_legal) begin
                            total_beats_remaining <= total_beats_remaining - 17'd1;
                            if (dma_local_final) begin
                                load_phase <= LOAD_PHASE_DONE;
                                region_beats_remaining <= 17'd0;
                            end else begin
                                case (load_phase)
                                    LOAD_PHASE_WEIGHT: begin
                                        if (region_beats_remaining == 17'd1) begin
                                            if (pre_padding_beats_total != 17'd0) begin
                                                load_phase <= LOAD_PHASE_PADDING;
                                                region_beats_remaining <= pre_padding_beats_total;
                                                padding_before_param <= 1'b1;
                                            end else begin
                                                load_phase <= LOAD_PHASE_PARAM;
                                                region_beats_remaining <= param_beats_total;
                                                padding_before_param <= 1'b0;
                                            end
                                        end else begin
                                            region_beats_remaining <= region_beats_remaining - 17'd1;
                                        end
                                    end
                                    LOAD_PHASE_PADDING: begin
                                        if (region_beats_remaining == 17'd1) begin
                                            if (padding_before_param) begin
                                                load_phase <= LOAD_PHASE_PARAM;
                                                region_beats_remaining <= param_beats_total;
                                                padding_before_param <= 1'b0;
                                            end else begin
                                                load_phase <= LOAD_PHASE_DONE;
                                                region_beats_remaining <= 17'd0;
                                            end
                                        end else begin
                                            region_beats_remaining <= region_beats_remaining - 17'd1;
                                        end
                                    end
                                    LOAD_PHASE_PARAM: begin
                                        if (region_beats_remaining == 17'd1) begin
                                            load_phase <= LOAD_PHASE_PADDING;
                                            region_beats_remaining <= total_beats_remaining - 17'd1;
                                            padding_before_param <= 1'b0;
                                        end else begin
                                            region_beats_remaining <= region_beats_remaining - 17'd1;
                                        end
                                    end
                                    default: begin
                                        load_phase <= LOAD_PHASE_DONE;
                                        region_beats_remaining <= 17'd0;
                                    end
                                endcase
                            end
                        end

                        if (dma_write_enable) begin
                            case (desc_kind)
                                `CNN_KIND_CONV0: begin
                                    if (dma_in_weight) begin
                                        case (local_weight_bank[1:0])
                                            2'd0: conv0_w_bank0[local_weight_word_addr[4:0]] <= s_dma_data;
                                            2'd1: conv0_w_bank1[local_weight_word_addr[4:0]] <= s_dma_data;
                                            2'd2: conv0_w_bank2[local_weight_word_addr[4:0]] <= s_dma_data;
                                            2'd3: conv0_w_bank3[local_weight_word_addr[4:0]] <= s_dma_data;
                                        endcase
                                    end else begin
                                        conv0_param_mem[local_param_word_addr[4:0]] <= s_dma_data;
                                    end
                                end
                                `CNN_KIND_DEPTHWISE: begin
                                    if (dma_in_weight) begin
                                        case (local_weight_bank[1:0])
                                            2'd0: dw_w_bank0[local_weight_word_addr[6:0]] <= s_dma_data;
                                            2'd1: dw_w_bank1[local_weight_word_addr[6:0]] <= s_dma_data;
                                            2'd2: dw_w_bank2[local_weight_word_addr[6:0]] <= s_dma_data;
                                            2'd3: dw_w_bank3[local_weight_word_addr[6:0]] <= s_dma_data;
                                        endcase
                                    end else begin
                                        dw_param_mem[local_param_word_addr] <= s_dma_data;
                                    end
                                end
                                `CNN_KIND_POINTWISE: begin
                                    if (dma_in_weight) begin
                                        case (local_weight_bank)
                                            4'd0:  pw_w_bank0[local_weight_word_addr] <= s_dma_data;
                                            4'd1:  pw_w_bank1[local_weight_word_addr] <= s_dma_data;
                                            4'd2:  pw_w_bank2[local_weight_word_addr] <= s_dma_data;
                                            4'd3:  pw_w_bank3[local_weight_word_addr] <= s_dma_data;
                                            4'd4:  pw_w_bank4[local_weight_word_addr] <= s_dma_data;
                                            4'd5:  pw_w_bank5[local_weight_word_addr] <= s_dma_data;
                                            4'd6:  pw_w_bank6[local_weight_word_addr] <= s_dma_data;
                                            4'd7:  pw_w_bank7[local_weight_word_addr] <= s_dma_data;
                                            4'd8:  pw_w_bank8[local_weight_word_addr] <= s_dma_data;
                                            4'd9:  pw_w_bank9[local_weight_word_addr] <= s_dma_data;
                                            4'd10: pw_w_bank10[local_weight_word_addr] <= s_dma_data;
                                            4'd11: pw_w_bank11[local_weight_word_addr] <= s_dma_data;
                                            4'd12: pw_w_bank12[local_weight_word_addr] <= s_dma_data;
                                            4'd13: pw_w_bank13[local_weight_word_addr] <= s_dma_data;
                                            4'd14: pw_w_bank14[local_weight_word_addr] <= s_dma_data;
                                            4'd15: pw_w_bank15[local_weight_word_addr] <= s_dma_data;
                                        endcase
                                    end else begin
                                        case (local_param_bank)
                                            2'd0: pw_param_bank0[local_param_word_addr] <= s_dma_data;
                                            2'd1: pw_param_bank1[local_param_word_addr] <= s_dma_data;
                                            2'd2: pw_param_bank2[local_param_word_addr] <= s_dma_data;
                                            2'd3: pw_param_bank3[local_param_word_addr] <= s_dma_data;
                                        endcase
                                    end
                                end
                                default: begin end
                            endcase

                            if (dma_in_weight) begin
                                if (((desc_kind == `CNN_KIND_POINTWISE) &&
                                     (local_weight_bank == 4'd15)) ||
                                    ((desc_kind != `CNN_KIND_POINTWISE) &&
                                     (local_weight_bank[1:0] == 2'd3))) begin
                                    local_weight_bank <= 4'd0;
                                    local_weight_word_addr <= local_weight_word_addr + 11'd1;
                                end else begin
                                    local_weight_bank <= local_weight_bank + 4'd1;
                                end
                            end else begin
                                if (desc_kind == `CNN_KIND_POINTWISE) begin
                                    if (local_param_bank == 2'd3) begin
                                        local_param_bank <= 2'd0;
                                        local_param_word_addr <= local_param_word_addr + 9'd1;
                                    end else begin
                                        local_param_bank <= local_param_bank + 2'd1;
                                    end
                                end else begin
                                    local_param_word_addr <= local_param_word_addr + 9'd1;
                                end
                            end
                        end
                    end

                    STATE_COMMIT: begin
                        case (desc_kind)
                            `CNN_KIND_CONV0: begin
                                conv0_storage_valid <= 1'b1;
                                conv0_loaded_op_id <= desc_op_id;
                                conv0_loaded_cin <= desc_cin;
                                conv0_loaded_cout <= desc_cout;
                            end
                            `CNN_KIND_DEPTHWISE: begin
                                dw_storage_valid <= 1'b1;
                                dw_loaded_op_id <= desc_op_id;
                                dw_loaded_cin <= desc_cin;
                                dw_loaded_cout <= desc_cout;
                            end
                            `CNN_KIND_POINTWISE: begin
                                pw_storage_valid <= 1'b1;
                                pw_loaded_op_id <= desc_op_id;
                                pw_loaded_cin <= desc_cin;
                                pw_loaded_cout <= desc_cout;
                                pw_loaded_batches <= desc_in_batches[3:0];
                                pw_loaded_groups <= desc_out_groups[6:0];
                                pw_weight_limit <= expected_weight_bytes[17:7];
                            end
                            default: begin end
                        endcase
                        load_done <= 1'b1;
                        state <= STATE_IDLE;
                    end

                    STATE_FAULT: begin
                        fault <= 1'b1;
                        state <= STATE_FAULT;
                    end

                    default: begin
                        fault <= 1'b1;
                        state <= STATE_FAULT;
                    end
                endcase
        end
    end

endmodule
