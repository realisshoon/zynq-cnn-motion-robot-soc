`timescale 1ns / 1ps

// 한 cfg에서 Cin 최대 384와 multi-pixel을 검증하는 full-layer regression.
// 모든 weight는 +1, parameter는 identity requant(M=65536, shift=16)이며
// activation은 pixel/batch/tap/lane index로 생성해 산술을 self-check한다.
module tb_depthwise_conv_pe_full_layer;

    parameter integer CASE_CIN = 96;
    parameter integer NUM_PIXELS = 1;
    parameter integer OUTPUT_W = 1;
    parameter integer ENABLE_STALL = 0;

    reg clk;
    reg rst_n;
    reg cfg_valid;
    wire cfg_ready;
    reg [255:0] cfg_desc;
    wire done;
    wire fault;

    reg [255:0] s_tap_data;
    reg s_tap_valid;
    wire s_tap_ready;
    reg [31:0] s_tap_mask;
    reg [63:0] s_tap_tag;

    wire [255:0] m_pixel_data;
    wire m_pixel_valid;
    reg m_pixel_ready;
    wire [31:0] m_pixel_mask;
    wire [63:0] m_pixel_tag;

    wire dw_w_req_valid;
    reg dw_w_req_ready;
    wire [6:0] dw_w_req_addr;
    wire dw_w_rsp_valid;
    wire dw_w_rsp_ready;
    wire [255:0] dw_w_rsp_data;

    wire dw_p_req_valid;
    reg dw_p_req_ready;
    wire [8:0] dw_p_req_addr;
    wire dw_p_rsp_valid;
    wire dw_p_rsp_ready;
    wire [63:0] dw_p_rsp_data;

    reg w_pending;
    reg [6:0] w_pending_addr;
    integer w_delay;
    reg p_pending;
    reg [8:0] p_pending_addr;
    integer p_delay;

    integer errors;
    integer cycle_count;
    integer batches;
    integer pixel;
    integer batch;
    integer tap;
    integer lane;
    integer valid_lanes;
    integer weight_requests;
    integer weight_responses;
    integer param_requests;
    integer param_responses;
    integer tap_accepts;
    integer output_accepts;
    integer quant_issues;
    integer quant_gaps;
    integer last_quant_cycle;
    integer last_quant_kernel;
    integer kernel_sequence;
    integer kernel_start_cycle;
    integer measured_latency;
    integer max_batch_latency;
    integer nonfinal_pixel_checks;
    integer owner_access_checks;
    integer expected_weight_requests;
    integer source_gap_cycles;
    integer output_stall_cycles;
    integer reset_index;

    reg tap_hold_active;
    reg [255:0] tap_hold_data;
    reg [31:0] tap_hold_mask;
    reg [63:0] tap_hold_tag;
    reg output_hold_active;
    reg [255:0] output_hold_data;
    reg [31:0] output_hold_mask;
    reg [63:0] output_hold_tag;

    wire w_req_fire;
    wire w_rsp_fire;
    wire p_req_fire;
    wire p_rsp_fire;
    wire tap_fire;
    wire output_fire;

    assign w_req_fire = dw_w_req_valid && dw_w_req_ready;
    assign w_rsp_fire = dw_w_rsp_valid && dw_w_rsp_ready;
    assign p_req_fire = dw_p_req_valid && dw_p_req_ready;
    assign p_rsp_fire = dw_p_rsp_valid && dw_p_rsp_ready;
    assign tap_fire = s_tap_valid && s_tap_ready;
    assign output_fire = m_pixel_valid && m_pixel_ready;

    assign dw_w_rsp_valid = w_pending && (w_delay == 0);
    assign dw_w_rsp_data = {32{8'h01}};
    assign dw_p_rsp_valid = p_pending && (p_delay == 0);
    assign dw_p_rsp_data = 64'h0001_0000_0000_0000;

    depthwise_conv_pe dut (
        .clk(clk),
        .rst_n(rst_n),
        .cfg_valid(cfg_valid),
        .cfg_ready(cfg_ready),
        .cfg_desc(cfg_desc),
        .done(done),
        .fault(fault),
        .s_tap_data(s_tap_data),
        .s_tap_valid(s_tap_valid),
        .s_tap_ready(s_tap_ready),
        .s_tap_mask(s_tap_mask),
        .s_tap_tag(s_tap_tag),
        .m_pixel_data(m_pixel_data),
        .m_pixel_valid(m_pixel_valid),
        .m_pixel_ready(m_pixel_ready),
        .m_pixel_mask(m_pixel_mask),
        .m_pixel_tag(m_pixel_tag),
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
        .dw_p_rsp_data(dw_p_rsp_data)
    );

    function [7:0] activation_value;
        input integer pixel_number;
        input integer batch_number;
        input integer tap_number;
        input integer lane_number;
        integer value;
        begin
            value = (pixel_number * 7 + batch_number * 3 +
                     tap_number + lane_number) % 16;
            activation_value = value[7:0];
        end
    endfunction

    function [7:0] expected_output_value;
        input integer pixel_number;
        input integer batch_number;
        input integer lane_number;
        integer sum;
        integer function_tap;
        integer absolute_channel;
        begin
            absolute_channel = batch_number * 32 + lane_number;
            if (absolute_channel >= CASE_CIN) begin
                expected_output_value = 8'd0;
            end else begin
                sum = 0;
                for (function_tap = 0; function_tap < 9;
                function_tap = function_tap + 1)
                    sum = sum + activation_value(pixel_number, batch_number,
                                                  function_tap, lane_number);
                if (sum > 127) expected_output_value = 8'd127;
                else expected_output_value = sum[7:0];
            end
        end
    endfunction

    initial begin
        clk = 1'b0;
        forever #5 clk = ~clk;
    end

    // One-outstanding weight owner model. Optional delay와 request-ready
    // 변조로 N+1 response, stall hold, consume+next-request를 검증한다.
    always @(posedge clk) begin
        if (!rst_n) begin
            w_pending <= 1'b0;
            w_pending_addr <= 7'd0;
            w_delay <= 0;
            weight_requests = 0;
            weight_responses = 0;
            dw_w_req_ready <= 1'b1;
        end else begin
            if (ENABLE_STALL != 0)
                dw_w_req_ready <= ((cycle_count % 7) != 3);
            else
                dw_w_req_ready <= 1'b1;

            if (w_pending && (w_delay > 0)) w_delay <= w_delay - 1;

            if (w_rsp_fire) begin
                if (w_pending_addr !== (weight_responses % (batches * 9))) begin
                    $display("FAIL FULL Cin%0d weight response order expected=%0d actual=%0d",
                             CASE_CIN, weight_responses, w_pending_addr);
                    errors = errors + 1;
                end
                weight_responses = weight_responses + 1;
                w_pending <= 1'b0;
            end

            if (w_req_fire) begin
                if (dw_w_req_addr !== (weight_requests % (batches * 9))) begin
                    $display("FAIL FULL Cin%0d weight address expected=%0d actual=%0d",
                             CASE_CIN, weight_requests, dw_w_req_addr);
                    errors = errors + 1;
                end
                if (w_pending && !w_rsp_fire) begin
                    $display("FAIL FULL Cin%0d more than one weight outstanding", CASE_CIN);
                    errors = errors + 1;
                end
                w_pending <= 1'b1;
                w_pending_addr <= dw_w_req_addr;
                if ((ENABLE_STALL != 0) && ((dw_w_req_addr % 5) == 2))
                    w_delay <= 2;
                else
                    w_delay <= 0;
                weight_requests = weight_requests + 1;
            end
        end
    end

    // One-outstanding parameter responder.  Addresses repeat from zero for
    // each pixel and cover only real channels; dummy tail lanes issue no read.
    always @(posedge clk) begin
        if (!rst_n) begin
            p_pending <= 1'b0;
            p_pending_addr <= 9'd0;
            p_delay <= 0;
            param_requests = 0;
            param_responses = 0;
            dw_p_req_ready <= 1'b1;
        end else begin
            if (ENABLE_STALL != 0)
                dw_p_req_ready <= ((cycle_count % 11) != 5);
            else
                dw_p_req_ready <= 1'b1;

            if (p_pending && (p_delay > 0)) p_delay <= p_delay - 1;

            if (p_rsp_fire) begin
                param_responses = param_responses + 1;
                p_pending <= 1'b0;
            end

            if (p_req_fire) begin
                if (dw_p_req_addr !== (param_requests % CASE_CIN)) begin
                    $display("FAIL FULL Cin%0d param address request=%0d expected=%0d actual=%0d",
                             CASE_CIN, param_requests, param_requests % CASE_CIN,
                             dw_p_req_addr);
                    errors = errors + 1;
                end
                if (p_pending && !p_rsp_fire) begin
                    $display("FAIL FULL Cin%0d more than one param outstanding", CASE_CIN);
                    errors = errors + 1;
                end
                p_pending <= 1'b1;
                p_pending_addr <= dw_p_req_addr;
                if ((ENABLE_STALL != 0) && ((dw_p_req_addr % 13) == 4))
                    p_delay <= 2;
                else
                    p_delay <= 0;
                param_requests = param_requests + 1;
            end
        end
    end

    // Event counters, kernel latency start, II=1 check, and stall stability.
    always @(posedge clk) begin
        if (!rst_n) begin
            cycle_count = 0;
            tap_accepts = 0;
            output_accepts = 0;
            quant_issues = 0;
            quant_gaps = 0;
            last_quant_cycle = -1;
            last_quant_kernel = -1;
            kernel_sequence = -1;
            kernel_start_cycle = -1;
            tap_hold_active <= 1'b0;
            output_hold_active <= 1'b0;
        end else begin
            cycle_count = cycle_count + 1;

            if (tap_fire) begin
                tap_accepts = tap_accepts + 1;
                if (dut.tap_index == 0) begin
                    kernel_sequence = kernel_sequence + 1;
                    kernel_start_cycle = cycle_count;
                end
            end
            if (output_fire) output_accepts = output_accepts + 1;

            if (dut.p0_valid) begin
                if ((ENABLE_STALL == 0) &&
                (last_quant_kernel == kernel_sequence) &&
                (last_quant_cycle >= 0) &&
                (cycle_count != last_quant_cycle + 1)) begin
                    $display("FAIL FULL Cin%0d requant II gap kernel=%0d prev=%0d now=%0d",
                             CASE_CIN, kernel_sequence, last_quant_cycle, cycle_count);
                    errors = errors + 1;
                    quant_gaps = quant_gaps + 1;
                end
                last_quant_cycle = cycle_count;
                last_quant_kernel = kernel_sequence;
                quant_issues = quant_issues + 1;
            end

            if (s_tap_valid && !s_tap_ready) begin
                if (tap_hold_active &&
                ((s_tap_data !== tap_hold_data) ||
                 (s_tap_mask !== tap_hold_mask) ||
                 (s_tap_tag !== tap_hold_tag))) begin
                    $display("FAIL FULL Cin%0d tap payload changed while stalled", CASE_CIN);
                    errors = errors + 1;
                end
                tap_hold_active <= 1'b1;
                tap_hold_data <= s_tap_data;
                tap_hold_mask <= s_tap_mask;
                tap_hold_tag <= s_tap_tag;
            end else begin
                tap_hold_active <= 1'b0;
            end

            if (m_pixel_valid && !m_pixel_ready) begin
                if (output_hold_active &&
                ((m_pixel_data !== output_hold_data) ||
                 (m_pixel_mask !== output_hold_mask) ||
                 (m_pixel_tag !== output_hold_tag))) begin
                    $display("FAIL FULL Cin%0d output payload changed while stalled", CASE_CIN);
                    errors = errors + 1;
                end
                output_hold_active <= 1'b1;
                output_hold_data <= m_pixel_data;
                output_hold_mask <= m_pixel_mask;
                output_hold_tag <= m_pixel_tag;
            end else begin
                output_hold_active <= 1'b0;
            end

            if (fault) begin
                $display("FAIL FULL Cin%0d unexpected sticky fault cycle=%0d", CASE_CIN,
                         cycle_count);
                errors = errors + 1;
            end
        end
    end

    task send_tap;
        input integer pixel_number;
        input integer batch_number;
        input integer tap_number;
        integer task_lane;
        integer task_valid_lanes;
        reg [255:0] task_data;
        reg [31:0] task_mask;
        reg [63:0] task_tag;
        begin
            if ((ENABLE_STALL != 0) &&
            (((pixel_number + batch_number + tap_number) % 6) == 2)) begin
                repeat (2) @(negedge clk);
                source_gap_cycles = source_gap_cycles + 2;
            end

            task_valid_lanes = CASE_CIN - batch_number * 32;
            if (task_valid_lanes > 32) task_valid_lanes = 32;
            if (task_valid_lanes == 32) task_mask = 32'hffffffff;
            else task_mask = (32'h00000001 << task_valid_lanes) - 1'b1;

            task_data = 256'd0;
            for (task_lane = 0; task_lane < task_valid_lanes;
            task_lane = task_lane + 1)
                task_data[task_lane*8+:8] = activation_value(
                    pixel_number, batch_number, tap_number, task_lane
                );

            task_tag = 64'd0;
            task_tag[7:0] = pixel_number % OUTPUT_W;
            task_tag[15:8] = pixel_number / OUTPUT_W;
            task_tag[19:16] = batch_number[3:0];
            task_tag[30:27] = tap_number[3:0];
            task_tag[37:33] = 5'd1;
            if ((tap_number == 8) && (batch_number == batches - 1)) begin
                task_tag[31] = 1'b1;
                task_tag[32] = 1'b1;
                task_tag[39] = 1'b1;
                if (pixel_number == NUM_PIXELS - 1)
                    task_tag[38] = 1'b1;
            end

            @(negedge clk);
            s_tap_data = task_data;
            s_tap_mask = task_mask;
            s_tap_tag = task_tag;
            s_tap_valid = 1'b1;
            while (!s_tap_ready) @(negedge clk);
            @(posedge clk);
            #1;
            s_tap_valid = 1'b0;
            s_tap_data = 256'd0;
            s_tap_mask = 32'd0;
            s_tap_tag = 64'd0;
        end
    endtask

    task check_output;
        input integer pixel_number;
        input integer batch_number;
        integer check_lane;
        integer check_valid_lanes;
        reg [31:0] expected_mask;
        reg [63:0] expected_tag;
        reg [7:0] expected_byte;
        begin
            check_valid_lanes = CASE_CIN - batch_number * 32;
            if (check_valid_lanes > 32) check_valid_lanes = 32;
            if (check_valid_lanes == 32) expected_mask = 32'hffffffff;
            else expected_mask = (32'h00000001 << check_valid_lanes) - 1'b1;

            expected_tag = 64'd0;
            expected_tag[7:0] = pixel_number % OUTPUT_W;
            expected_tag[15:8] = pixel_number / OUTPUT_W;
            expected_tag[19:16] = batch_number[3:0];
            expected_tag[37:33] = 5'd1;
            if (batch_number == batches - 1) begin
                expected_tag[31] = 1'b1;
                expected_tag[32] = 1'b1;
                expected_tag[39] = 1'b1;
                if (pixel_number == NUM_PIXELS - 1)
                    expected_tag[38] = 1'b1;
            end

            if (m_pixel_mask !== expected_mask) begin
                $display("FAIL FULL Cin%0d pixel=%0d batch=%0d mask expected=%08x actual=%08x",
                         CASE_CIN, pixel_number, batch_number, expected_mask, m_pixel_mask);
                errors = errors + 1;
            end
            if (m_pixel_tag !== expected_tag) begin
                $display("FAIL FULL Cin%0d pixel=%0d batch=%0d tag expected=%016x actual=%016x",
                         CASE_CIN, pixel_number, batch_number, expected_tag, m_pixel_tag);
                errors = errors + 1;
            end
            for (check_lane = 0; check_lane < 32; check_lane = check_lane + 1) begin
                expected_byte = expected_output_value(pixel_number, batch_number, check_lane);
                if (m_pixel_data[check_lane*8+:8] !== expected_byte) begin
                    $display("FAIL FULL Cin%0d pixel=%0d batch=%0d lane=%0d expected=%0d actual=%0d",
                             CASE_CIN, pixel_number, batch_number, check_lane, expected_byte,
                             m_pixel_data[check_lane*8+:8]);
                    errors = errors + 1;
                end
            end
        end
    endtask

    initial begin
        rst_n = 1'b0;
        cfg_valid = 1'b0;
        cfg_desc = 256'd0;
        s_tap_data = 256'd0;
        s_tap_valid = 1'b0;
        s_tap_mask = 32'd0;
        s_tap_tag = 64'd0;
        m_pixel_ready = 1'b1;
        dw_w_req_ready = 1'b1;
        dw_p_req_ready = 1'b1;
        w_pending = 1'b0;
        w_pending_addr = 7'd0;
        w_delay = 0;
        p_pending = 1'b0;
        p_pending_addr = 9'd0;
        p_delay = 0;
        errors = 0;
        max_batch_latency = 0;
        nonfinal_pixel_checks = 0;
        owner_access_checks = 0;
        source_gap_cycles = 0;
        output_stall_cycles = 0;
        tap_hold_active = 1'b0;
        output_hold_active = 1'b0;
        batches = (CASE_CIN + 31) / 32;
        expected_weight_requests = NUM_PIXELS * batches * 9;

        if ((CASE_CIN < 1) || (CASE_CIN > 384) ||
        (batches < 1) || (batches > 12) ||
        (NUM_PIXELS < 1) || (OUTPUT_W < 1) ||
        ((NUM_PIXELS % OUTPUT_W) != 0)) begin
            $display("FAIL FULL invalid test parameters Cin=%0d pixels=%0d width=%0d",
                     CASE_CIN, NUM_PIXELS, OUTPUT_W);
            $finish(0);
        end

        $dumpfile("dw_full_layer.vcd");
        $dumpvars(0, tb_depthwise_conv_pe_full_layer);

        for (reset_index = 0; reset_index < 5; reset_index = reset_index + 1) begin
            @(posedge clk);
            #1;
            if (cfg_ready !== 1'b0 || fault !== 1'b0 || done !== 1'b0) begin
                $display("FAIL FULL reset outputs cycle=%0d", reset_index);
                errors = errors + 1;
            end
        end
        @(negedge clk);
        rst_n = 1'b1;
        @(posedge clk);
        #1;
        if (cfg_ready !== 1'b1) begin
            $display("FAIL FULL cfg_ready did not recover");
            errors = errors + 1;
        end

        cfg_desc[4:0] = 5'd1;
        cfg_desc[6:5] = 2'd1;
        cfg_desc[8:7] = 2'd0;
        cfg_desc[17:9] = NUM_PIXELS / OUTPUT_W;
        cfg_desc[26:18] = OUTPUT_W;
        cfg_desc[35:27] = NUM_PIXELS / OUTPUT_W;
        cfg_desc[44:36] = OUTPUT_W;
        cfg_desc[53:45] = CASE_CIN;
        cfg_desc[62:54] = CASE_CIN;
        cfg_desc[64:63] = 2'd1;
        cfg_desc[66:65] = 2'd1;
        cfg_desc[68:67] = 2'd1;
        cfg_desc[74:69] = 6'd16;
        cfg_desc[148:145] = 4'd1;

        @(negedge clk);
        cfg_valid = 1'b1;
        while (!cfg_ready) @(negedge clk);
        @(posedge clk);
        #1;
        cfg_valid = 1'b0;

        for (pixel = 0; pixel < NUM_PIXELS; pixel = pixel + 1) begin
            for (batch = 0; batch < batches; batch = batch + 1) begin
                // Stall selected multi-layer outputs before valid so the DUT
                // must hold its payload after it reaches ST_EMIT.
                if ((ENABLE_STALL != 0) &&
                (((pixel * batches + batch) % 5) == 3))
                    m_pixel_ready = 1'b0;
                else
                    m_pixel_ready = 1'b1;

                for (tap = 0; tap < 9; tap = tap + 1)
                    send_tap(pixel, batch, tap);

                // Accumulators are exact before the requant result is emitted.
                valid_lanes = CASE_CIN - batch * 32;
                if (valid_lanes > 32) valid_lanes = 32;
                for (lane = 0; lane < 32; lane = lane + 1) begin
                    if ($signed(dut.accum[lane]) !==
                    $signed({16'd0, expected_output_value(pixel, batch, lane)})) begin
                        // Saturated outputs cannot be used as accumulator oracle.
                        if ((lane < valid_lanes) &&
                        (expected_output_value(pixel, batch, lane) != 8'd127)) begin
                            $display("FAIL FULL Cin%0d accumulator pixel=%0d batch=%0d lane=%0d",
                                     CASE_CIN, pixel, batch, lane);
                            errors = errors + 1;
                        end
                    end
                end

                while (!m_pixel_valid) begin
                    @(posedge clk);
                    #1;
                end
                measured_latency = cycle_count - kernel_start_cycle + 1;
                if (measured_latency > max_batch_latency)
                    max_batch_latency = measured_latency;
                if ((ENABLE_STALL == 0) && (measured_latency > 48)) begin
                    $display("FAIL FULL Cin%0d pixel=%0d batch=%0d latency=%0d",
                             CASE_CIN, pixel, batch, measured_latency);
                    errors = errors + 1;
                end

                check_output(pixel, batch);

                if (!m_pixel_ready) begin
                    repeat (3) begin
                        @(posedge clk);
                        #1;
                        output_stall_cycles = output_stall_cycles + 1;
                        if (!m_pixel_valid) begin
                            $display("FAIL FULL Cin%0d output valid dropped during stall",
                                     CASE_CIN);
                            errors = errors + 1;
                        end
                    end
                    @(negedge clk);
                    m_pixel_ready = 1'b1;
                end

                @(posedge clk);
                #1;
                if ((pixel == NUM_PIXELS - 1) && (batch == batches - 1)) begin
                    if (!done || !cfg_ready) begin
                        $display("FAIL FULL Cin%0d final accept done=%b ready=%b",
                                 CASE_CIN, done, cfg_ready);
                        errors = errors + 1;
                    end
                end else begin
                    if (done) begin
                        $display("FAIL FULL Cin%0d early done pixel=%0d batch=%0d",
                                 CASE_CIN, pixel, batch);
                        errors = errors + 1;
                    end
                    if ((batch == batches - 1) && (pixel < NUM_PIXELS - 1)) begin
                        if ((dut.state == 0) || (dut.batch_index != 0) ||
                        (dut.tap_index != 0) ||
                        !(dw_w_req_valid || dut.weight_outstanding ||
                          dut.weight_buf_valid)) begin
                            $display("FAIL FULL Cin%0d nonfinal pixel did not continue state=%0d batch=%0d tap=%0d w_req=%b outstanding=%b buffered=%b",
                                     CASE_CIN, dut.state, dut.batch_index, dut.tap_index,
                                     dw_w_req_valid, dut.weight_outstanding,
                                     dut.weight_buf_valid);
                            errors = errors + 1;
                        end else begin
                            nonfinal_pixel_checks = nonfinal_pixel_checks + 1;
                        end
                    end
                end
            end
        end

        @(posedge clk);
        #1;
        if (done) begin
            $display("FAIL FULL Cin%0d done wider than one cycle", CASE_CIN);
            errors = errors + 1;
        end

        if ((weight_requests != expected_weight_requests) ||
        (weight_responses != expected_weight_requests)) begin
            $display("FAIL FULL Cin%0d owner weight access req=%0d rsp=%0d expected=%0d",
                     CASE_CIN, weight_requests, weight_responses,
                     expected_weight_requests);
            errors = errors + 1;
        end else begin
            owner_access_checks = 1;
        end
        if ((param_requests != NUM_PIXELS * CASE_CIN) ||
        (param_responses != NUM_PIXELS * CASE_CIN)) begin
            $display("FAIL FULL Cin%0d param counts req=%0d rsp=%0d expected=%0d",
                     CASE_CIN, param_requests, param_responses, NUM_PIXELS * CASE_CIN);
            errors = errors + 1;
        end
        if (tap_accepts != NUM_PIXELS * batches * 9) begin
            $display("FAIL FULL Cin%0d tap count=%0d expected=%0d", CASE_CIN,
                     tap_accepts, NUM_PIXELS * batches * 9);
            errors = errors + 1;
        end
        if (output_accepts != NUM_PIXELS * batches) begin
            $display("FAIL FULL Cin%0d output count=%0d expected=%0d", CASE_CIN,
                     output_accepts, NUM_PIXELS * batches);
            errors = errors + 1;
        end
        if (quant_issues != NUM_PIXELS * batches * 32) begin
            $display("FAIL FULL Cin%0d quant issues=%0d expected=%0d", CASE_CIN,
                     quant_issues, NUM_PIXELS * batches * 32);
            errors = errors + 1;
        end

        $display("FULL_LAYER_SUMMARY Cin=%0d pixels=%0d batches=%0d taps=%0d outputs=%0d weight_expected=%0d weight_actual=%0d params=%0d quant=%0d ii_gaps=%0d max_latency=%0d nonfinal_continue=%0d owner_access=%0d source_gaps=%0d output_stalls=%0d errors=%0d",
                 CASE_CIN, NUM_PIXELS, batches, tap_accepts, output_accepts,
                 expected_weight_requests, weight_requests, param_requests,
                 quant_issues, quant_gaps, max_batch_latency,
                 nonfinal_pixel_checks, owner_access_checks, source_gap_cycles,
                 output_stall_cycles, errors);
        if (errors == 0)
            $display("PASS FULL_LAYER Cin%0d pixels=%0d stall=%0d", CASE_CIN,
                     NUM_PIXELS, ENABLE_STALL);
        else
            $display("FAIL FULL_LAYER Cin%0d pixels=%0d stall=%0d errors=%0d",
                     CASE_CIN, NUM_PIXELS, ENABLE_STALL, errors);
        #20;
        $finish(0);
    end

    initial begin
        #2000000;
        $display("TIMEOUT FULL_LAYER Cin%0d pixels=%0d stall=%0d", CASE_CIN,
                 NUM_PIXELS, ENABLE_STALL);
        $finish(0);
    end

endmodule
