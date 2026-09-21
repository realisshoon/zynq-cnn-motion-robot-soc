`timescale 1ns/1ps
`include "cnn_common_params.vh"

module tb_pointwise_conv_pe_fault;
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
    reg pw_req_ready;
    wire [10:0] pw_req_addr;
    reg pw_rsp_valid;
    wire pw_rsp_ready;
    reg [1023:0] pw_rsp_data;
    reg [255:0] pw_rsp_params;
    wire [6:0] pw_req_group;

    integer errors;
    integer canonical_reset_mismatch;
    integer public_xz;
    integer fault_tests_pass;
    integer fault_tests_total;
    integer timeout_count;
    integer before_errors;
    integer guard;
    reg invalid_cfg_pass;
    reg protocol_fault_pass;
    reg sticky_fault_pass;
    reg new_cfg_blocked_pass;
    reg new_work_blocked_pass;
    reg done_suppressed_pass;
    reg reset_recovery_pass;
    reg fault_priority_pass;

    wire [156:0] public_outputs = {
        cfg_ready, done, fault, s_pixel_ready,
        m_value_data, m_value_valid, m_value_mask, m_value_tag,
        pw_req_valid, pw_req_addr, pw_rsp_ready, pw_req_group
    };

    pointwise_conv_pe dut (
        .clk(clk), .rst_n(rst_n),
        .cfg_valid(cfg_valid), .cfg_ready(cfg_ready), .cfg_desc(cfg_desc),
        .done(done), .fault(fault),
        .s_pixel_data(s_pixel_data), .s_pixel_valid(s_pixel_valid),
        .s_pixel_ready(s_pixel_ready), .s_pixel_mask(s_pixel_mask),
        .s_pixel_tag(s_pixel_tag),
        .m_value_data(m_value_data), .m_value_valid(m_value_valid),
        .m_value_ready(m_value_ready), .m_value_mask(m_value_mask),
        .m_value_tag(m_value_tag),
        .pw_req_valid(pw_req_valid), .pw_req_ready(pw_req_ready),
        .pw_req_addr(pw_req_addr), .pw_rsp_valid(pw_rsp_valid),
        .pw_rsp_ready(pw_rsp_ready), .pw_rsp_data(pw_rsp_data),
        .pw_rsp_params(pw_rsp_params), .pw_req_group(pw_req_group)
    );

    initial clk = 1'b0;
    always #5 clk = ~clk;

    function has_xz;
        input [156:0] value;
        begin
            has_xz = (^value === 1'bx);
        end
    endfunction

    function [255:0] make_cfg;
        input valid_kind;
        reg [255:0] value;
        begin
            value = 256'd0;
            value[`CFG_OP_ID_MSB:`CFG_OP_ID_LSB] = 5'd2;
            value[`CFG_KIND_MSB:`CFG_KIND_LSB] =
                valid_kind ? `CNN_KIND_POINTWISE : 2'd0;
            value[`CFG_MODE_MSB:`CFG_MODE_LSB] = `CNN_MODE_BODY;
            value[`CFG_HIN_MSB:`CFG_HIN_LSB] = 9'd1;
            value[`CFG_WIN_MSB:`CFG_WIN_LSB] = 9'd1;
            value[`CFG_HOUT_MSB:`CFG_HOUT_LSB] = 9'd1;
            value[`CFG_WOUT_MSB:`CFG_WOUT_LSB] = 9'd1;
            value[`CFG_CIN_MSB:`CFG_CIN_LSB] = 9'd1;
            value[`CFG_COUT_MSB:`CFG_COUT_LSB] = 9'd1;
            value[`CFG_SHIFT_MSB:`CFG_SHIFT_LSB] = 6'd0;
            make_cfg = value;
        end
    endfunction

    function [63:0] make_valid_input_tag;
        input unused;
        reg [63:0] value;
        begin
            value = 64'd0;
            value[`TAG_BATCH_LAST_BIT] = 1'b1;
            value[`TAG_PIXEL_LAST_BIT] = 1'b1;
            value[`TAG_OP_ID_MSB:`TAG_OP_ID_LSB] = 5'd1;
            value[`TAG_FRAME_END_BIT] = 1'b1;
            make_valid_input_tag = value;
        end
    endfunction

    task tick;
        begin
            @(posedge clk);
            #1;
        end
    endtask

    task check;
        input condition;
        input [8*96-1:0] message;
        begin
            if (condition !== 1'b1) begin
                errors = errors + 1;
                $display("CHECK_FAIL %0s time=%0t", message, $time);
            end
        end
    endtask

    task idle_inputs;
        begin
            cfg_valid = 1'b0;
            cfg_desc = 256'd0;
            s_pixel_data = 256'd0;
            s_pixel_valid = 1'b0;
            s_pixel_mask = 32'd0;
            s_pixel_tag = 64'd0;
            m_value_ready = 1'b0;
            pw_req_ready = 1'b1;
            pw_rsp_valid = 1'b0;
            pw_rsp_data = 1024'd0;
            pw_rsp_params = 256'd0;
        end
    endtask

    task reset_five_cycles;
        integer index;
        begin
            idle_inputs;
            rst_n = 1'b0;
            for (index = 0; index < 5; index = index + 1) begin
                tick;
                if (has_xz(public_outputs)) begin
                    public_xz = public_xz + 1;
                    $display("PUBLIC_XZ_DURING_RESET cycle=%0d value=%h", index, public_outputs);
                end
                if (public_outputs !== 157'd0) begin
                    canonical_reset_mismatch = canonical_reset_mismatch + 1;
                    $display("CANONICAL_RESET_MISMATCH cycle=%0d value=%h", index, public_outputs);
                end
                check(dut.active === 1'b0, "active not clear during reset");
                check(dut.pending_valid === 1'b0, "pending_valid not clear during reset");
                check(dut.request_hold_valid === 1'b0, "request hold not clear during reset");
                check(dut.pipe_valid === 11'd0, "pipeline valid not clear during reset");
            end
            rst_n = 1'b1;
            tick;
            check(cfg_ready === 1'b1, "cfg not ready after reset release");
            if (has_xz(public_outputs)) begin
                public_xz = public_xz + 1;
                $display("PUBLIC_XZ_AFTER_RESET value=%h", public_outputs);
            end
        end
    endtask

    task accept_cfg;
        input valid_kind;
        begin
            cfg_desc = make_cfg(valid_kind);
            cfg_valid = 1'b1;
            check(cfg_ready === 1'b1, "cfg_ready low before cfg handshake");
            tick;
            cfg_valid = 1'b0;
            cfg_desc = 256'd0;
        end
    endtask

    task check_fault_blocking;
        integer index;
        begin
            cfg_desc = make_cfg(1'b1);
            cfg_valid = 1'b1;
            s_pixel_valid = 1'b1;
            s_pixel_mask = 32'hffffffff;
            s_pixel_tag = 64'hffffffffffffffff;
            pw_rsp_valid = 1'b1;
            for (index = 0; index < 4; index = index + 1) begin
                tick;
                check(fault === 1'b1, "fault not sticky");
                check(cfg_ready === 1'b0, "new cfg not blocked after fault");
                check(s_pixel_ready === 1'b0, "new pixel work not blocked after fault");
                check(pw_req_valid === 1'b0, "new request not blocked after fault");
                check(pw_rsp_ready === 1'b0, "response not blocked after fault");
                check(m_value_valid === 1'b0, "output valid asserted after fault");
                check(done === 1'b0, "done asserted after fault");
            end
            cfg_valid = 1'b0;
            s_pixel_valid = 1'b0;
            pw_rsp_valid = 1'b0;
        end
    endtask

    task send_valid_pixel;
        begin
            s_pixel_data = 256'd7;
            s_pixel_mask = 32'h00000001;
            s_pixel_tag = make_valid_input_tag(1'b0);
            s_pixel_valid = 1'b1;
            guard = 0;
            while ((s_pixel_ready !== 1'b1) && (guard < 50)) begin
                tick;
                guard = guard + 1;
            end
            check(guard < 50, "timeout waiting for pixel ready");
            tick;
            s_pixel_valid = 1'b0;
            s_pixel_data = 256'd0;
            s_pixel_mask = 32'd0;
            s_pixel_tag = 64'd0;
        end
    endtask

    task serve_one_weight_response;
        begin
            pw_req_ready = 1'b1;
            guard = 0;
            while ((pw_req_valid !== 1'b1) && (guard < 100)) begin
                tick;
                guard = guard + 1;
            end
            check(guard < 100, "timeout waiting for weight request");
            check(pw_req_addr === 11'd0, "unexpected request address");
            check(pw_req_group === 7'd0, "unexpected request group");
            tick;
            pw_rsp_data = 1024'd0;
            pw_rsp_params = 256'd0;
            pw_rsp_valid = 1'b1;
            guard = 0;
            while ((pw_rsp_ready !== 1'b1) && (guard < 100)) begin
                tick;
                guard = guard + 1;
            end
            check(guard < 100, "timeout waiting for response ready");
            tick;
            pw_rsp_valid = 1'b0;
        end
    endtask

    task wait_for_output;
        begin
            guard = 0;
            while ((m_value_valid !== 1'b1) && (guard < 100)) begin
                tick;
                guard = guard + 1;
            end
            if (guard >= 100) begin
                timeout_count = timeout_count + 1;
                errors = errors + 1;
                $display("CHECK_FAIL timeout waiting for output");
            end
            check(m_value_mask === 4'b0001, "normal output mask mismatch");
            check(m_value_data === 64'd0, "normal zero-weight output mismatch");
        end
    endtask

    task run_clean_operation;
        begin
            accept_cfg(1'b1);
            check(fault === 1'b0, "fault after valid cfg");
            check(dut.active === 1'b1, "operation did not start");
            send_valid_pixel;
            serve_one_weight_response;
            wait_for_output;
            m_value_ready = 1'b1;
            tick;
            check(done === 1'b1, "clean operation done missing");
            check(fault === 1'b0, "clean operation faulted");
            tick;
            check(done === 1'b0, "done was not one-cycle pulse");
            check(cfg_ready === 1'b1, "cfg not ready after clean completion");
            m_value_ready = 1'b0;
        end
    endtask

    initial begin
        errors = 0;
        canonical_reset_mismatch = 0;
        public_xz = 0;
        fault_tests_pass = 0;
        fault_tests_total = 4;
        timeout_count = 0;
        invalid_cfg_pass = 1'b0;
        protocol_fault_pass = 1'b0;
        sticky_fault_pass = 1'b1;
        new_cfg_blocked_pass = 1'b1;
        new_work_blocked_pass = 1'b1;
        done_suppressed_pass = 1'b1;
        reset_recovery_pass = 1'b0;
        fault_priority_pass = 1'b0;
        rst_n = 1'b0;
        idle_inputs;

        // A. Invalid cfg: wrong kind is an existing live-RTL cfg_error term.
        reset_five_cycles;
        before_errors = errors;
        accept_cfg(1'b0);
        check(fault === 1'b1, "invalid cfg did not set fault");
        check(dut.active === 1'b0, "invalid cfg started operation");
        check_fault_blocking;
        if (errors == before_errors) begin
            invalid_cfg_pass = 1'b1;
            fault_tests_pass = fault_tests_pass + 1;
            $display("FAULT_CASE_PASS INVALID_CFG");
        end

        // B. Protocol fault: valid TAG with an invalid final-batch mask.
        reset_five_cycles;
        before_errors = errors;
        accept_cfg(1'b1);
        s_pixel_data = 256'd1;
        s_pixel_tag = make_valid_input_tag(1'b0);
        s_pixel_mask = 32'd0;
        s_pixel_valid = 1'b1;
        check(s_pixel_ready === 1'b1, "protocol test input not ready");
        tick;
        s_pixel_valid = 1'b0;
        check(fault === 1'b1, "mask mismatch did not set fault");
        check_fault_blocking;
        if (errors == before_errors) begin
            protocol_fault_pass = 1'b1;
            fault_tests_pass = fault_tests_pass + 1;
            $display("FAULT_CASE_PASS PROTOCOL_MASK_MISMATCH");
        end

        // C. Five-cycle synchronous reset recovery followed by normal work.
        before_errors = errors;
        reset_five_cycles;
        check(fault === 1'b0, "fault did not clear on reset");
        run_clean_operation;
        if (errors == before_errors) begin
            reset_recovery_pass = 1'b1;
            fault_tests_pass = fault_tests_pass + 1;
            $display("FAULT_CASE_PASS RESET_RECOVERY_NORMAL_OPERATION");
        end

        // D. Final output handshake and unexpected response on the same edge.
        // The existing outer fault branch must suppress the completion branch.
        reset_five_cycles;
        before_errors = errors;
        accept_cfg(1'b1);
        send_valid_pixel;
        serve_one_weight_response;
        wait_for_output;
        check(dut.pending_valid === 1'b0, "priority injection requires no pending request");
        m_value_ready = 1'b1;
        pw_rsp_valid = 1'b1;
        tick;
        pw_rsp_valid = 1'b0;
        m_value_ready = 1'b0;
        check(fault === 1'b1, "simultaneous protocol fault did not win");
        check(done === 1'b0, "done asserted in fault/completion race");
        check(m_value_valid === 1'b0, "output remained valid after priority fault");
        check_fault_blocking;
        if (errors == before_errors) begin
            fault_priority_pass = 1'b1;
            fault_tests_pass = fault_tests_pass + 1;
            $display("FAULT_CASE_PASS FAULT_OVER_COMPLETION");
        end

        if (fault !== 1'b1) sticky_fault_pass = 1'b0;
        if (cfg_ready !== 1'b0) new_cfg_blocked_pass = 1'b0;
        if ((s_pixel_ready !== 1'b0) || (pw_req_valid !== 1'b0) ||
            (pw_rsp_ready !== 1'b0)) new_work_blocked_pass = 1'b0;
        if (done !== 1'b0) done_suppressed_pass = 1'b0;

        $display("FAULT_INVALID_CFG %0s", invalid_cfg_pass ? "PASS" : "FAIL");
        $display("FAULT_PROTOCOL %0s", protocol_fault_pass ? "PASS" : "FAIL");
        $display("STICKY_FAULT %0s", sticky_fault_pass ? "PASS" : "FAIL");
        $display("NEW_CFG_BLOCKED_AFTER_FAULT %0s", new_cfg_blocked_pass ? "PASS" : "FAIL");
        $display("NEW_WORK_BLOCKED %0s", new_work_blocked_pass ? "PASS" : "FAIL");
        $display("DONE_SUPPRESSED %0s", done_suppressed_pass ? "PASS" : "FAIL");
        $display("RESET_RECOVERY %0s", reset_recovery_pass ? "PASS" : "FAIL");
        $display("FAULT_PRIORITY %0s", fault_priority_pass ? "PASS" : "FAIL");
        $display("PUBLIC_XZ %0d", public_xz);
        $display("CANONICAL_RESET_MISMATCH %0d", canonical_reset_mismatch);
        $display("FAULT_DIRECTED_PASS %0d", fault_tests_pass);
        $display("FAULT_DIRECTED_TOTAL %0d", fault_tests_total);
        $display("FAULT_TIMEOUT %0d", timeout_count);
        $display("FAULT_ERRORS %0d", errors);
        if ((errors == 0) && (fault_tests_pass == fault_tests_total) &&
            (public_xz == 0) && (canonical_reset_mismatch == 0))
            $display("POINTWISE_FAULT_RECOVERY PASS");
        else
            $display("POINTWISE_FAULT_RECOVERY FAIL");
        $finish;
    end
endmodule
