`timescale 1ns/1ps

// CNN-v4.0 self-checking testbench for coord_restore.
//
// Covered contract:
//   - 17 joints accepted in order, back-to-back when ready
//   - 5 enabled-stage pipeline / II=1 before any stall
//   - X/Y Q16 math with signed16 offset and coefficient 15480
//   - final-only signed RNE ties-to-even
//   - signed threshold comparison
//   - x/y image boundaries and invalid x=y=0 with score preserved
//   - threshold snapshot at cfg boundary
//   - output backpressure freezes the full pipeline
//   - data/index/last/good stability while stalled
//   - joint16 output accept -> next-cycle done pulse
//   - sticky protocol fault and synchronous reset recovery
//
// Expected result words below were generated independently from the
// CNN-v4 Python integer-reference contract, not from the DUT RTL.
//
module tb_coord_restore;

reg clk;
reg rst_n;

reg cfg_valid;
wire cfg_ready;
reg [255:0] cfg_desc;

wire done;
wire fault;

reg [63:0] s_result_data;
reg s_result_valid;
wire s_result_ready;
reg s_result_last;
reg [4:0] s_result_joint;

wire [31:0] m_joint_data;
wire m_joint_valid;
wire m_joint_ready;
wire m_joint_last;
wire [4:0] m_joint_index;
wire m_joint_good;

reg [7:0] threshold;

// -----------------------------------------------------------------------------
// DUT
// -----------------------------------------------------------------------------
coord_restore dut (
    .clk(clk),
    .rst_n(rst_n),

    .cfg_valid(cfg_valid),
    .cfg_ready(cfg_ready),
    .cfg_desc(cfg_desc),

    .done(done),
    .fault(fault),

    .s_result_data(s_result_data),
    .s_result_valid(s_result_valid),
    .s_result_ready(s_result_ready),
    .s_result_last(s_result_last),
    .s_result_joint(s_result_joint),

    .m_joint_data(m_joint_data),
    .m_joint_valid(m_joint_valid),
    .m_joint_ready(m_joint_ready),
    .m_joint_last(m_joint_last),
    .m_joint_index(m_joint_index),
    .m_joint_good(m_joint_good),

    .threshold(threshold)
);

// 100 MHz
initial clk = 1'b0;
always #5 clk = ~clk;

// -----------------------------------------------------------------------------
// Test vectors
// -----------------------------------------------------------------------------
reg [7:0]  vec_col   [0:16];
reg [7:0]  vec_row   [0:16];
reg [15:0] vec_ox    [0:16];
reg [15:0] vec_oy    [0:16];
reg [7:0]  vec_score [0:16];

reg [31:0] exp_word  [0:16];
reg        exp_good  [0:16];

// Useful for human-readable failure messages.
integer exp_x [0:16];
integer exp_y [0:16];

// Independent Python-reference expected vectors, threshold=-46.
//
// j0  : ordinary signed offsets
// j1  : score == threshold (-46) -> pass
// j2  : score -47 -> threshold fail, score preserved
// j3  : x == 0 boundary
// j4  : x == 1279 boundary
// j5  : x == 1280 -> invalid
// j6  : y == 0 boundary with negative intermediate Q16
// j7  : y == 719 boundary
// j8  : y == 720 -> invalid
// j9  : negative y -> invalid
// j10 : positive x offset / negative y offset
// j11 : negative x/y offsets, still valid
// j12 : exact +0.5 tie where floor q is even -> remain even (x=232)
// j13 : exact +0.5 tie where floor q is odd  -> round to even (y=688)
// j14 : proves threshold is snapshotted at cfg; live threshold later changes
// j15 : signed score +127
// j16 : signed score -128 -> invalid, final packet beat
initial begin
    // col,row,ox,oy,score
    vec_col[0]   = 8'd5;  vec_row[0]   = 8'd7;
    vec_ox[0]    = -16'sd100; vec_oy[0] = 16'sd200;
    vec_score[0] = -8'sd20;
    exp_word[0]  = 32'hEC147178; exp_good[0] = 1'b1;
    exp_x[0] = 376; exp_y[0] = 327;

    vec_col[1]   = 8'd1;  vec_row[1]   = 8'd4;
    vec_ox[1]    = 16'sd0; vec_oy[1] = 16'sd0;
    vec_score[1] = -8'sd46;
    exp_word[1]  = 32'hD2028050; exp_good[1] = 1'b1;
    exp_x[1] = 80; exp_y[1] = 40;

    vec_col[2]   = 8'd1;  vec_row[2]   = 8'd4;
    vec_ox[2]    = 16'sd0; vec_oy[2] = 16'sd0;
    vec_score[2] = -8'sd47;
    exp_word[2]  = 32'hD1000000; exp_good[2] = 1'b0;
    exp_x[2] = 80; exp_y[2] = 40;

    vec_col[3]   = 8'd0;  vec_row[3]   = 8'd4;
    vec_ox[3]    = 16'sd0; vec_oy[3] = 16'sd0;
    vec_score[3] = 8'sd0;
    exp_word[3]  = 32'h00028000; exp_good[3] = 1'b1;
    exp_x[3] = 0; exp_y[3] = 40;

    vec_col[4]   = 8'd15; vec_row[4]   = 8'd4;
    vec_ox[4]    = 16'sd333; vec_oy[4] = 16'sd0;
    vec_score[4] = 8'sd10;
    exp_word[4]  = 32'h0A0284FF; exp_good[4] = 1'b1;
    exp_x[4] = 1279; exp_y[4] = 40;

    vec_col[5]   = 8'd15; vec_row[5]   = 8'd4;
    vec_ox[5]    = 16'sd337; vec_oy[5] = 16'sd0;
    vec_score[5] = 8'sd11;
    exp_word[5]  = 32'h0B000000; exp_good[5] = 1'b0;
    exp_x[5] = 1280; exp_y[5] = 40;

    vec_col[6]   = 8'd1;  vec_row[6]   = 8'd0;
    vec_ox[6]    = 16'sd0; vec_oy[6] = 16'sd1184;
    vec_score[6] = 8'sd12;
    exp_word[6]  = 32'h0C000050; exp_good[6] = 1'b1;
    exp_x[6] = 80; exp_y[6] = 0;

    vec_col[7]   = 8'd1;  vec_row[7]   = 8'd7;
    vec_ox[7]    = 16'sd0; vec_oy[7] = 16'sd1857;
    vec_score[7] = 8'sd13;
    exp_word[7]  = 32'h0D2CF050; exp_good[7] = 1'b1;
    exp_x[7] = 80; exp_y[7] = 719;

    vec_col[8]   = 8'd1;  vec_row[8]   = 8'd7;
    vec_ox[8]    = 16'sd0; vec_oy[8] = 16'sd1861;
    vec_score[8] = 8'sd14;
    exp_word[8]  = 32'h0E000000; exp_good[8] = 1'b0;
    exp_x[8] = 80; exp_y[8] = 720;

    vec_col[9]   = 8'd1;  vec_row[9]   = 8'd3;
    vec_ox[9]    = 16'sd0; vec_oy[9] = 16'sd0;
    vec_score[9] = 8'sd15;
    exp_word[9]  = 32'h0F000000; exp_good[9] = 1'b0;
    exp_x[9] = 80; exp_y[9] = -40;

    vec_col[10]   = 8'd2; vec_row[10]   = 8'd5;
    vec_ox[10]    = 16'sd500; vec_oy[10] = -16'sd300;
    vec_score[10] = 8'sd16;
    exp_word[10]  = 32'h10031116; exp_good[10] = 1'b1;
    exp_x[10] = 278; exp_y[10] = 49;

    vec_col[11]   = 8'd10; vec_row[11]   = 8'd10;
    vec_ox[11]    = -16'sd200; vec_oy[11] = -16'sd100;
    vec_score[11] = 8'sd17;
    exp_word[11]  = 32'h111F02F1; exp_good[11] = 1'b1;
    exp_x[11] = 753; exp_y[11] = 496;

    vec_col[12]   = 8'd15; vec_row[12]   = 8'd4;
    vec_ox[12]    = -16'sd4096; vec_oy[12] = 16'sd0;
    vec_score[12] = 8'sd20;
    exp_word[12]  = 32'h140280E8; exp_good[12] = 1'b1;
    exp_x[12] = 232; exp_y[12] = 40;

    vec_col[13]   = 8'd1; vec_row[13]   = 8'd0;
    vec_ox[13]    = 16'sd0; vec_oy[13] = 16'sd4096;
    vec_score[13] = 8'sd21;
    exp_word[13]  = 32'h152B0050; exp_good[13] = 1'b1;
    exp_x[13] = 80; exp_y[13] = 688;

    vec_col[14]   = 8'd3; vec_row[14]   = 8'd6;
    vec_ox[14]    = 16'sd0; vec_oy[14] = 16'sd0;
    vec_score[14] = -8'sd45;
    exp_word[14]  = 32'hD30C80F0; exp_good[14] = 1'b1;
    exp_x[14] = 240; exp_y[14] = 200;

    vec_col[15]   = 8'd14; vec_row[15]   = 8'd12;
    vec_ox[15]    = 16'sd100; vec_oy[15] = 16'sd100;
    vec_score[15] = 8'sd127;
    exp_word[15]  = 32'h7F2C0478; exp_good[15] = 1'b1;
    exp_x[15] = 1144; exp_y[15] = 704;

    vec_col[16]   = 8'd4; vec_row[16]   = 8'd6;
    vec_ox[16]    = 16'sd0; vec_oy[16] = 16'sd0;
    vec_score[16] = -8'sd128;
    exp_word[16]  = 32'h80000000; exp_good[16] = 1'b0;
    exp_x[16] = 320; exp_y[16] = 200;
end

function [63:0] pack_argmax_result;
    input [7:0] col;
    input [7:0] row;
    input [15:0] ox;
    input [15:0] oy;
    input [7:0] score;
    begin
        pack_argmax_result = {
            8'h00,   // reserved[63:56]
            score,   // score[55:48]
            oy,      // offset_y[47:32]
            ox,      // offset_x[31:16]
            row,     // grid_row[15:8]
            col      // grid_col[7:0]
        };
    end
endfunction

// Canonical op28 descriptor. coord_restore does not use its geometry fields
// in the datapath, but the TB presents the same style of cfg payload as top.
task build_cfg_op28;
    begin
        cfg_desc = 256'd0;
        cfg_desc[4:0]     = 5'd28;       // op_id
        cfg_desc[6:5]     = 2'd2;        // kind = PW
        cfg_desc[8:7]     = 2'd2;        // mode = offset
        cfg_desc[17:9]    = 9'd16;       // hin
        cfg_desc[26:18]   = 9'd16;       // win
        cfg_desc[35:27]   = 9'd16;       // hout
        cfg_desc[44:36]   = 9'd16;       // wout
        cfg_desc[53:45]   = 9'd384;      // cin
        cfg_desc[62:54]   = 9'd34;       // cout
        cfg_desc[64:63]   = 2'd1;        // stride
        cfg_desc[66:65]   = 2'd1;        // dilation
        cfg_desc[68:67]   = 2'd0;        // pad
        cfg_desc[74:69]   = 6'd16;       // shift
        cfg_desc[106:75]  = 32'd1273536; // weight_offset
        cfg_desc[124:107] = 18'd13824;   // param_offset
        cfg_desc[144:125] = 20'd14144;   // dma_bytes
        cfg_desc[148:145] = 4'd15;       // stage_id
        cfg_desc[255:149] = 107'd0;
    end
endtask

// -----------------------------------------------------------------------------
// Output backpressure.
// Stall joint8 for exactly 3 full clock accepts and joint16 for 2.
// Because ready is combinational from the registered output joint, the stall
// begins before that output can be accepted.
// -----------------------------------------------------------------------------
integer stall8_count;
integer stall16_count;

assign m_joint_ready =
    !(
        (m_joint_valid && (m_joint_index == 5'd8)  && (stall8_count  < 3)) ||
        (m_joint_valid && (m_joint_index == 5'd16) && (stall16_count < 2))
     );

always @(posedge clk) begin
    if (!rst_n) begin
        stall8_count  = 0;
        stall16_count = 0;
    end
    else begin
        if (m_joint_valid && (m_joint_index == 5'd8) && !m_joint_ready)
            stall8_count = stall8_count + 1;

        if (m_joint_valid && (m_joint_index == 5'd16) && !m_joint_ready)
            stall16_count = stall16_count + 1;
    end
end

// -----------------------------------------------------------------------------
// Scoreboard / protocol monitors
// -----------------------------------------------------------------------------
integer errors;
integer checks;
integer cycle_count;
integer input_accept_count;
integer output_accept_count;
integer expected_output_joint;
integer first_input_cycle;
integer first_output_cycle;

reg monitor_enable;
reg main_done_seen;
reg expect_done_after_accept;

reg stall_hold_active;
reg [31:0] held_data;
reg [4:0] held_index;
reg held_last;
reg held_good;

task fail;
    input [1023:0] msg;
    begin
        errors = errors + 1;
        $display("[FAIL][%0t] %0s", $time, msg);
    end
endtask

task pass;
    input [1023:0] msg;
    begin
        checks = checks + 1;
        $display("[PASS][%0t] %0s", $time, msg);
    end
endtask

// Count accepted input beats and remember the first acceptance edge.
always @(posedge clk) begin
    if (!rst_n) begin
        cycle_count = 0;
        input_accept_count = 0;
        first_input_cycle = -1;
    end
    else begin
        cycle_count = cycle_count + 1;

        if (monitor_enable && s_result_valid && s_result_ready) begin
            input_accept_count = input_accept_count + 1;
            if (first_input_cycle < 0)
                first_input_cycle = cycle_count;
        end
    end
end

// Check outputs after posedge/NBA updates have settled.
always @(negedge clk) begin
    if (!rst_n) begin
        output_accept_count = 0;
        expected_output_joint = 0;
        first_output_cycle = -1;
        main_done_seen = 1'b0;
        expect_done_after_accept = 1'b0;

        stall_hold_active = 1'b0;
        held_data = 32'd0;
        held_index = 5'd0;
        held_last = 1'b0;
        held_good = 1'b0;
    end
    else if (monitor_enable) begin
        // done must correspond exactly to the previous edge's final output
        // acceptance.  It must not occur early.
        if (expect_done_after_accept) begin
            if (done !== 1'b1)
                fail("joint16 was accepted but done is not high in the following cycle");
            else begin
                pass("joint16 accept -> next-cycle done pulse");
                main_done_seen = 1'b1;
            end
        end
        else if (done === 1'b1) begin
            fail("done asserted without a preceding joint16 output acceptance");
        end

        // The DUT should not accept a new cfg while an operation is active.
        if (!done && (output_accept_count < 17) && (input_accept_count > 0)) begin
            if (cfg_ready !== 1'b0)
                fail("cfg_ready asserted while coord_restore operation is active");
        end

        if (m_joint_valid) begin
            if (expected_output_joint > 16) begin
                fail("more than 17 result beats were produced");
            end
            else begin
                if (m_joint_index !== expected_output_joint[4:0]) begin
                    $display("  expected index=%0d actual=%0d",
                             expected_output_joint, m_joint_index);
                    fail("m_joint_index sequence mismatch");
                end

                if (m_joint_last !== (expected_output_joint == 16))
                    fail("m_joint_last does not match joint16 boundary");

                if (m_joint_data !== exp_word[expected_output_joint]) begin
                    $display("  joint=%0d expected_word=%08h actual_word=%08h",
                             expected_output_joint,
                             exp_word[expected_output_joint],
                             m_joint_data);
                    $display("  expected decoded x=%0d y=%0d good=%0d",
                             exp_x[expected_output_joint],
                             exp_y[expected_output_joint],
                             exp_good[expected_output_joint]);
                    fail("Python-reference packed word mismatch");
                end

                if (m_joint_good !== exp_good[expected_output_joint]) begin
                    $display("  joint=%0d expected_good=%0d actual_good=%0d",
                             expected_output_joint,
                             exp_good[expected_output_joint],
                             m_joint_good);
                    fail("m_joint_good mismatch");
                end

                if (first_output_cycle < 0 &&
                    (m_joint_index == 5'd0)) begin
                    first_output_cycle = cycle_count;

                    // Input accept edge is counted as S0 (stage 1).
                    // S4 becomes visible four later rising edges: five
                    // enabled stages inclusive.
                    if ((first_output_cycle - first_input_cycle) != 4) begin
                        $display("  first_input_cycle=%0d first_output_cycle=%0d",
                                 first_input_cycle, first_output_cycle);
                        fail("first result does not traverse the specified 5 enabled stages");
                    end
                    else begin
                        pass("5-stage no-stall latency observed for joint0");
                    end
                end
            end
        end

        // General stall-stability checker.
        if (m_joint_valid && !m_joint_ready) begin
            if (!stall_hold_active) begin
                stall_hold_active = 1'b1;
                held_data  = m_joint_data;
                held_index = m_joint_index;
                held_last  = m_joint_last;
                held_good  = m_joint_good;
            end
            else begin
                if (m_joint_data !== held_data)
                    fail("m_joint_data changed during output stall");
                if (m_joint_index !== held_index)
                    fail("m_joint_index changed during output stall");
                if (m_joint_last !== held_last)
                    fail("m_joint_last changed during output stall");
                if (m_joint_good !== held_good)
                    fail("m_joint_good changed during output stall");
            end

            // Full-pipeline freeze means upstream must also be backpressured.
            if (s_result_ready !== 1'b0)
                fail("s_result_ready remained high while output stalled");
        end
        else begin
            if (stall_hold_active)
                pass("output payload/index/last/good remained stable during stall");
            stall_hold_active = 1'b0;
        end

        // Capture whether the next clock edge will accept joint16.
        // At the following negedge the DUT must present done=1.
        expect_done_after_accept =
            m_joint_valid && m_joint_ready && m_joint_last;

        if (m_joint_valid && m_joint_ready) begin
            if (expected_output_joint <= 16) begin
                $display("[PASS][%0t] joint%0d result = %08h good=%0d",
                         $time,
                         expected_output_joint,
                         m_joint_data,
                         m_joint_good);
                checks = checks + 1;
            end

            output_accept_count = output_accept_count + 1;
            expected_output_joint = expected_output_joint + 1;
        end
    end
end

// -----------------------------------------------------------------------------
// Stimulus helpers
// -----------------------------------------------------------------------------

// Synchronous active-low reset, asserted for at least four rising edges.
task apply_reset;
    begin
        @(negedge clk);
        rst_n = 1'b0;
        cfg_valid = 1'b0;
        s_result_valid = 1'b0;
        s_result_data = 64'd0;
        s_result_last = 1'b0;
        s_result_joint = 5'd0;

        repeat (4) @(posedge clk);

        // Check after reset has been sampled on a rising edge and all NBA
        // updates have settled. Exact comparisons also reject X/Z values.
        @(negedge clk);
        if (cfg_ready !== 1'b0)
            fail("cfg_ready is not zero during synchronous reset");
        else
            pass("cfg_ready is zero during synchronous reset");

        if ((^({cfg_ready, done, fault, s_result_ready,
                m_joint_data, m_joint_valid, m_joint_last,
                m_joint_index, m_joint_good})) === 1'bx)
            fail("public output contains X/Z after sampled reset edge");
        else if ({cfg_ready, done, fault, s_result_ready,
                  m_joint_data, m_joint_valid, m_joint_last,
                  m_joint_index, m_joint_good} !== 44'd0)
            fail("public output does not match canonical zero reset value");
        else
            pass("4-state public output reset values are canonical and known");

        rst_n = 1'b1;

        // Let combinational ready settle in the newly reset state.
        @(negedge clk);
    end
endtask

task send_cfg;
    begin
        while (cfg_ready !== 1'b1)
            @(negedge clk);

        cfg_valid = 1'b1;
        @(negedge clk); // handshake occurred at the intervening posedge
        cfg_valid = 1'b0;
    end
endtask

// Sends all 17 joints with valid continuously asserted.
// If the DUT deasserts ready because of downstream stall, the current payload
// is held unchanged until it is accepted.
task send_all_joints;
    integer idx;
    begin
        idx = 0;

        @(negedge clk);
        s_result_valid = 1'b1;
        s_result_data =
            pack_argmax_result(
                vec_col[idx], vec_row[idx],
                vec_ox[idx], vec_oy[idx],
                vec_score[idx]
            );
        s_result_joint = idx[4:0];
        s_result_last = (idx == 16);

        while (idx < 17) begin
            // A transfer is defined and sampled on the accepting rising edge.
            // Update/deassert the payload only at the following falling edge,
            // after the DUT has sampled the accepted beat.
            @(posedge clk);
            if (s_result_valid && (s_result_ready === 1'b1)) begin
                idx = idx + 1;

                @(negedge clk);

                // Change the live threshold after joint0 has been accepted.
                // The operation must continue using the -46 snapshot taken
                // at cfg acceptance.  Joint14(score=-45) proves this.
                if (idx == 1)
                    threshold = 8'h7F;

                if (idx < 17) begin
                    s_result_data =
                        pack_argmax_result(
                            vec_col[idx], vec_row[idx],
                            vec_ox[idx], vec_oy[idx],
                            vec_score[idx]
                        );
                    s_result_joint = idx[4:0];
                    s_result_last = (idx == 16);
                end
                else begin
                    s_result_valid = 1'b0;
                    s_result_data = 64'd0;
                    s_result_joint = 5'd0;
                    s_result_last = 1'b0;
                end
            end
            else begin
                @(negedge clk);
            end
        end
    end
endtask

// One deliberately malformed beat for sticky-fault testing.
// coord_restore expects joint0, but this drives joint1.
task send_bad_first_joint;
    begin
        while (s_result_ready !== 1'b1)
            @(negedge clk);

        s_result_valid = 1'b1;
        s_result_joint = 5'd1; // protocol error: expected 0
        s_result_last = 1'b0;
        s_result_data =
            pack_argmax_result(
                8'd1, 8'd4,
                16'd0, 16'd0,
                8'd0
            );

        @(negedge clk); // malformed beat accepted at previous posedge

        s_result_valid = 1'b0;
        s_result_joint = 5'd0;
        s_result_last = 1'b0;
        s_result_data = 64'd0;
    end
endtask

// -----------------------------------------------------------------------------
// Main test
// -----------------------------------------------------------------------------
initial begin
    errors = 0;
    checks = 0;

    rst_n = 1'b0;
    cfg_valid = 1'b0;
    cfg_desc = 256'd0;

    s_result_data = 64'd0;
    s_result_valid = 1'b0;
    s_result_last = 1'b0;
    s_result_joint = 5'd0;

    threshold = 8'hD2; // signed -46

    monitor_enable = 1'b0;
    main_done_seen = 1'b0;

    build_cfg_op28;

    $dumpfile("tb_coord_restore.vcd");
    $dumpvars(0, tb_coord_restore);

    $display("============================================================");
    $display(" coord_restore CNN-v4.0 self-checking testbench");
    $display("============================================================");

    // ---------------------------------------------------------------------
    // TEST 1: Functional/reference/pipeline/stall coverage
    // ---------------------------------------------------------------------
    $display("\n[TEST 1] 17-joint Python-reference comparison");

    apply_reset;

    if (fault !== 1'b0)
        fail("fault did not clear after reset");
    if (cfg_ready !== 1'b1)
        fail("cfg_ready is not high in idle after reset");
    if (done !== 1'b0)
        fail("done is not low after reset");
    if (m_joint_valid !== 1'b0)
        fail("m_joint_valid is not low after reset");

    threshold = 8'hD2; // -46 at cfg snapshot
    monitor_enable = 1'b1;

    send_cfg;

    if (cfg_ready !== 1'b0)
        fail("cfg_ready did not drop after cfg acceptance");
    else
        pass("cfg accepted and module entered active operation");

    send_all_joints;

    // Wait for all 17 outputs and the completion pulse.
    while (!main_done_seen)
        @(negedge clk);

    // One more cycle: done must return low.
    @(negedge clk);
    if (done !== 1'b0)
        fail("done pulse lasted longer than one cycle");
    else
        pass("done is a one-cycle pulse");

    if (input_accept_count != 17) begin
        $display("  accepted inputs=%0d", input_accept_count);
        fail("input accept count is not 17");
    end
    else
        pass("exactly 17 input joints accepted");

    if (output_accept_count != 17) begin
        $display("  accepted outputs=%0d", output_accept_count);
        fail("output accept count is not 17");
    end
    else
        pass("exactly 17 output joints accepted");

    if (stall8_count != 3) begin
        $display("  joint8 stall cycles=%0d", stall8_count);
        fail("joint8 did not stall for exactly 3 cycles");
    end
    else
        pass("joint8 output stall exercised for 3 cycles");

    if (stall16_count != 2) begin
        $display("  joint16 stall cycles=%0d", stall16_count);
        fail("joint16 did not stall for exactly 2 cycles");
    end
    else
        pass("joint16 output stall exercised for 2 cycles");

    if (cfg_ready !== 1'b1)
        fail("module did not return to idle after final output");
    else
        pass("module returned to idle after final output accept");

    monitor_enable = 1'b0;

    // ---------------------------------------------------------------------
    // TEST 2: Sticky fault / new-work block / reset recovery
    // ---------------------------------------------------------------------
    $display("\n[TEST 2] sticky protocol fault and reset recovery");

    apply_reset;
    build_cfg_op28;
    threshold = 8'hD2;

    send_cfg;
    send_bad_first_joint;

    @(negedge clk);

    if (fault !== 1'b1)
        fail("wrong first joint did not raise sticky fault");
    else
        pass("wrong first joint raised sticky fault");

    if (cfg_ready !== 1'b0)
        fail("cfg_ready not blocked during sticky fault");
    else
        pass("sticky fault blocks new cfg");

    if (s_result_ready !== 1'b0)
        fail("s_result_ready not blocked during sticky fault");
    else
        pass("sticky fault blocks new input work");

    if (done !== 1'b0)
        fail("done asserted with sticky fault");
    else
        pass("fault has priority over done");

    // Fault must remain set without reset.
    repeat (3) begin
        @(negedge clk);
        if (fault !== 1'b1)
            fail("fault cleared without rst_n");
    end
    pass("fault remains sticky until reset");

    // Synchronous reset must recover the module.
    apply_reset;

    if (fault !== 1'b0)
        fail("fault did not clear after synchronous reset");
    else
        pass("synchronous reset clears sticky fault");

    if (cfg_ready !== 1'b1)
        fail("cfg_ready did not recover after reset");
    else
        pass("module accepts new work again after reset");

    // ---------------------------------------------------------------------
    // Final summary
    // ---------------------------------------------------------------------
    $display("\n============================================================");
    $display(" checks = %0d", checks);
    $display(" errors = %0d", errors);

    if (errors == 0)
        $display(" TEST PASS: coord_restore contract checks passed");
    else
        $display(" TEST FAIL: coord_restore has %0d error(s)", errors);

    $display(" VCD = tb_coord_restore.vcd");
    $display("============================================================");

    $finish;
end

// Hard timeout protects against handshake/deadlock regressions.
initial begin
    #200000;
    $display("\n[TIMEOUT] tb_coord_restore exceeded 200 us");
    $display("TEST FAIL");
    $finish;
end

endmodule
