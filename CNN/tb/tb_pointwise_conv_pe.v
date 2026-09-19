`timescale 1ns/1ps
`include "cnn_common_params.vh"

module tb_pointwise_conv_pe;

    reg clk;
    reg rst_n;
    reg cfg_valid;
    wire cfg_ready;
    reg [255:0] cfg_desc;
    wire done;
    wire fault;
    reg [255:0] s_pixel_data;
    reg s_pixel_valid;
    wire s_pixel_ready;
    reg [31:0] s_pixel_mask;
    reg [63:0] s_pixel_tag;
    wire [63:0] m_value_data;
    wire m_value_valid;
    reg m_value_ready;
    wire [3:0] m_value_mask;
    wire [63:0] m_value_tag;
    wire pw_req_valid;
    reg pw_req_ready_enable;
    wire pw_req_ready;
    wire [10:0] pw_req_addr;
    reg pw_rsp_valid;
    wire pw_rsp_ready;
    reg [1023:0] pw_rsp_data;
    reg [255:0] pw_rsp_params;
    wire [6:0] pw_req_group;

    integer errors;
    integer cycle_count;
    integer current_scenario;
    integer tb_cin, tb_cout, tb_gin, tb_gout;
    integer expected_count, output_seen;
    integer request_index;
    integer issue_count, p10_count;
    integer issue_cycles [0:127];
    integer measured_latency;
    integer measured_ii;
    integer latency_checks;
    integer ii_checks;
    integer output_stall_seen;
    integer weight_stall_seen;
    integer input_during_output_stall_seen;

    reg [63:0] expected_data [0:63];
    reg [3:0] expected_mask [0:63];
    reg [63:0] expected_tag [0:63];

    reg stalled_output_active;
    reg [63:0] stalled_data;
    reg [3:0] stalled_mask;
    reg [63:0] stalled_tag;
    reg stalled_request_active;
    reg [10:0] stalled_request_addr;
    reg [6:0] stalled_request_group;

    pointwise_conv_pe dut (
        .clk(clk),
        .rst_n(rst_n),
        .cfg_valid(cfg_valid),
        .cfg_ready(cfg_ready),
        .cfg_desc(cfg_desc),
        .done(done),
        .fault(fault),
        .s_pixel_data(s_pixel_data),
        .s_pixel_valid(s_pixel_valid),
        .s_pixel_ready(s_pixel_ready),
        .s_pixel_mask(s_pixel_mask),
        .s_pixel_tag(s_pixel_tag),
        .m_value_data(m_value_data),
        .m_value_valid(m_value_valid),
        .m_value_ready(m_value_ready),
        .m_value_mask(m_value_mask),
        .m_value_tag(m_value_tag),
        .pw_req_valid(pw_req_valid),
        .pw_req_ready(pw_req_ready),
        .pw_req_addr(pw_req_addr),
        .pw_rsp_valid(pw_rsp_valid),
        .pw_rsp_ready(pw_rsp_ready),
        .pw_rsp_data(pw_rsp_data),
        .pw_rsp_params(pw_rsp_params),
        .pw_req_group(pw_req_group)
    );

    initial clk = 1'b0;
    always #5 clk = ~clk;

    function [31:0] make_input_mask;
        input integer channels;
        input integer batch;
        integer remaining;
        begin
            remaining = channels - batch*32;
            if (remaining >= 32)
                make_input_mask = 32'hffffffff;
            else if (remaining <= 0)
                make_input_mask = 32'd0;
            else
                make_input_mask = (32'h1 << remaining) - 1'b1;
        end
    endfunction

    function integer scenario_weight;
        input integer scenario;
        input integer channel;
        begin
            case (scenario)
                1: begin
                    case (channel)
                        0: scenario_weight = 1;
                        1: scenario_weight = -1;
                        2: scenario_weight = 2;
                        3: scenario_weight = 0;
                        4: scenario_weight = 3;
                        5: scenario_weight = 4;
                        6: scenario_weight = 5;
                        default: scenario_weight = 6;
                    endcase
                end
                2: begin
                    case (channel)
                        0: scenario_weight = 1;
                        1: scenario_weight = 2;
                        2: scenario_weight = -1;
                        default: scenario_weight = 3;
                    endcase
                end
                3: begin
                    if (channel == 0) scenario_weight = -3;
                    else if (channel == 1) scenario_weight = 3;
                    else scenario_weight = (channel % 3) - 1;
                end
                5: begin
                    case (channel)
                        0: scenario_weight = 1;
                        1: scenario_weight = -1;
                        2: scenario_weight = 2;
                        default: scenario_weight = 0;
                    endcase
                end
                default: scenario_weight = 0;
            endcase
        end
    endfunction

    function integer scenario_bias;
        input integer scenario;
        input integer channel;
        begin
            case (scenario)
                1, 5: scenario_bias = channel - 2;
                2: begin
                    case (channel)
                        0: scenario_bias = 0;
                        1: scenario_bias = -1;
                        2: scenario_bias = 100;
                        default: scenario_bias = -100;
                    endcase
                end
                3: scenario_bias = channel - 8;
                4: begin
                    case (channel)
                        0: scenario_bias = -40000;
                        1: scenario_bias = 40000;
                        2: scenario_bias = -32768;
                        3: scenario_bias = 32767;
                        default: scenario_bias = channel*100 - 1500;
                    endcase
                end
                default: scenario_bias = 0;
            endcase
        end
    endfunction

    function [1023:0] make_weight_word;
        input [6:0] group_value;
        input [10:0] address_value;
        integer batch_value;
        integer k, j, channel, input_channel;
        integer signed weight_value;
        reg [1023:0] word_value;
        begin
            word_value = 1024'd0;
            batch_value = address_value - group_value*tb_gin;
            for (k = 0; k < 4; k = k + 1) begin
                channel = group_value*4 + k;
                for (j = 0; j < 32; j = j + 1) begin
                    input_channel = batch_value*32 + j;
                    if ((channel < tb_cout) && (input_channel < tb_cin))
                        weight_value = scenario_weight(current_scenario, channel);
                    else
                        weight_value = 0;
                    word_value[(k*32+j)*8 +: 8] = weight_value[7:0];
                end
            end
            make_weight_word = word_value;
        end
    endfunction

    function [255:0] make_parameter_word;
        input [6:0] group_value;
        integer k, channel;
        integer signed bias_value;
        reg [255:0] word_value;
        begin
            word_value = 256'd0;
            for (k = 0; k < 4; k = k + 1) begin
                channel = group_value*4 + k;
                if (channel < tb_cout) begin
                    bias_value = scenario_bias(current_scenario, channel);
                    word_value[k*64 +: 32] = bias_value[31:0];
                    word_value[k*64+32 +: 32] = 32'd65536;
                end
            end
            make_parameter_word = word_value;
        end
    endfunction

    assign pw_req_ready = pw_req_ready_enable &&
                          (!pw_rsp_valid || pw_rsp_ready);

    always @(posedge clk) begin
        if (!rst_n) begin
            pw_rsp_valid <= 1'b0;
            pw_rsp_data <= 1024'd0;
            pw_rsp_params <= 256'd0;
            request_index = 0;
        end else begin
            if (pw_req_valid && pw_req_ready) begin
                if (pw_req_group !== ((request_index / tb_gin) % tb_gout)) begin
                    $display("ERROR request group scenario=%0d index=%0d got=%0d expected=%0d",
                             current_scenario, request_index, pw_req_group,
                             ((request_index / tb_gin) % tb_gout));
                    errors = errors + 1;
                end
                if (pw_req_addr !==
                    (pw_req_group*tb_gin + (request_index % tb_gin))) begin
                    $display("ERROR request address scenario=%0d index=%0d got=%0d expected=%0d",
                             current_scenario, request_index, pw_req_addr,
                             pw_req_group*tb_gin + (request_index % tb_gin));
                    errors = errors + 1;
                end
                pw_rsp_valid <= 1'b1;
                pw_rsp_data <= make_weight_word(pw_req_group, pw_req_addr);
                pw_rsp_params <= make_parameter_word(pw_req_group);
                request_index = request_index + 1;
            end else if (pw_rsp_valid && pw_rsp_ready) begin
                pw_rsp_valid <= 1'b0;
            end
        end
    end

    task set_expected;
        input integer index_value;
        input integer signed value0;
        input integer signed value1;
        input integer signed value2;
        input integer signed value3;
        input [3:0] mask_value;
        input integer row_value;
        input integer col_value;
        input integer group_value;
        input integer op_value;
        input integer group_last_value;
        input integer frame_end_value;
        reg [63:0] tag_value;
        begin
            expected_data[index_value] = {
                value3[15:0], value2[15:0], value1[15:0], value0[15:0]};
            expected_mask[index_value] = mask_value;
            tag_value = 64'd0;
            tag_value[`TAG_COL_MSB:`TAG_COL_LSB] = col_value[7:0];
            tag_value[`TAG_ROW_MSB:`TAG_ROW_LSB] = row_value[7:0];
            tag_value[`TAG_BATCH_MSB:`TAG_BATCH_LSB] = 4'd0;
            tag_value[`TAG_GROUP_MSB:`TAG_GROUP_LSB] = group_value[6:0];
            tag_value[`TAG_TAP_MSB:`TAG_TAP_LSB] = 4'd0;
            tag_value[`TAG_BATCH_LAST_BIT] = 1'b1;
            tag_value[`TAG_PIXEL_LAST_BIT] = group_last_value[0];
            tag_value[`TAG_OP_ID_MSB:`TAG_OP_ID_LSB] = op_value[4:0];
            tag_value[`TAG_FRAME_END_BIT] = frame_end_value[0];
            tag_value[`TAG_GROUP_LAST_BIT] = group_last_value[0];
            expected_tag[index_value] = tag_value;
        end
    endtask

    task begin_scenario;
        input integer scenario_value;
        input integer count_value;
        begin
            current_scenario = scenario_value;
            expected_count = count_value;
            output_seen = 0;
            request_index = 0;
            issue_count = 0;
            p10_count = 0;
            stalled_output_active = 1'b0;
            stalled_request_active = 1'b0;
        end
    endtask

    task configure;
        input [1:0] mode_value;
        input [4:0] op_value;
        input integer hin_value;
        input integer win_value;
        input integer cin_value;
        input integer cout_value;
        begin
            tb_cin = cin_value;
            tb_cout = cout_value;
            tb_gin = (cin_value + 31) / 32;
            tb_gout = (cout_value + 3) / 4;
            cfg_desc = 256'd0;
            cfg_desc[`CFG_OP_ID_MSB:`CFG_OP_ID_LSB] = op_value;
            cfg_desc[`CFG_KIND_MSB:`CFG_KIND_LSB] = `CNN_KIND_POINTWISE;
            cfg_desc[`CFG_MODE_MSB:`CFG_MODE_LSB] = mode_value;
            cfg_desc[`CFG_HIN_MSB:`CFG_HIN_LSB] = hin_value;
            cfg_desc[`CFG_WIN_MSB:`CFG_WIN_LSB] = win_value;
            cfg_desc[`CFG_HOUT_MSB:`CFG_HOUT_LSB] = hin_value;
            cfg_desc[`CFG_WOUT_MSB:`CFG_WOUT_LSB] = win_value;
            cfg_desc[`CFG_CIN_MSB:`CFG_CIN_LSB] = cin_value;
            cfg_desc[`CFG_COUT_MSB:`CFG_COUT_LSB] = cout_value;
            cfg_desc[`CFG_STRIDE_MSB:`CFG_STRIDE_LSB] = 2'd1;
            cfg_desc[`CFG_DILATION_MSB:`CFG_DILATION_LSB] = 2'd1;
            cfg_desc[`CFG_PAD_MSB:`CFG_PAD_LSB] = 2'd0;
            cfg_desc[`CFG_SHIFT_MSB:`CFG_SHIFT_LSB] = 6'd16;
            cfg_valid = 1'b1;
            while (!cfg_ready) @(posedge clk);
            @(posedge clk);
            #1 cfg_valid = 1'b0;
        end
    endtask

    task send_pixel;
        input integer row_value;
        input integer col_value;
        input integer pixel_value;
        input integer final_pixel;
        integer b, j, lane_value;
        reg [63:0] tag_value;
        reg [31:0] mask_value;
        begin
            for (b = 0; b < tb_gin; b = b + 1) begin
                s_pixel_data = 256'd0;
                mask_value = make_input_mask(tb_cin, b);
                for (j = 0; j < 32; j = j + 1) begin
                    if (mask_value[j]) begin
                        if ((current_scenario == 2) && (b == 1))
                            lane_value = 2;
                        else
                            lane_value = pixel_value;
                        s_pixel_data[j*8 +: 8] = lane_value[7:0];
                    end
                end
                tag_value = 64'd0;
                tag_value[`TAG_COL_MSB:`TAG_COL_LSB] = col_value[7:0];
                tag_value[`TAG_ROW_MSB:`TAG_ROW_LSB] = row_value[7:0];
                tag_value[`TAG_BATCH_MSB:`TAG_BATCH_LSB] = b[3:0];
                tag_value[`TAG_BATCH_LAST_BIT] = (b == tb_gin-1);
                tag_value[`TAG_PIXEL_LAST_BIT] = (b == tb_gin-1);
                tag_value[`TAG_OP_ID_MSB:`TAG_OP_ID_LSB] =
                    (current_scenario == 3) ? 5'd27 :
                    (current_scenario == 4) ? 5'd28 :
                    (current_scenario == 1) ? 5'd1 :
                    (current_scenario == 2) ? 5'd3 : 5'd5;
                tag_value[`TAG_FRAME_END_BIT] = final_pixel && (b == tb_gin-1);
                s_pixel_mask = mask_value;
                s_pixel_tag = tag_value;
                s_pixel_valid = 1'b1;
                while (!s_pixel_ready) @(posedge clk);
                @(posedge clk);
                if (m_value_valid && !m_value_ready)
                    input_during_output_stall_seen = 1;
                #1 s_pixel_valid = 1'b0;
            end
        end
    endtask

    task wait_done_pulse;
        begin
            wait (output_seen == expected_count);
            wait (done === 1'b1);
            @(posedge clk);
            #1;
            if (done !== 1'b0) begin
                $display("ERROR done is not a one-cycle pulse in scenario %0d",
                         current_scenario);
                errors = errors + 1;
            end
            if (fault) begin
                $display("ERROR fault asserted at scenario completion %0d",
                         current_scenario);
                errors = errors + 1;
            end
        end
    endtask

    always @(posedge clk) begin
        if (!rst_n) begin
            cycle_count = 0;
            stalled_output_active = 1'b0;
            stalled_request_active = 1'b0;
        end else begin
            cycle_count = cycle_count + 1;

            if (dut.mac_issue_last) begin
                issue_cycles[issue_count] = cycle_count;
                if ((current_scenario == 1) && (issue_count > 0)) begin
                    measured_ii = cycle_count - issue_cycles[issue_count-1];
                    ii_checks = ii_checks + 1;
                    if (measured_ii != 1) begin
                        $display("ERROR II expected 1 got %0d", measured_ii);
                        errors = errors + 1;
                    end
                end
                issue_count = issue_count + 1;
            end

            if (dut.p10_load) begin
                if (p10_count >= issue_count) begin
                    $display("ERROR P10 token without matching final-batch issue");
                    errors = errors + 1;
                end else if (current_scenario == 1) begin
                    measured_latency = cycle_count - issue_cycles[p10_count] + 1;
                    latency_checks = latency_checks + 1;
                    if (measured_latency != 11) begin
                        $display("ERROR latency expected 11 got %0d output=%0d",
                                 measured_latency, p10_count);
                        errors = errors + 1;
                    end
                end
                p10_count = p10_count + 1;
            end

            if (m_value_valid && !m_value_ready) begin
                output_stall_seen = 1;
                if (!stalled_output_active) begin
                    stalled_output_active = 1'b1;
                    stalled_data = m_value_data;
                    stalled_mask = m_value_mask;
                    stalled_tag = m_value_tag;
                end else if ((m_value_data !== stalled_data) ||
                             (m_value_mask !== stalled_mask) ||
                             (m_value_tag !== stalled_tag)) begin
                    $display("ERROR output payload changed during stall");
                    errors = errors + 1;
                end
            end else begin
                stalled_output_active = 1'b0;
            end

            if (stalled_request_active) begin
                if ((pw_req_valid !== 1'b1) ||
                    (pw_req_addr !== stalled_request_addr) ||
                    (pw_req_group !== stalled_request_group)) begin
                    $display("ERROR weight request changed during stall");
                    errors = errors + 1;
                end
            end
            if (pw_req_valid && !pw_req_ready) weight_stall_seen = 1;
            stalled_request_active = pw_req_valid && !pw_req_ready;
            stalled_request_addr = pw_req_addr;
            stalled_request_group = pw_req_group;

            if (m_value_valid && m_value_ready) begin
                if (output_seen >= expected_count) begin
                    $display("ERROR unexpected extra output scenario=%0d data=%h",
                             current_scenario, m_value_data);
                    errors = errors + 1;
                end else begin
                    if (m_value_data !== expected_data[output_seen]) begin
                        $display("ERROR data scenario=%0d output=%0d got=%h expected=%h",
                                 current_scenario, output_seen,
                                 m_value_data, expected_data[output_seen]);
                        errors = errors + 1;
                    end
                    if (m_value_mask !== expected_mask[output_seen]) begin
                        $display("ERROR mask scenario=%0d output=%0d got=%h expected=%h",
                                 current_scenario, output_seen,
                                 m_value_mask, expected_mask[output_seen]);
                        errors = errors + 1;
                    end
                    if (m_value_tag !== expected_tag[output_seen]) begin
                        $display("ERROR tag scenario=%0d output=%0d got=%h expected=%h",
                                 current_scenario, output_seen,
                                 m_value_tag, expected_tag[output_seen]);
                        errors = errors + 1;
                    end
                end
                output_seen = output_seen + 1;
            end

            if (fault) begin
                $display("ERROR DUT fault scenario=%0d cycle=%0d",
                         current_scenario, cycle_count);
                errors = errors + 1;
            end
        end
    end

    initial begin
        errors = 0;
        current_scenario = 0;
        tb_cin = 1; tb_cout = 1; tb_gin = 1; tb_gout = 1;
        expected_count = 0; output_seen = 0; request_index = 0;
        issue_count = 0; p10_count = 0;
        measured_latency = 0; measured_ii = 0;
        latency_checks = 0; ii_checks = 0;
        output_stall_seen = 0;
        weight_stall_seen = 0;
        input_during_output_stall_seen = 0;
        rst_n = 1'b0;
        cfg_valid = 1'b0;
        cfg_desc = 256'd0;
        s_pixel_data = 256'd0;
        s_pixel_valid = 1'b0;
        s_pixel_mask = 32'd0;
        s_pixel_tag = 64'd0;
        m_value_ready = 1'b1;
        pw_req_ready_enable = 1'b1;
        pw_rsp_valid = 1'b0;
        pw_rsp_data = 1024'd0;
        pw_rsp_params = 256'd0;
        stalled_output_active = 1'b0;
        stalled_request_active = 1'b0;

        repeat (5) @(posedge clk);
        #1 rst_n = 1'b1;
        wait (cfg_ready);

        /* Scenario 1: Cin24/Cout8, two consecutive groups, BODY, request stall,
           latency=11 and II=1.  Constants were generated by cnn_model_v4.py. */
        begin_scenario(1, 2);
        set_expected(0, 22, 0, 48, 1, 4'hf, 0, 0, 0, 2, 0, 0);
        set_expected(1, 74, 99, 124, 127, 4'hf, 0, 0, 1, 2, 1, 1);
        pw_req_ready_enable = 1'b0;
        configure(`CNN_MODE_BODY, 5'd2, 1, 1, 24, 8);
        send_pixel(0, 0, 1, 1);
        wait (pw_req_valid);
        repeat (3) @(posedge clk);
        pw_req_ready_enable = 1'b1;
        wait_done_pulse;

        /* Scenario 2: Cin48 two-batch accumulation and first/last batch. */
        begin_scenario(2, 1);
        set_expected(0, 64, 127, 36, 92, 4'hf, 0, 0, 0, 4, 1, 1);
        configure(`CNN_MODE_BODY, 5'd4, 1, 1, 48, 4);
        send_pixel(0, 0, 1, 1);
        wait_done_pulse;

        /* Scenario 3: signed INT8 HEATMAP and Cout17 tail mask 0001. */
        begin_scenario(3, 5);
        set_expected(0, -128, 127, 42, -53, 4'hf, 0, 0, 0, 27, 0, 0);
        set_expected(1, -4, 45, -50, -1, 4'hf, 0, 0, 1, 27, 0, 0);
        set_expected(2, 48, -47, 2, 51, 4'hf, 0, 0, 2, 27, 0, 0);
        set_expected(3, -44, 5, 54, -41, 4'hf, 0, 0, 3, 27, 0, 0);
        set_expected(4, 8, 0, 0, 0, 4'h1, 0, 0, 4, 27, 1, 1);
        configure(`CNN_MODE_HEAT, 5'd27, 1, 1, 24, 17);
        send_pixel(0, 0, 2, 1);
        wait_done_pulse;

        /* Scenario 4: signed INT16 OFFSET and Cout34 tail mask 0011. */
        begin_scenario(4, 9);
        set_expected(0, -32768, 32767, -32768, 32767, 4'hf, 0, 0, 0, 28, 0, 0);
        set_expected(1, -1100, -1000, -900, -800, 4'hf, 0, 0, 1, 28, 0, 0);
        set_expected(2, -700, -600, -500, -400, 4'hf, 0, 0, 2, 28, 0, 0);
        set_expected(3, -300, -200, -100, 0, 4'hf, 0, 0, 3, 28, 0, 0);
        set_expected(4, 100, 200, 300, 400, 4'hf, 0, 0, 4, 28, 0, 0);
        set_expected(5, 500, 600, 700, 800, 4'hf, 0, 0, 5, 28, 0, 0);
        set_expected(6, 900, 1000, 1100, 1200, 4'hf, 0, 0, 6, 28, 0, 0);
        set_expected(7, 1300, 1400, 1500, 1600, 4'hf, 0, 0, 7, 28, 0, 0);
        set_expected(8, 1700, 1800, 0, 0, 4'h3, 0, 0, 8, 28, 1, 1);
        configure(`CNN_MODE_OFFSET, 5'd28, 1, 1, 24, 34);
        send_pixel(0, 0, 1, 1);
        wait_done_pulse;

        /* Scenario 5: hold P10, collect pixel1 into the other slot, then resume. */
        begin_scenario(5, 2);
        set_expected(0, 22, 0, 48, 1, 4'hf, 0, 0, 0, 6, 1, 0);
        set_expected(1, 46, 0, 96, 1, 4'hf, 0, 1, 0, 6, 1, 1);
        configure(`CNN_MODE_BODY, 5'd6, 1, 2, 24, 4);
        send_pixel(0, 0, 1, 0);
        wait (m_value_valid);
        m_value_ready = 1'b0;
        send_pixel(0, 1, 2, 1);
        repeat (5) @(posedge clk);
        m_value_ready = 1'b1;
        wait_done_pulse;

        if (!weight_stall_seen) begin
            $display("ERROR weight request stall scenario was not observed");
            errors = errors + 1;
        end
        if (!output_stall_seen) begin
            $display("ERROR output stall scenario was not observed");
            errors = errors + 1;
        end
        if (!input_during_output_stall_seen) begin
            $display("ERROR second pixel was not accepted during output stall");
            errors = errors + 1;
        end
        if (latency_checks != 2) begin
            $display("ERROR expected 2 latency checks got %0d", latency_checks);
            errors = errors + 1;
        end
        if (ii_checks != 1) begin
            $display("ERROR expected 1 II check got %0d", ii_checks);
            errors = errors + 1;
        end

        if (errors == 0) begin
            $display("POINTWISE_DIRECTED_TEST PASS");
            $display("ARITHMETIC PASS");
            $display("BODY_MODE PASS");
            $display("HEATMAP_MODE PASS");
            $display("OFFSET_MODE PASS");
            $display("TAIL_MASK PASS");
            $display("TAG PASS");
            $display("OUTPUT_STALL PASS");
            $display("WEIGHT_STALL PASS");
            $display("NO_STALL_LATENCY %0d", measured_latency);
            $display("II %0d", measured_ii);
        end else begin
            $display("POINTWISE_DIRECTED_TEST FAIL errors=%0d", errors);
        end
        $finish;
    end

    initial begin
        #200000;
        $display("POINTWISE_DIRECTED_TEST TIMEOUT");
        $finish;
    end

endmodule
