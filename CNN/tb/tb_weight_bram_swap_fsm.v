`timescale 1ns / 1ps
`include "cnn_common_params.vh"

module tb_weight_bram_swap_fsm;
    reg clk;
    reg rst_n;
    wire fault;
    reg load_valid;
    wire load_ready;
    reg [255:0] load_desc;
    wire load_done;
    reg [63:0] s_dma_data;
    reg s_dma_valid;
    wire s_dma_ready;
    reg s_dma_last;
    reg [7:0] s_dma_keep;
    reg conv0_req_valid;
    wire conv0_req_ready;
    reg [4:0] conv0_req_addr;
    wire conv0_rsp_valid;
    reg conv0_rsp_ready;
    wire [215:0] conv0_rsp_data;
    wire [63:0] conv0_rsp_params;
    reg dw_w_req_valid;
    wire dw_w_req_ready;
    reg [6:0] dw_w_req_addr;
    wire dw_w_rsp_valid;
    reg dw_w_rsp_ready;
    wire [255:0] dw_w_rsp_data;
    reg dw_p_req_valid;
    wire dw_p_req_ready;
    reg [8:0] dw_p_req_addr;
    wire dw_p_rsp_valid;
    reg dw_p_rsp_ready;
    wire [63:0] dw_p_rsp_data;
    reg pw_req_valid;
    wire pw_req_ready;
    reg [10:0] pw_req_addr;
    wire pw_rsp_valid;
    reg pw_rsp_ready;
    wire [1023:0] pw_rsp_data;
    wire [255:0] pw_rsp_params;
    reg [6:0] pw_req_group;

    integer pass_count;
    integer fail_count;
    integer cycle_count;
    integer i;
    integer total_beats;
    reg [215:0] held216;
    reg [255:0] desc_tmp;

    weight_bram_swap_fsm dut (
        .clk(clk),
        .rst_n(rst_n),
        .fault(fault),
        .load_valid(load_valid),
        .load_ready(load_ready),
        .load_desc(load_desc),
        .load_done(load_done),
        .s_dma_data(s_dma_data),
        .s_dma_valid(s_dma_valid),
        .s_dma_ready(s_dma_ready),
        .s_dma_last(s_dma_last),
        .s_dma_keep(s_dma_keep),
        .conv0_req_valid(conv0_req_valid),
        .conv0_req_ready(conv0_req_ready),
        .conv0_req_addr(conv0_req_addr),
        .conv0_rsp_valid(conv0_rsp_valid),
        .conv0_rsp_ready(conv0_rsp_ready),
        .conv0_rsp_data(conv0_rsp_data),
        .conv0_rsp_params(conv0_rsp_params),
        .dw_w_req_valid(dw_w_req_valid),
        .dw_w_req_ready(dw_w_req_ready),
        .dw_w_req_addr(dw_w_req_addr),
        .dw_w_rsp_valid(dw_w_rsp_valid),
        .dw_w_rsp_ready(dw_w_rsp_ready),
        .dw_w_rsp_data(dw_w_rsp_data),
        .dw_p_req_valid(dw_p_req_valid),
        .dw_p_req_ready(dw_p_req_ready),
        .dw_p_req_addr(dw_p_req_addr),
        .dw_p_rsp_valid(dw_p_rsp_valid),
        .dw_p_rsp_ready(dw_p_rsp_ready),
        .dw_p_rsp_data(dw_p_rsp_data),
        .pw_req_valid(pw_req_valid),
        .pw_req_ready(pw_req_ready),
        .pw_req_addr(pw_req_addr),
        .pw_rsp_valid(pw_rsp_valid),
        .pw_rsp_ready(pw_rsp_ready),
        .pw_rsp_data(pw_rsp_data),
        .pw_rsp_params(pw_rsp_params),
        .pw_req_group(pw_req_group)
    );

    always #5 clk = ~clk;
    always @(posedge clk) cycle_count <= cycle_count + 1;

    task check;
        input condition;
        input [8*96-1:0] name;
        begin
            if (condition) begin
                pass_count = pass_count + 1;
                $display("PASS cycle=%0d %0s", cycle_count, name);
            end else begin
                fail_count = fail_count + 1;
                $display("FAIL cycle=%0d %0s", cycle_count, name);
            end
        end
    endtask

    function [255:0] make_desc;
        input [4:0] op_id;
        input [1:0] kind;
        input [8:0] cin;
        input [8:0] cout;
        input [17:0] param_offset;
        input [19:0] dma_bytes;
        reg [255:0] d;
        begin
            d = 256'd0;
            d[`CFG_OP_ID_MSB:`CFG_OP_ID_LSB] = op_id;
            d[`CFG_KIND_MSB:`CFG_KIND_LSB] = kind;
            d[`CFG_CIN_MSB:`CFG_CIN_LSB] = cin;
            d[`CFG_COUT_MSB:`CFG_COUT_LSB] = cout;
            d[`CFG_PARAM_OFFSET_MSB:`CFG_PARAM_OFFSET_LSB] = param_offset;
            d[`CFG_DMA_BYTES_MSB:`CFG_DMA_BYTES_LSB] = dma_bytes;
            make_desc = d;
        end
    endfunction

    function [63:0] weight_word;
        input [1:0] kind;
        input [19:0] word_index;
        begin
            case (kind)
                `CNN_KIND_CONV0:
                weight_word = 64'hc000000000000000 | {44'd0, word_index};
                `CNN_KIND_DEPTHWISE:
                weight_word = 64'hd000000000000000 | {44'd0, word_index};
                default:
                weight_word = 64'he000000000000000 | {44'd0, word_index};
            endcase
        end
    endfunction

    function [63:0] param_word;
        input [19:0] record_index;
        begin
            param_word = {12'd0, record_index, 8'd0, 4'd0, record_index};
        end
    endfunction

    function [215:0] conv0_record;
        input [4:0] record_index;
        reg [19:0] base_word;
        reg [63:0] w0;
        reg [63:0] w1;
        reg [63:0] w2;
        reg [23:0] top24;
        begin
            base_word = record_index * 4;
            w0 = weight_word(`CNN_KIND_CONV0, base_word);
            w1 = weight_word(`CNN_KIND_CONV0, base_word + 1);
            w2 = weight_word(`CNN_KIND_CONV0, base_word + 2);
            top24 = {4'd0, base_word} + 24'd3;
            conv0_record = {top24, w2, w1, w0};
        end
    endfunction

    function [1023:0] pw_record;
        input [10:0] record_index;
        integer k;
        reg [19:0] base_word;
        reg [19:0] k20;
        begin
            base_word = record_index * 16;
            for (k = 0; k < 16; k = k + 1) begin
                k20 = {15'd0, k[4:0]};
                pw_record[k*64+:64] =
                    weight_word(`CNN_KIND_POINTWISE, base_word + k20);
            end
        end
    endfunction

    function [63:0] dma_word;
        input [1:0] kind;
        input [19:0] byte_offset;
        input [19:0] weight_bytes;
        input [19:0] param_offset;
        input [19:0] param_bytes;
        input [8:0] cout;
        reg [19:0] rec;
        begin
            if (byte_offset < weight_bytes) begin
                dma_word = weight_word(kind, byte_offset >> 3);
            end else if ((byte_offset >= param_offset) &&
                         (byte_offset < (param_offset + param_bytes))) begin
                rec = (byte_offset - param_offset) >> 3;
                if ((kind == `CNN_KIND_POINTWISE) && (rec >= {11'd0, cout}))
                    dma_word = 64'd0;
                else dma_word = param_word(rec);
            end else begin
                dma_word = 64'hfeedfacecafebeef;
            end
        end
    endfunction

    task clear_inputs;
        begin
            load_valid = 1'b0;
            load_desc = 256'd0;
            s_dma_data = 64'd0;
            s_dma_valid = 1'b0;
            s_dma_last = 1'b0;
            s_dma_keep = 8'hff;
            conv0_req_valid = 1'b0;
            conv0_req_addr = 5'd0;
            conv0_rsp_ready = 1'b1;
            dw_w_req_valid = 1'b0;
            dw_w_req_addr = 7'd0;
            dw_w_rsp_ready = 1'b1;
            dw_p_req_valid = 1'b0;
            dw_p_req_addr = 9'd0;
            dw_p_rsp_ready = 1'b1;
            pw_req_valid = 1'b0;
            pw_req_addr = 11'd0;
            pw_req_group = 7'd0;
            pw_rsp_ready = 1'b1;
        end
    endtask

    task apply_reset;
        begin
            @(negedge clk);
            clear_inputs;
            rst_n = 1'b0;
            repeat (4) @(posedge clk);
            @(negedge clk);
            rst_n = 1'b1;
            @(posedge clk);
            #1;
        end
    endtask

    task start_load;
        input [255:0] d;
        begin
            @(negedge clk);
            load_desc  = d;
            load_valid = 1'b1;
            while (!load_ready) @(negedge clk);
            @(posedge clk);
            #1;
            @(negedge clk);
            load_valid = 1'b0;
            load_desc  = 256'd0;
            while (!s_dma_ready && !fault) @(posedge clk);
            #1;
        end
    endtask

    task send_beat;
        input [63:0] data;
        input [7:0] keep;
        input last;
        begin
            @(negedge clk);
            s_dma_data  = data;
            s_dma_keep  = keep;
            s_dma_last  = last;
            s_dma_valid = 1'b1;
            while (!s_dma_ready && !fault) @(negedge clk);
            if (!fault) begin
                @(posedge clk);
                #1;
            end
            @(negedge clk);
            s_dma_valid = 1'b0;
            s_dma_last  = 1'b0;
            s_dma_keep  = 8'hff;
        end
    endtask

    task load_operation;
        input [255:0] d;
        input [1:0] kind;
        input [19:0] weight_bytes;
        input [19:0] param_offset;
        input [19:0] param_bytes;
        input [19:0] dma_bytes;
        input [8:0] cout;
        integer b;
        reg [19:0] byte_off;
        begin
            start_load(d);
            total_beats = {12'd0, dma_bytes} / 32'd8;
            for (b = 0; b < total_beats; b = b + 1) begin
                byte_off = {b[16:0], 3'b000};
                send_beat(dma_word(
                          kind,
                          byte_off,
                          weight_bytes,
                          param_offset,
                          param_bytes,
                          cout
                          ), 8'hff, (b == (total_beats - 1)));
            end
            while (!load_done && !fault) @(negedge clk);
            check(load_done && !fault,
                  "normal load reaches COMMIT and pulses load_done");
            @(posedge clk);
            #1;
            check(!load_done, "load_done is exactly one cycle");
        end
    endtask

    task consume_all_responses;
        begin
            conv0_rsp_ready = 1'b1;
            dw_w_rsp_ready = 1'b1;
            dw_p_rsp_ready = 1'b1;
            pw_rsp_ready = 1'b1;
            @(posedge clk);
            #1;
        end
    endtask

    initial begin
        $dumpfile("weight_bram_swap_fsm.vcd");
        $dumpvars(0, tb_weight_bram_swap_fsm);
        clk = 1'b0;
        rst_n = 1'b0;
        pass_count = 0;
        fail_count = 0;
        cycle_count = 0;
        clear_inputs;

        apply_reset;
        check(!fault, "01 reset clears sticky fault");
        check(load_ready, "02 IDLE after reset accepts first load");
        check(
            !conv0_req_ready && !dw_w_req_ready && !dw_p_req_ready && !pw_req_ready,
            "03 unloaded stores reject PE reads");

        @(negedge clk);
        conv0_req_valid = 1'b1;
        load_valid = 1'b1;
        load_desc =
            make_desc(5'd0, `CNN_KIND_CONV0, 9'd3, 9'd24, 18'd768, 20'd960);
        #1;
        check(!load_ready && !conv0_req_ready,
              "04 invalid-store request backpressures load without fault");
        @(negedge clk);
        conv0_req_valid = 1'b0;
        load_valid = 1'b0;

        load_operation(
            make_desc(5'd0, `CNN_KIND_CONV0, 9'd3, 9'd24, 18'd768, 20'd960),
            `CNN_KIND_CONV0, 20'd768, 20'd768, 20'd192, 20'd960, 9'd24);
        check(conv0_req_ready,
              "05 conv0 storage becomes valid only after COMMIT");

        @(negedge clk);
        conv0_req_addr  = 5'd0;
        conv0_req_valid = 1'b1;
        conv0_rsp_ready = 1'b1;
        #1;
        check(conv0_req_ready, "06 conv0 request accepted in IDLE");
        @(posedge clk);
        #1;
        @(negedge clk);
        conv0_req_valid = 1'b0;
        check(conv0_rsp_valid, "07 conv0 response valid at N+1");
        check(conv0_rsp_data == conv0_record(5'd0),
              "08 conv0 216-bit record removes five padding bytes");
        check(conv0_rsp_params == param_word(20'd0),
              "09 conv0 parameter response");
        @(posedge clk);
        #1;

        @(negedge clk);
        conv0_rsp_ready = 1'b0;
        conv0_req_addr  = 5'd1;
        conv0_req_valid = 1'b1;
        @(posedge clk);
        #1;
        @(negedge clk);
        conv0_req_valid = 1'b0;
        held216 = conv0_rsp_data;
        repeat (2) begin
            @(posedge clk);
            #1;
            check(
                conv0_rsp_valid && (conv0_rsp_data == held216) && !conv0_req_ready,
                "10 stalled conv0 response holds valid/data and blocks request");
        end
        @(negedge clk);
        load_valid = 1'b1;
        load_desc =
            make_desc(5'd0, `CNN_KIND_CONV0, 9'd3, 9'd24, 18'd768, 20'd960);
        #1;
        check(!load_ready, "11 pending response blocks new load");
        load_valid = 1'b0;

        conv0_rsp_ready = 1'b1;
        conv0_req_addr = 5'd2;
        conv0_req_valid = 1'b1;
        #1;
        check(conv0_req_ready,
              "12 response consume and new request can coincide");
        @(posedge clk);
        #1;
        @(negedge clk);
        conv0_req_valid = 1'b0;
        check(conv0_rsp_valid && (conv0_rsp_params == param_word(20'd2)),
              "13 consume plus new request preserves II=1");
        @(posedge clk);
        #1;

        load_operation(
            make_desc(5'd1, `CNN_KIND_DEPTHWISE, 9'd24, 9'd24, 18'd320, 20'd512
            ), `CNN_KIND_DEPTHWISE, 20'd288, 20'd320, 20'd192, 20'd512, 9'd24);
        check(dw_w_req_ready && dw_p_req_ready,
              "14 DW weight and parameter stores committed");

        @(negedge clk);
        dw_w_req_addr  = 7'd0;
        dw_w_req_valid = 1'b1;
        dw_p_req_addr  = 9'd0;
        dw_p_req_valid = 1'b1;
        @(posedge clk);
        #1;
        @(negedge clk);
        dw_w_req_valid = 1'b0;
        dw_p_req_valid = 1'b0;
        check(dw_w_rsp_valid && dw_p_rsp_valid,
              "15 DW read channels operate independently together");
        check(dw_w_rsp_data == {weight_word(`CNN_KIND_DEPTHWISE, 20'd3
              ), weight_word(`CNN_KIND_DEPTHWISE, 20'd2), weight_word(
              `CNN_KIND_DEPTHWISE, 20'd1), weight_word(
              `CNN_KIND_DEPTHWISE, 20'd0)}, "16 DW 256-bit weight bank order");
        check(dw_p_rsp_data == param_word(20'd0), "17 DW real parameter read");
        @(posedge clk);
        #1;

        @(negedge clk);
        dw_p_req_addr  = 9'd24;
        dw_p_req_valid = 1'b1;
        @(posedge clk);
        #1;
        @(negedge clk);
        dw_p_req_valid = 1'b0;
        check(dw_p_rsp_valid && (dw_p_rsp_data == 64'd0),
              "18 DW tail lane returns zero without RAM data");
        @(posedge clk);
        #1;

        load_operation(
            make_desc(
            5'd2, `CNN_KIND_POINTWISE, 9'd24, 9'd48, 18'd1536, 20'd1920),
            `CNN_KIND_POINTWISE, 20'd1536, 20'd1536, 20'd384, 20'd1920, 9'd48);
        check(pw_req_ready, "19 PW store committed");

        @(negedge clk);
        dw_w_rsp_ready = 1'b0;
        dw_w_req_addr = 7'd1;
        dw_w_req_valid = 1'b1;
        pw_req_addr = 11'd0;
        pw_req_group = 7'd0;
        pw_req_valid = 1'b1;
        pw_rsp_ready = 1'b1;
        @(posedge clk);
        #1;
        @(negedge clk);
        dw_w_req_valid = 1'b0;
        pw_req_valid   = 1'b0;
        check(dw_w_rsp_valid && pw_rsp_valid,
              "20 simultaneous DW/PW read accepted");
        @(posedge clk);
        #1;
        check(dw_w_rsp_valid && !pw_rsp_valid,
              "21 one read channel can stall while another completes");
        check(pw_rsp_data == pw_record(11'd0), "22 PW 16-bank response order");
        check(pw_rsp_params[63:0] == param_word(20'd0
              ) && pw_rsp_params[255:192] == param_word(20'd3),
              "23 PW four-record parameter group order");
        @(negedge clk);
        dw_w_rsp_ready = 1'b1;
        @(posedge clk);
        #1;

        @(negedge clk);
        pw_req_addr = 11'd1;
        pw_req_group = 7'd1;
        pw_req_valid = 1'b1;
        load_desc = make_desc(5'd1, `CNN_KIND_DEPTHWISE, 9'd24, 9'd24, 18'd320,
                              20'd512);
        load_valid = 1'b1;
        #1;
        check(pw_req_ready && !load_ready,
              "24 PE read wins simultaneous IDLE load presentation");
        @(posedge clk);
        #1;
        @(negedge clk);
        pw_req_valid = 1'b0;
        load_valid   = 1'b0;
        @(posedge clk);
        #1;

        load_operation(
            make_desc(
            5'd27, `CNN_KIND_POINTWISE, 9'd384, 9'd17, 18'd7680, 20'd7872),
            `CNN_KIND_POINTWISE, 20'd7680, 20'd7680, 20'd192, 20'd7872, 9'd17);
        @(negedge clk);
        pw_req_addr  = 11'd59;
        pw_req_group = 7'd4;
        pw_req_valid = 1'b1;
        @(posedge clk);
        #1;
        @(negedge clk);
        pw_req_valid = 1'b0;
        check(pw_rsp_valid && (pw_rsp_params[63:0] == param_word(20'd16
              )) && (pw_rsp_params[255:64] == 192'd0),
              "25 PW final group reads packed zero dummy records");
        @(posedge clk);
        #1;

        load_operation(
            make_desc(
            5'd28, `CNN_KIND_POINTWISE, 9'd384, 9'd34, 18'd13824, 20'd14144),
            `CNN_KIND_POINTWISE, 20'd13824, 20'd13824, 20'd288, 20'd14144,
            9'd34);
        @(negedge clk);
        pw_req_addr  = 11'd107;
        pw_req_group = 7'd8;
        pw_req_valid = 1'b1;
        @(posedge clk);
        #1;
        @(negedge clk);
        pw_req_valid = 1'b0;
        check(pw_rsp_valid && (pw_rsp_params[63:0] == param_word(20'd32
              )) && (pw_rsp_params[127:64] == param_word(20'd33
              )) && (pw_rsp_params[255:128] == 128'd0),
              "25b final op tail padding and packed dummy records");
        @(posedge clk);
        #1;

        load_operation(
            make_desc(
            5'd2, `CNN_KIND_POINTWISE, 9'd384, 9'd384, 18'd147456, 20'd150528),
            `CNN_KIND_POINTWISE, 20'd147456, 20'd147456, 20'd3072, 20'd150528,
            9'd384);
        @(negedge clk);
        pw_req_addr  = 11'd1151;
        pw_req_group = 7'd95;
        pw_req_valid = 1'b1;
        @(posedge clk);
        #1;
        @(negedge clk);
        pw_req_valid = 1'b0;
        check(pw_rsp_valid && (pw_rsp_data == pw_record(11'd1151
              )) && (pw_rsp_params[63:0] == param_word(20'd380)),
              "26 maximum PW address boundary");
        @(posedge clk);
        #1;

        apply_reset;
        start_load(make_desc(
                   5'd0, `CNN_KIND_CONV0, 9'd3, 9'd24, 18'd768, 20'd960));
        check(
            !conv0_req_ready && !dw_w_req_ready && !dw_p_req_ready && !pw_req_ready,
            "27 loader CHECK/LOAD blocks every PE request");
        send_beat(weight_word(`CNN_KIND_CONV0, 20'd0), 8'h0f, 1'b0);
        check(fault && !s_dma_ready && !load_done,
              "28 wrong keep causes sticky fault and no done");
        @(posedge clk);
        #1;
        check(fault, "29 fault remains sticky");

        apply_reset;
        check(!fault && !conv0_req_ready,
              "30 reset recovers control but storage remains invalid");
        start_load(make_desc(
                   5'd0, `CNN_KIND_CONV0, 9'd3, 9'd24, 18'd768, 20'd960));
        send_beat(weight_word(`CNN_KIND_CONV0, 20'd0), 8'hff, 1'b1);
        check(fault, "31 early last detected on accepted beat");

        apply_reset;
        start_load(make_desc(
                   5'd0, `CNN_KIND_CONV0, 9'd3, 9'd24, 18'd768, 20'd960));
        for (i = 0; i < 120; i = i + 1)
        send_beat(
            dma_word(
            `CNN_KIND_CONV0, {i[16:0], 3'b000}, 20'd768, 20'd768, 20'd192, 9'd24
            ), 8'hff, 1'b0);
        check(fault, "32 missing last detected at expected byte count");

        apply_reset;
        start_load(make_desc(
                   5'd0, `CNN_KIND_CONV0, 9'd3, 9'd24, 18'd768, 20'd960));
        for (i = 0; i < 96; i = i + 1)
        send_beat(weight_word(`CNN_KIND_CONV0, i[19:0]), 8'hff, 1'b0);
        send_beat(64'h00000001_01000000, 8'hff, 1'b0);
        check(fault, "33 parameter bias sign-extension error detected");

        apply_reset;
        start_load(make_desc(
                   5'd0, `CNN_KIND_CONV0, 9'd3, 9'd24, 18'd768, 20'd960));
        for (i = 0; i < 96; i = i + 1)
        send_beat(weight_word(`CNN_KIND_CONV0, i[19:0]), 8'hff, 1'b0);
        send_beat(64'h00020000_00000000, 8'hff, 1'b0);
        check(fault, "34 parameter M range error detected");

        apply_reset;
        start_load(make_desc(
                   5'd0, `CNN_KIND_CONV0, 9'd3, 9'd24, 18'd768, 20'd960));
        @(negedge clk);
        dut.byte_count = 20'd960;
        send_beat(64'd0, 8'hff, 1'b0);
        check(fault, "35 DMA count overflow detector");

        apply_reset;
        desc_tmp = make_desc(5'd0, 2'd3, 9'd3, 9'd24, 18'd768, 20'd960);
        start_load(desc_tmp);
        check(fault, "36 invalid kind rejected in CHECK");

        apply_reset;
        desc_tmp =
            make_desc(5'd0, `CNN_KIND_CONV0, 9'd3, 9'd24, 18'd768, 20'd960);
        desc_tmp[255] = 1'b1;
        start_load(desc_tmp);
        check(fault, "37 nonzero reserved descriptor bits rejected");

        apply_reset;
        desc_tmp = make_desc(5'd2, `CNN_KIND_POINTWISE, 9'd384, 9'd384,
                             18'd147392, 20'd150528);
        start_load(desc_tmp);
        check(fault, "38 descriptor offset/capacity contract rejected");

        apply_reset;
        load_operation(
            make_desc(5'd1, `CNN_KIND_DEPTHWISE, 9'd1, 9'd1, 18'd320, 20'd384),
            `CNN_KIND_DEPTHWISE, 20'd288, 20'd320, 20'd8, 20'd384, 9'd1);
        @(negedge clk);
        dw_p_req_addr  = 9'd0;
        dw_p_req_valid = 1'b1;
        @(posedge clk);
        #1;
        @(negedge clk);
        dw_p_req_valid = 1'b0;
        check(dw_p_rsp_valid && (dw_p_rsp_data == param_word(20'd0)),
              "38b tail padding does not overwrite the real parameter");
        @(posedge clk);
        #1;
        @(negedge clk);
        dw_p_req_addr  = 9'd31;
        dw_p_req_valid = 1'b1;
        @(posedge clk);
        #1;
        @(negedge clk);
        dw_p_req_valid = 1'b0;
        check(dw_p_rsp_valid && (dw_p_rsp_data == 64'd0),
              "39 minimum Cin final dummy lane is legal");
        @(posedge clk);
        #1;
        @(negedge clk);
        dw_p_req_addr  = 9'd32;
        dw_p_req_valid = 1'b1;
        @(posedge clk);
        #1;
        @(negedge clk);
        dw_p_req_valid = 1'b0;
        check(fault && !dw_p_rsp_valid,
              "40 out-of-range DW parameter request faults with no response");

        apply_reset;
        check(!fault && !load_done && !s_dma_ready,
              "41 final reset returns loader to clean IDLE");

        $display("REGRESSION SUMMARY PASS=%0d FAIL=%0d", pass_count,
                 fail_count);
        if (fail_count != 0) begin
            $display("TESTBENCH FAILED");
            $finish;
        end else begin
            $display("TESTBENCH PASSED");
            $finish;
        end
    end

endmodule
