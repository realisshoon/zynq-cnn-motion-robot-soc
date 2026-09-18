`timescale 1ns / 1ps

module tb_depthwise_conv_pe;

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

    reg [255:0] tap_mem[0:8];
    reg [255:0] weight_mem[0:8];
    reg [63:0] param_mem[0:31];
    reg signed [23:0] expected_acc[0:31];
    reg [255:0] expected_out[0:0];

    reg w_pending;
    reg [6:0] w_pending_addr;
    reg p_pending;
    reg [8:0] p_pending_addr;

    integer errors;
    integer tap;
    integer lane;
    integer weight_requests;
    integer weight_responses;
    integer param_requests;
    integer param_responses;
    integer param_ii1_gaps;
    integer cycle_count;
    integer last_param_response_cycle;
    integer completed;
    integer audit_tap_accepts;
    integer cfg_accept_cycle;
    integer first_weight_request_cycle;
    integer last_weight_request_cycle;
    integer first_weight_response_cycle;
    integer last_weight_response_cycle;
    integer first_tap_accept_cycle;
    integer ninth_tap_accept_cycle;
    integer first_tap_mac_issue_cycle;
    integer first_param_request_cycle;
    integer last_param_request_cycle;
    integer first_param_response_cycle;
    integer first_requant_issue_cycle;
    integer last_requant_issue_cycle;
    integer m_pixel_valid_assert_cycle;
    integer output_accept_cycle;
    integer done_cycle;
    integer cfg_to_output_accept_cycles;
    integer first_tap_to_output_accept_cycles;
    integer issue_count_plus_drain_cycles;
    integer first_weight_to_valid_cycles;
    integer first_tap_to_valid_cycles;
    integer contract_latency_cycles;
    integer reset_test_errors;
    integer reset_cfg_handshakes;
    integer reset_cycles_held;
    integer reset_cycle_index;
    integer trace_index;
    integer tap_w_req_cycle[0:8];
    integer tap_w_rsp_valid_cycle[0:8];
    integer tap_w_rsp_accept_cycle[0:8];
    integer tap_accept_cycle[0:8];
    integer tap_mac_issue_cycle[0:8];
    reg [63:0] expected_tag;

    assign dw_w_rsp_valid = w_pending;
    assign dw_w_rsp_data  = weight_mem[w_pending_addr];
    assign dw_p_rsp_valid = p_pending;
    assign dw_p_rsp_data  = param_mem[p_pending_addr];

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

    initial begin
        clk = 1'b0;
        forever #5 clk = ~clk;
    end

    always @(posedge clk) begin
        if (!rst_n) begin
            w_pending <= 1'b0;
            w_pending_addr <= 7'd0;
            weight_requests <= 0;
        end else begin
            if (w_pending && dw_w_rsp_ready) w_pending <= 1'b0;

            if (dw_w_req_valid && dw_w_req_ready) begin
                if (dw_w_req_addr !== weight_requests[6:0]) begin
                    $display("FAIL weight address expected=%0d actual=%0d", weight_requests,
                             dw_w_req_addr);
                    errors = errors + 1;
                end
                w_pending <= 1'b1;
                w_pending_addr <= dw_w_req_addr;
                weight_requests <= weight_requests + 1;
            end
        end
    end

    always @(posedge clk) begin
        if (!rst_n) begin
            cycle_count = 0;
            param_responses = 0;
            param_ii1_gaps = 0;
            last_param_response_cycle = -1;
            weight_responses = 0;
            audit_tap_accepts = 0;
            cfg_accept_cycle = -1;
            first_weight_request_cycle = -1;
            last_weight_request_cycle = -1;
            first_weight_response_cycle = -1;
            last_weight_response_cycle = -1;
            first_tap_accept_cycle = -1;
            ninth_tap_accept_cycle = -1;
            first_tap_mac_issue_cycle = -1;
            first_param_request_cycle = -1;
            last_param_request_cycle = -1;
            first_param_response_cycle = -1;
            first_requant_issue_cycle = -1;
            last_requant_issue_cycle = -1;
            m_pixel_valid_assert_cycle = -1;
            output_accept_cycle = -1;
            done_cycle = -1;
            for (trace_index = 0; trace_index < 9; trace_index = trace_index + 1) begin
                tap_w_req_cycle[trace_index] = -1;
                tap_w_rsp_valid_cycle[trace_index] = -1;
                tap_w_rsp_accept_cycle[trace_index] = -1;
                tap_accept_cycle[trace_index] = -1;
                tap_mac_issue_cycle[trace_index] = -1;
            end
        end else begin
            cycle_count = cycle_count + 1;

            if (cfg_valid && cfg_ready && (cfg_accept_cycle < 0)) cfg_accept_cycle = cycle_count;

            if (dw_w_req_valid && dw_w_req_ready && (first_weight_request_cycle < 0))
                first_weight_request_cycle = cycle_count;
            if (dw_w_req_valid && dw_w_req_ready) begin
                last_weight_request_cycle = cycle_count;
                if (weight_requests < 9) tap_w_req_cycle[weight_requests] = cycle_count;
            end

            if (dw_w_rsp_valid && (weight_responses < 9) &&
            (tap_w_rsp_valid_cycle[weight_responses] < 0))
                tap_w_rsp_valid_cycle[weight_responses] = cycle_count;

            if (dw_w_rsp_valid && dw_w_rsp_ready) begin
                if (first_weight_response_cycle < 0) first_weight_response_cycle = cycle_count;
                last_weight_response_cycle = cycle_count;
                if (weight_responses < 9) tap_w_rsp_accept_cycle[weight_responses] = cycle_count;
                weight_responses = weight_responses + 1;
            end

            if (s_tap_valid && s_tap_ready) begin
                if (audit_tap_accepts == 0) begin
                    first_tap_accept_cycle = cycle_count;
                    first_tap_mac_issue_cycle = cycle_count;
                end
                if (audit_tap_accepts == 8) ninth_tap_accept_cycle = cycle_count;
                if (audit_tap_accepts < 9) begin
                    tap_accept_cycle[audit_tap_accepts] = cycle_count;
                    // The RTL issues all 32 lane MAC assignments in tap_fire.
                    tap_mac_issue_cycle[audit_tap_accepts] = cycle_count;
                end
                audit_tap_accepts = audit_tap_accepts + 1;
            end

            if (dw_p_req_valid && dw_p_req_ready) begin
                if (first_param_request_cycle < 0) first_param_request_cycle = cycle_count;
                last_param_request_cycle = cycle_count;
            end

            if (dw_p_rsp_valid && dw_p_rsp_ready) begin
                if (first_param_response_cycle < 0) first_param_response_cycle = cycle_count;
                if (first_requant_issue_cycle < 0) first_requant_issue_cycle = cycle_count;
                last_requant_issue_cycle = cycle_count;
                if ((param_responses != 0) &&
                (cycle_count != (last_param_response_cycle + 1))) begin
                    $display(
                        "FAIL param response II expected=1 previous_cycle=%0d current_cycle=%0d",
                        last_param_response_cycle, cycle_count);
                    errors = errors + 1;
                    param_ii1_gaps = param_ii1_gaps + 1;
                end
                last_param_response_cycle = cycle_count;
                param_responses = param_responses + 1;
            end

            if (m_pixel_valid && m_pixel_ready && (output_accept_cycle < 0))
                output_accept_cycle = cycle_count;

            if (done && (done_cycle < 0)) done_cycle = cycle_count;
        end
    end

    always @(posedge clk) begin
        if (!rst_n && cfg_valid && cfg_ready) begin
            $display("FAIL RESET R2 cfg handshake occurred while rst_n=0");
            reset_cfg_handshakes = reset_cfg_handshakes + 1;
            reset_test_errors = reset_test_errors + 1;
            errors = errors + 1;
        end
    end

    always @(posedge m_pixel_valid) begin
        if (rst_n && (m_pixel_valid_assert_cycle < 0)) m_pixel_valid_assert_cycle = cycle_count;
    end

    always @(posedge clk) begin
        if (!rst_n) begin
            p_pending <= 1'b0;
            p_pending_addr <= 9'd0;
            param_requests <= 0;
        end else begin
            if (p_pending && dw_p_rsp_ready) p_pending <= 1'b0;

            if (dw_p_req_valid && dw_p_req_ready) begin
                if (dw_p_req_addr !== param_requests[8:0]) begin
                    $display("FAIL param address expected=%0d actual=%0d", param_requests,
                             dw_p_req_addr);
                    errors = errors + 1;
                end
                p_pending <= 1'b1;
                p_pending_addr <= dw_p_req_addr;
                param_requests <= param_requests + 1;
            end
        end
    end

    always @(posedge clk) begin
        if (rst_n && fault) begin
            $display("FAIL unexpected sticky fault at time %0t", $time);
            $display("DW STEP 1-A FAIL");
            $finish(0);
        end
    end

    task send_tap;
        input integer tap_number;
        reg [63:0] tag_value;
        begin
            tag_value = 64'd0;
            tag_value[7:0] = 8'd11;
            tag_value[15:8] = 8'd7;
            tag_value[19:16] = 4'd0;
            tag_value[26:20] = 7'd0;
            tag_value[30:27] = tap_number[3:0];
            tag_value[37:33] = 5'd1;
            if (tap_number == 8) begin
                tag_value[31] = 1'b1;
                tag_value[32] = 1'b1;
                tag_value[38] = 1'b1;
                tag_value[39] = 1'b1;
            end

            @(negedge clk);
            s_tap_data  = tap_mem[tap_number];
            s_tap_mask  = 32'hffffffff;
            s_tap_tag   = tag_value;
            s_tap_valid = 1'b1;

            while (!s_tap_ready) @(negedge clk);

            @(posedge clk);
            #1;
            s_tap_valid = 1'b0;
            s_tap_data  = 256'd0;
            s_tap_mask  = 32'd0;
            s_tap_tag   = 64'd0;
        end
    endtask

    initial begin
        $readmemh("dw_taps.mem", tap_mem);
        $readmemh("dw_weights.mem", weight_mem);
        $readmemh("dw_params.mem", param_mem);
        $readmemh("dw_expected_acc.mem", expected_acc);
        $readmemh("dw_expected_out.mem", expected_out);

        $dumpfile("dw_step1a.vcd");
        $dumpvars(0, clk, rst_n, cfg_valid, cfg_ready, done, fault, s_tap_valid, s_tap_ready,
                  s_tap_tag, dw_w_req_valid, dw_w_req_ready, dw_w_req_addr, dw_w_rsp_valid,
                  dw_w_rsp_ready, dw_w_rsp_data, dw_p_req_valid, dw_p_req_ready, dw_p_req_addr,
                  dw_p_rsp_valid, dw_p_rsp_ready, dw_p_rsp_data, m_pixel_valid, m_pixel_ready,
                  m_pixel_data, m_pixel_mask, m_pixel_tag, dut.state, dut.tap_index,
                  dut.batch_index, dut.weight_word, dut.accum_probe0, dut.accum_probe1,
                  dut.accum_probe31, dut.requant_lane, dut.current_acc, dut.current_bias,
                  dut.current_m, dut.current_result, dut.p0_valid, dut.p1_valid, dut.p2_valid);

        rst_n = 1'b0;
        cfg_valid = 1'b1;
        cfg_desc = 256'd0;
        s_tap_data = 256'd0;
        s_tap_valid = 1'b0;
        s_tap_mask = 32'd0;
        s_tap_tag = 64'd0;
        m_pixel_ready = 1'b1;
        dw_w_req_ready = 1'b1;
        dw_p_req_ready = 1'b1;
        errors = 0;
        completed = 0;
        reset_test_errors = 0;
        reset_cfg_handshakes = 0;
        reset_cycles_held = 0;
        expected_tag = 64'd0;

        for (
            reset_cycle_index = 0; reset_cycle_index < 5; reset_cycle_index = reset_cycle_index + 1
        ) begin
            @(posedge clk);
            #1;
            reset_cycles_held = reset_cycles_held + 1;
            if (cfg_ready !== 1'b0) begin
                $display("FAIL RESET R1 cfg_ready expected=0 actual=%b reset_cycle=%0d", cfg_ready,
                         reset_cycles_held);
                reset_test_errors = reset_test_errors + 1;
                errors = errors + 1;
            end
        end

        if (reset_test_errors == 0)
            $display("PASS RESET R1 cfg_ready=0 for %0d reset cycles", reset_cycles_held);
        if (reset_cfg_handshakes == 0)
            $display("PASS RESET R2 cfg_valid=1 produced no handshake during reset");

        @(negedge clk);
        rst_n = 1'b1;
        cfg_valid = 1'b0;

        @(posedge clk);
        #1;
        if (cfg_ready !== 1'b1) begin
            $display("FAIL RESET R3 cfg_ready did not recover after reset actual=%b", cfg_ready);
            reset_test_errors = reset_test_errors + 1;
            errors = errors + 1;
        end else begin
            $display("PASS RESET R3 cfg_ready recovered in first normal idle cycle");
        end

        cfg_desc = 256'd0;
        cfg_desc[4:0] = 5'd1;
        cfg_desc[6:5] = 2'd1;
        cfg_desc[8:7] = 2'd0;
        cfg_desc[17:9] = 9'd1;
        cfg_desc[26:18] = 9'd1;
        cfg_desc[35:27] = 9'd1;
        cfg_desc[44:36] = 9'd1;
        cfg_desc[53:45] = 9'd32;
        cfg_desc[62:54] = 9'd32;
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

        for (tap = 0; tap < 9; tap = tap + 1) send_tap(tap);

        for (lane = 0; lane < 32; lane = lane + 1) begin
            if ($signed(dut.accum[lane]) !== $signed(expected_acc[lane])) begin
                $display("FAIL acc lane=%0d expected=%0d actual=%0d", lane,
                         $signed(expected_acc[lane]), $signed(dut.accum[lane]));
                errors = errors + 1;
            end else begin
                $display("PASS acc lane=%0d value=%0d", lane, $signed(dut.accum[lane]));
            end
        end

        while (!m_pixel_valid) begin
            @(posedge clk);
            #1;
        end

        expected_tag = 64'd0;
        expected_tag[7:0] = 8'd11;
        expected_tag[15:8] = 8'd7;
        expected_tag[31] = 1'b1;
        expected_tag[32] = 1'b1;
        expected_tag[37:33] = 5'd1;
        expected_tag[38] = 1'b1;
        expected_tag[39] = 1'b1;

        for (lane = 0; lane < 32; lane = lane + 1) begin
            if (m_pixel_data[lane*8+:8] !== expected_out[0][lane*8+:8]) begin
                $display("FAIL output lane=%0d expected=%0d actual=%0d", lane,
                         expected_out[0][lane*8+:8], m_pixel_data[lane*8+:8]);
                errors = errors + 1;
            end else begin
                $display("PASS output lane=%0d value=%0d", lane, m_pixel_data[lane*8+:8]);
            end
        end

        if (m_pixel_data !== expected_out[0]) begin
            $display("FAIL packed output mismatch");
            errors = errors + 1;
        end
        if (m_pixel_mask !== 32'hffffffff) begin
            $display("FAIL output mask expected=ffffffff actual=%08x", m_pixel_mask);
            errors = errors + 1;
        end
        if (m_pixel_tag !== expected_tag) begin
            $display("FAIL output tag expected=%016x actual=%016x", expected_tag, m_pixel_tag);
            errors = errors + 1;
        end
        if (done !== 1'b0) begin
            $display("FAIL done asserted before output accept");
            errors = errors + 1;
        end
        if (weight_requests != 9) begin
            $display("FAIL weight request count expected=9 actual=%0d", weight_requests);
            errors = errors + 1;
        end
        if (weight_responses != 9) begin
            $display("FAIL weight response count expected=9 actual=%0d", weight_responses);
            errors = errors + 1;
        end
        if (param_requests != 32) begin
            $display("FAIL param request count expected=32 actual=%0d", param_requests);
            errors = errors + 1;
        end
        if (param_responses != 32) begin
            $display("FAIL param response count expected=32 actual=%0d", param_responses);
            errors = errors + 1;
        end
        if (param_ii1_gaps != 0) begin
            $display("FAIL param II=1 gap count expected=0 actual=%0d", param_ii1_gaps);
            errors = errors + 1;
        end

        @(posedge clk);
        #1;
        if (done !== 1'b1) begin
            $display("FAIL done missing one cycle after output accept");
            errors = errors + 1;
        end
        if (cfg_ready !== 1'b1) begin
            $display("FAIL RESET R5 cfg_ready not asserted with completed operation");
            reset_test_errors = reset_test_errors + 1;
            errors = errors + 1;
        end

        @(posedge clk);
        #1;
        if (done !== 1'b0) begin
            $display("FAIL done was not a one-cycle pulse");
            errors = errors + 1;
        end
        if (fault !== 1'b0) begin
            $display("FAIL fault asserted at completion");
            errors = errors + 1;
        end
        if (cfg_ready !== 1'b1) begin
            $display("FAIL RESET R5 cfg_ready did not remain asserted in IDLE");
            reset_test_errors = reset_test_errors + 1;
            errors = errors + 1;
        end else if (done === 1'b0) begin
            $display("PASS RESET R5 done one-cycle pulse, IDLE and cfg_ready recovered");
        end

        completed = 1;
        cfg_to_output_accept_cycles = output_accept_cycle - cfg_accept_cycle;
        first_tap_to_output_accept_cycles = output_accept_cycle - first_tap_accept_cycle;
        issue_count_plus_drain_cycles = 9 + 32 +
        (m_pixel_valid_assert_cycle - last_param_request_cycle);
        first_weight_to_valid_cycles = m_pixel_valid_assert_cycle - first_weight_request_cycle;
        first_tap_to_valid_cycles = m_pixel_valid_assert_cycle - first_tap_accept_cycle;
        contract_latency_cycles = m_pixel_valid_assert_cycle - first_tap_mac_issue_cycle + 1;
        $display(
            "CYCLE_MILESTONES cfg_accept=%0d first_weight_request=%0d first_weight_response=%0d first_tap_accept=%0d ninth_tap_accept=%0d",
            cfg_accept_cycle, first_weight_request_cycle, first_weight_response_cycle,
            first_tap_accept_cycle, ninth_tap_accept_cycle);
        $display(
            "CYCLE_MILESTONES first_param_request=%0d last_param_request=%0d first_param_response=%0d first_requant_issue=%0d last_requant_issue=%0d",
            first_param_request_cycle, last_param_request_cycle, first_param_response_cycle,
            first_requant_issue_cycle, last_requant_issue_cycle);
        $display("CYCLE_MILESTONES m_pixel_valid_assert=%0d output_accept=%0d done=%0d",
                 m_pixel_valid_assert_cycle, output_accept_cycle, done_cycle);
        $display(
            "CYCLE_DELTAS cfg_accept_to_output_accept=%0d first_tap_accept_to_output_accept=%0d",
            cfg_to_output_accept_cycles, first_tap_to_output_accept_cycles);
        $display(
            "CYCLE_CANDIDATES issue_count_plus_drain=%0d first_weight_request_to_valid=%0d first_tap_accept_to_valid=%0d",
            issue_count_plus_drain_cycles, first_weight_to_valid_cycles, first_tap_to_valid_cycles);
        $display(
            "CONTRACT_EVENTS first_tap_mac_issue=%0d first_m_pixel_valid=%0d contract_latency=%0d",
            first_tap_mac_issue_cycle, m_pixel_valid_assert_cycle, contract_latency_cycles);
        for (trace_index = 0; trace_index < 9; trace_index = trace_index + 1) begin
            if (trace_index < 8) begin
                $display(
                    "TAP_TRACE tap=%0d w_req=%0d w_rsp_valid=%0d w_rsp_accept=%0d tap_accept=%0d mac_issue=%0d next_w_req=%0d internal_bubble=%0d",
                    trace_index, tap_w_req_cycle[trace_index], tap_w_rsp_valid_cycle[trace_index],
                    tap_w_rsp_accept_cycle[trace_index], tap_accept_cycle[trace_index],
                    tap_mac_issue_cycle[trace_index], tap_w_req_cycle[trace_index+1],
                    tap_mac_issue_cycle[trace_index+1] - tap_mac_issue_cycle[trace_index] - 1);
            end else begin
                $display(
                    "TAP_TRACE tap=%0d w_req=%0d w_rsp_valid=%0d w_rsp_accept=%0d tap_accept=%0d mac_issue=%0d next_w_req=-1 internal_bubble_to_param=%0d",
                    trace_index, tap_w_req_cycle[trace_index], tap_w_rsp_valid_cycle[trace_index],
                    tap_w_rsp_accept_cycle[trace_index], tap_accept_cycle[trace_index],
                    tap_mac_issue_cycle[trace_index],
                    first_param_request_cycle - tap_mac_issue_cycle[trace_index] - 1);
            end
        end
        $display("CYCLE_STAGES W_REQ first=%0d last=%0d accepted=%0d elapsed=%0d",
                 first_weight_request_cycle, last_weight_request_cycle, weight_requests,
                 last_weight_request_cycle - first_weight_request_cycle);
        $display("CYCLE_STAGES W_RSP first=%0d last=%0d accepted=%0d elapsed=%0d",
                 first_weight_response_cycle, last_weight_response_cycle, weight_responses,
                 last_weight_response_cycle - first_weight_response_cycle);
        $display("CYCLE_STAGES TAP first=%0d last=%0d accepted=%0d elapsed=%0d",
                 first_tap_accept_cycle, ninth_tap_accept_cycle, audit_tap_accepts,
                 ninth_tap_accept_cycle - first_tap_accept_cycle);
        $display("CYCLE_STAGES PARAM_REQ first=%0d last=%0d accepted=%0d elapsed=%0d",
                 first_param_request_cycle, last_param_request_cycle, param_requests,
                 last_param_request_cycle - first_param_request_cycle);
        $display("CYCLE_STAGES PARAM_RSP first=%0d last=%0d accepted=%0d elapsed=%0d",
                 first_param_response_cycle, last_param_response_cycle, param_responses,
                 last_param_response_cycle - first_param_response_cycle);
        $display("CYCLE_STAGES REQUANT first=%0d last=%0d issued=%0d elapsed=%0d",
                 first_requant_issue_cycle, last_requant_issue_cycle, param_responses,
                 last_requant_issue_cycle - first_requant_issue_cycle);
        $display("CYCLE_STAGES PIPELINE_DRAIN start=%0d end=%0d elapsed=%0d",
                 last_requant_issue_cycle, m_pixel_valid_assert_cycle,
                 m_pixel_valid_assert_cycle - last_requant_issue_cycle);
        $display("CYCLE_STAGES EMIT start=%0d end=%0d elapsed=%0d", m_pixel_valid_assert_cycle,
                 output_accept_cycle, output_accept_cycle - m_pixel_valid_assert_cycle);
        $display("RESET_TEST_ERRORS=%0d", reset_test_errors);
        $display(
            "SUMMARY accum_checked=32 outputs_checked=32 weight_requests=%0d weight_responses=%0d param_requests=%0d param_responses=%0d ii1_gaps=%0d errors=%0d",
            weight_requests, weight_responses, param_requests, param_responses, param_ii1_gaps,
            errors);
        if (errors == 0) begin
            $display("PASS RESET R4 existing STEP 1-A functional path unchanged");
            $display("DW STEP 1-A PASS");
        end else begin
            $display("DW STEP 1-A FAIL");
        end
        $finish(0);
    end

    initial begin
        repeat (1000) @(posedge clk);
        if (!completed) begin
            $display("FAIL timeout");
            $display("DW STEP 1-A FAIL");
            $finish(0);
        end
    end

endmodule
