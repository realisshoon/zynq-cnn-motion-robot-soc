`timescale 1ns / 1ps

// CNN-v4.0 argmax_threshold self-checking testbench
// Verilog-2001 only.
//
// Covered contract items:
//   1) all -128 first-hit behavior, strict-greater tie retention,
//      negative score, last-pixel maximum
//   2) heatmap final accept -> done pulse -> offset cfg while best state survives
//   3) Cout17/Cout34 tail masks, y=c / x=c+17 offset mapping
//   4) result backpressure stability, joint0..16 order, joint16 accept -> done
//   5) sticky fault and reset recovery for illegal offset-before-heatmap cfg
//
// Expected result words below were generated from the Python unit reference
// using the exact deterministic tensors produced by heat_value()/offset_value().

module tb_argmax_threshold;

    reg clk;
    reg rst_n;

    reg          cfg_valid;
    wire         cfg_ready;
    reg  [255:0] cfg_desc;
    wire         done;
    wire         fault;

    reg  [63:0]  s_head_data;
    reg          s_head_valid;
    wire         s_head_ready;
    reg  [3:0]   s_head_mask;
    reg  [63:0]  s_head_tag;

    wire [63:0]  m_result_data;
    wire         m_result_valid;
    reg          m_result_ready;
    wire         m_result_last;
    wire [4:0]   m_result_joint;

    integer errors;
    integer checks;
    integer r;
    integer c;
    integer g;
    integer joint;
    integer stall_cycles;

    reg [63:0] expected_result [0:16];
    reg [63:0] hold_data;
    reg [4:0]  hold_joint;
    reg        hold_last;

    localparam [1:0] MODE_HEATMAP = 2'd1;
    localparam [1:0] MODE_OFFSET  = 2'd2;

    localparam [4:0] OP_HEATMAP = 5'd27;
    localparam [4:0] OP_OFFSET  = 5'd28;

    argmax_threshold dut (
        .clk            (clk),
        .rst_n          (rst_n),
        .cfg_valid      (cfg_valid),
        .cfg_ready      (cfg_ready),
        .cfg_desc       (cfg_desc),
        .done           (done),
        .fault          (fault),
        .s_head_data    (s_head_data),
        .s_head_valid   (s_head_valid),
        .s_head_ready   (s_head_ready),
        .s_head_mask    (s_head_mask),
        .s_head_tag     (s_head_tag),
        .m_result_data  (m_result_data),
        .m_result_valid (m_result_valid),
        .m_result_ready (m_result_ready),
        .m_result_last  (m_result_last),
        .m_result_joint (m_result_joint)
    );

    // 100 MHz clock
    initial clk = 1'b0;
    always #5 clk = ~clk;

    // ------------------------------------------------------------------
    // Helper functions
    // ------------------------------------------------------------------

    function [255:0] make_cfg;
        input [1:0] mode;
        input [8:0] cout;
        input [4:0] op_id;
        input [3:0] stage_id;
        reg [255:0] d;
        begin
            d = 256'd0;

            d[4:0]     = op_id;       // op_id
            d[6:5]     = 2'd2;        // kind = PW
            d[8:7]     = mode;        // body/heatmap/offset
            d[17:9]    = 9'd16;       // hin
            d[26:18]   = 9'd16;       // win
            d[35:27]   = 9'd16;       // hout
            d[44:36]   = 9'd16;       // wout
            d[53:45]   = 9'd384;      // cin
            d[62:54]   = cout;        // cout = 17 / 34
            d[64:63]   = 2'd1;        // stride
            d[66:65]   = 2'd1;        // dilation
            d[68:67]   = 2'd0;        // pad
            d[74:69]   = 6'd16;       // shift
            d[148:145] = stage_id;    // 14 / 15
            // reserved[255:149] stays 0

            make_cfg = d;
        end
    endfunction

    function [63:0] make_tag;
        input integer row_i;
        input integer col_i;
        input integer group_i;
        input [4:0] op_id;
        input integer group_last_i;
        input integer frame_end_i;
        reg [63:0] t;
        begin
            t = 64'd0;
            t[7:0]   = col_i;
            t[15:8]  = row_i;
            t[19:16] = 4'd0;              // batch normalized to 0
            t[26:20] = group_i;
            t[30:27] = 4'd0;              // tap unused
            t[31]    = 1'b0;              // batch_last unused here
            t[32]    = (group_last_i != 0);   // pixel_last
            t[37:33] = op_id;
            t[38]    = (frame_end_i != 0);
            t[39]    = (group_last_i != 0);   // group_last
            t[63:40] = 24'd0;
            make_tag = t;
        end
    endfunction

    function [7:0] heat_value;
        input integer row_i;
        input integer col_i;
        input integer joint_i;
        integer value_i;
        integer best_r;
        integer best_c;
        begin
            value_i = -110;

            // joint0: all -128 -> first row-major pixel must remain selected.
            if (joint_i == 0) begin
                value_i = -128;
            end

            // joint1: equal maxima; the earlier (2,3) must win over (10,10).
            else if (joint_i == 1) begin
                value_i = -100;
                if (((row_i == 2) && (col_i == 3)) ||
                    ((row_i == 10) && (col_i == 10)))
                    value_i = 42;
            end

            // joint2: explicit last-pixel maximum.
            else if (joint_i == 2) begin
                value_i = -90;
                if ((row_i == 15) && (col_i == 15))
                    value_i = 100;
            end

            // joint3: maximum is still negative, checks signed compare.
            else if (joint_i == 3) begin
                value_i = -100;
                if ((row_i == 4) && (col_i == 5))
                    value_i = -5;
            end

            // joints4..15: deterministic unique maxima.
            else if ((joint_i >= 4) && (joint_i <= 15)) begin
                best_r = (joint_i * 5 + 1) % 16;
                best_c = (joint_i * 3 + 2) % 16;
                value_i = -110;
                if ((row_i == best_r) && (col_i == best_c))
                    value_i = 10 + joint_i;
            end

            // joint16: its maximum is on the final heatmap beat.  This also
            // makes x-offset joint16 arrive on the final offset beat.
            else if (joint_i == 16) begin
                value_i = -110;
                if ((row_i == 15) && (col_i == 15))
                    value_i = 26;
            end

            heat_value = value_i;
        end
    endfunction

    function [15:0] sign_extend8;
        input [7:0] v;
        begin
            sign_extend8 = {{8{v[7]}}, v};
        end
    endfunction

    function [15:0] offset_value;
        input integer row_i;
        input integer col_i;
        input integer channel_i;
        integer value_i;
        integer joint_i;
        begin
            if (channel_i < 17) begin
                // yoffset channel c -> joint c
                value_i = 100 + row_i * 64 + col_i * 2 + channel_i;
            end else begin
                // xoffset channel 17+j -> joint j
                joint_i = channel_i - 17;
                value_i = -1000 - row_i * 64 - col_i * 2 - joint_i;
            end
            offset_value = value_i;
        end
    endfunction

    function [63:0] make_heat_beat;
        input integer row_i;
        input integer col_i;
        input integer group_i;
        integer ch;
        reg [63:0] d;
        begin
            d = 64'd0;

            ch = group_i * 4;
            if (ch < 17)
                d[15:0] = sign_extend8(heat_value(row_i, col_i, ch));

            ch = group_i * 4 + 1;
            if (ch < 17)
                d[31:16] = sign_extend8(heat_value(row_i, col_i, ch));

            ch = group_i * 4 + 2;
            if (ch < 17)
                d[47:32] = sign_extend8(heat_value(row_i, col_i, ch));

            ch = group_i * 4 + 3;
            if (ch < 17)
                d[63:48] = sign_extend8(heat_value(row_i, col_i, ch));

            make_heat_beat = d;
        end
    endfunction

    function [63:0] make_offset_beat;
        input integer row_i;
        input integer col_i;
        input integer group_i;
        integer ch;
        reg [63:0] d;
        begin
            d = 64'd0;

            ch = group_i * 4;
            if (ch < 34)
                d[15:0] = offset_value(row_i, col_i, ch);

            ch = group_i * 4 + 1;
            if (ch < 34)
                d[31:16] = offset_value(row_i, col_i, ch);

            ch = group_i * 4 + 2;
            if (ch < 34)
                d[47:32] = offset_value(row_i, col_i, ch);

            ch = group_i * 4 + 3;
            if (ch < 34)
                d[63:48] = offset_value(row_i, col_i, ch);

            make_offset_beat = d;
        end
    endfunction

    // ------------------------------------------------------------------
    // Common tasks
    // ------------------------------------------------------------------

    task apply_reset;
        begin
            @(negedge clk);
            rst_n          = 1'b0;
            cfg_valid      = 1'b0;
            cfg_desc       = 256'd0;
            s_head_valid   = 1'b0;
            s_head_data    = 64'd0;
            s_head_mask    = 4'd0;
            s_head_tag     = 64'd0;
            m_result_ready = 1'b0;

            // Contract requires rst_n low for at least four sampled cycles.
            repeat (4) @(posedge clk);

            @(negedge clk);
            rst_n = 1'b1;
            m_result_ready = 1'b1;

            @(posedge clk);
            #1;
            checks = checks + 1;
            if (fault !== 1'b0) begin
                errors = errors + 1;
                $display("[FAIL] reset did not clear fault @ %0t", $time);
            end
            checks = checks + 1;
            if (cfg_ready !== 1'b1) begin
                errors = errors + 1;
                $display("[FAIL] cfg_ready not high after reset @ %0t", $time);
            end
        end
    endtask

    task send_cfg;
        input [255:0] desc;
        integer wait_count;
        begin
            @(negedge clk);
            cfg_desc  = desc;
            cfg_valid = 1'b1;
            wait_count = 0;

            while ((cfg_ready !== 1'b1) && (wait_count < 20)) begin
                @(negedge clk);
                wait_count = wait_count + 1;
            end

            checks = checks + 1;
            if (cfg_ready !== 1'b1) begin
                errors = errors + 1;
                $display("[FAIL] cfg_ready timeout @ %0t", $time);
            end

            @(posedge clk);  // cfg accept edge
            #1;
            @(negedge clk);
            cfg_valid = 1'b0;
            cfg_desc  = 256'd0;
        end
    endtask

    task drive_heatmap_stream;
        reg [3:0] mask_v;
        reg [63:0] tag_v;
        reg [63:0] data_v;
        integer group_last_v;
        integer frame_end_v;
        begin
            for (r = 0; r < 16; r = r + 1) begin
                for (c = 0; c < 16; c = c + 1) begin
                    for (g = 0; g < 5; g = g + 1) begin
                        group_last_v = (g == 4);
                        frame_end_v  = ((r == 15) && (c == 15) && (g == 4));
                        mask_v       = (g == 4) ? 4'b0001 : 4'b1111;
                        data_v       = make_heat_beat(r, c, g);
                        tag_v        = make_tag(r, c, g, OP_HEATMAP,
                                                group_last_v, frame_end_v);

                        @(negedge clk);
                        if (s_head_ready !== 1'b1) begin
                            errors = errors + 1;
                            $display("[FAIL] heat s_head_ready low at row=%0d col=%0d group=%0d @ %0t",
                                     r, c, g, $time);
                        end

                        s_head_data  = data_v;
                        s_head_mask  = mask_v;
                        s_head_tag   = tag_v;
                        s_head_valid = 1'b1;

                        @(posedge clk);  // accepted beat
                        #1;

                        checks = checks + 1;
                        if (fault === 1'b1) begin
                            errors = errors + 1;
                            $display("[FAIL] unexpected fault during heat scan at row=%0d col=%0d group=%0d @ %0t",
                                     r, c, g, $time);
                        end

                        if (frame_end_v) begin
                            checks = checks + 1;
                            if (done !== 1'b1) begin
                                errors = errors + 1;
                                $display("[FAIL] heatmap done not asserted after final input accept @ %0t", $time);
                            end
                            checks = checks + 1;
                            if (cfg_ready !== 1'b1) begin
                                errors = errors + 1;
                                $display("[FAIL] heatmap did not return to IDLE after final accept @ %0t", $time);
                            end
                        end else begin
                            checks = checks + 1;
                            if (done !== 1'b0) begin
                                errors = errors + 1;
                                $display("[FAIL] unexpected heatmap done before final beat @ %0t", $time);
                            end
                        end
                    end
                end
            end

            @(negedge clk);
            s_head_valid = 1'b0;
            s_head_data  = 64'd0;
            s_head_mask  = 4'd0;
            s_head_tag   = 64'd0;

            $display("[PASS] heatmap scan completed: all -128 / tie / signed score / last-pixel cases exercised");
        end
    endtask

    task drive_offset_stream;
        reg [3:0] mask_v;
        reg [63:0] tag_v;
        reg [63:0] data_v;
        integer group_last_v;
        integer frame_end_v;
        begin
            for (r = 0; r < 16; r = r + 1) begin
                for (c = 0; c < 16; c = c + 1) begin
                    for (g = 0; g < 9; g = g + 1) begin
                        group_last_v = (g == 8);
                        frame_end_v  = ((r == 15) && (c == 15) && (g == 8));
                        mask_v       = (g == 8) ? 4'b0011 : 4'b1111;
                        data_v       = make_offset_beat(r, c, g);
                        tag_v        = make_tag(r, c, g, OP_OFFSET,
                                                group_last_v, frame_end_v);

                        @(negedge clk);
                        if (s_head_ready !== 1'b1) begin
                            errors = errors + 1;
                            $display("[FAIL] offset s_head_ready low at row=%0d col=%0d group=%0d @ %0t",
                                     r, c, g, $time);
                        end

                        s_head_data  = data_v;
                        s_head_mask  = mask_v;
                        s_head_tag   = tag_v;
                        s_head_valid = 1'b1;

                        @(posedge clk);  // accepted beat
                        #1;

                        checks = checks + 1;
                        if (fault === 1'b1) begin
                            errors = errors + 1;
                            $display("[FAIL] unexpected fault during offset scan at row=%0d col=%0d group=%0d @ %0t",
                                     r, c, g, $time);
                        end

                        // Offset mode must not assert done on final input accept.
                        if (frame_end_v) begin
                            checks = checks + 1;
                            if (done !== 1'b0) begin
                                errors = errors + 1;
                                $display("[FAIL] offset done asserted on input completion; must wait for joint16 accept @ %0t",
                                         $time);
                            end
                        end
                    end
                end
            end

            @(negedge clk);
            s_head_valid = 1'b0;
            s_head_data  = 64'd0;
            s_head_mask  = 4'd0;
            s_head_tag   = 64'd0;

            $display("[PASS] offset scan completed: y=c, x=c+17 and Cout34 tail mask exercised");
        end
    endtask

    task check_result_stream_with_stall;
        integer wait_count;
        integer k;
        begin
            m_result_ready = 1'b1;

            for (joint = 0; joint < 17; joint = joint + 1) begin
                wait_count = 0;

                // Observe each result before the accept edge.
                while (!((m_result_valid === 1'b1) &&
                         (m_result_joint == joint)) &&
                       (wait_count < 50)) begin
                    @(negedge clk);
                    wait_count = wait_count + 1;
                end

                checks = checks + 1;
                if (!((m_result_valid === 1'b1) &&
                      (m_result_joint == joint))) begin
                    errors = errors + 1;
                    $display("[FAIL] timeout waiting for result joint%0d @ %0t", joint, $time);
                end

                checks = checks + 1;
                if (m_result_data !== expected_result[joint]) begin
                    errors = errors + 1;
                    $display("[FAIL] joint%0d result mismatch", joint);
                    $display("       expected = %016h", expected_result[joint]);
                    $display("       actual   = %016h", m_result_data);
                end else begin
                    $display("[PASS] joint%0d result = %016h", joint, m_result_data);
                end

                checks = checks + 1;
                if (m_result_last !== ((joint == 16) ? 1'b1 : 1'b0)) begin
                    errors = errors + 1;
                    $display("[FAIL] joint%0d m_result_last mismatch @ %0t", joint, $time);
                end

                // Deliberate downstream stalls on a middle result and the final
                // result.  data/joint/last must remain unchanged throughout.
                if (joint == 5)
                    stall_cycles = 3;
                else if (joint == 16)
                    stall_cycles = 2;
                else
                    stall_cycles = 0;

                if (stall_cycles > 0) begin
                    m_result_ready = 1'b0;
                    hold_data  = m_result_data;
                    hold_joint = m_result_joint;
                    hold_last  = m_result_last;

                    for (k = 0; k < stall_cycles; k = k + 1) begin
                        @(posedge clk);
                        #1;

                        checks = checks + 1;
                        if (m_result_valid !== 1'b1) begin
                            errors = errors + 1;
                            $display("[FAIL] result valid dropped during stall for joint%0d @ %0t",
                                     joint, $time);
                        end

                        checks = checks + 1;
                        if ((m_result_data !== hold_data) ||
                            (m_result_joint !== hold_joint) ||
                            (m_result_last !== hold_last)) begin
                            errors = errors + 1;
                            $display("[FAIL] result payload changed during stall for joint%0d @ %0t",
                                     joint, $time);
                        end

                        @(negedge clk);
                    end

                    m_result_ready = 1'b1;
                    $display("[PASS] joint%0d held stable for %0d stalled cycles",
                             joint, stall_cycles);
                end

                // Accept current joint on the next rising edge.
                @(posedge clk);
                #1;

                if (joint == 16) begin
                    checks = checks + 1;
                    if (done !== 1'b1) begin
                        errors = errors + 1;
                        $display("[FAIL] offset done not asserted after joint16 accept @ %0t", $time);
                    end
                    checks = checks + 1;
                    if (m_result_valid !== 1'b0) begin
                        errors = errors + 1;
                        $display("[FAIL] result_valid remained high after joint16 accept @ %0t", $time);
                    end
                end else begin
                    checks = checks + 1;
                    if (done !== 1'b0) begin
                        errors = errors + 1;
                        $display("[FAIL] done asserted before joint16 accept @ %0t", $time);
                    end
                end
            end

            // done must be exactly one cycle wide.
            @(posedge clk);
            #1;
            checks = checks + 1;
            if (done !== 1'b0) begin
                errors = errors + 1;
                $display("[FAIL] done pulse wider than one cycle @ %0t", $time);
            end

            $display("[PASS] result ordering, payload packing, backpressure hold and offset done checked");
        end
    endtask

    task test_offset_before_heatmap_fault;
        begin
            $display("\n--- TEST: offset-before-heatmap sticky fault ---");
            apply_reset;

            send_cfg(make_cfg(MODE_OFFSET, 9'd34, OP_OFFSET, 4'd15));

            checks = checks + 1;
            if (fault !== 1'b1) begin
                errors = errors + 1;
                $display("[FAIL] offset-before-heatmap did not set fault @ %0t", $time);
            end

            checks = checks + 1;
            if ((cfg_ready !== 1'b0) || (s_head_ready !== 1'b0)) begin
                errors = errors + 1;
                $display("[FAIL] ready outputs not blocked after sticky fault @ %0t", $time);
            end

            repeat (3) begin
                @(posedge clk);
                #1;
                checks = checks + 1;
                if (fault !== 1'b1) begin
                    errors = errors + 1;
                    $display("[FAIL] fault did not remain sticky @ %0t", $time);
                end
            end

            $display("[PASS] sticky fault blocks new work until reset");

            apply_reset;
            $display("[PASS] reset recovered module from sticky fault");
        end
    endtask

    // ------------------------------------------------------------------
    // Test sequence
    // ------------------------------------------------------------------

    initial begin
        errors = 0;
        checks = 0;

        rst_n          = 1'b0;
        cfg_valid      = 1'b0;
        cfg_desc       = 256'd0;
        s_head_data    = 64'd0;
        s_head_valid   = 1'b0;
        s_head_mask    = 4'd0;
        s_head_tag     = 64'd0;
        m_result_ready = 1'b0;

        // Python-reference expected words for the deterministic tensors above.
        expected_result[0]  = 64'h00800064fc180000;
        expected_result[1]  = 64'h002a00ebfb910203;
        expected_result[2]  = 64'h00640444f8380f0f;
        expected_result[3]  = 64'h00fb0171fb0b0405;
        expected_result[4]  = 64'h000e01c4fab8050e;
        expected_result[5]  = 64'h000f02ebf9910a01;
        expected_result[6]  = 64'h00100432f84a0f04;
        expected_result[7]  = 64'h00110179fb030407;
        expected_result[8]  = 64'h001202c0f9bc090a;
        expected_result[9]  = 64'h00130407f8750e0d;
        expected_result[10] = 64'h0014012efb4e0300;
        expected_result[11] = 64'h00150275fa070803;
        expected_result[12] = 64'h001603bcf8c00d06;
        expected_result[13] = 64'h00170103fb790209;
        expected_result[14] = 64'h0018024afa32070c;
        expected_result[15] = 64'h00190391f8eb0c0f;
        expected_result[16] = 64'h001a0452f82a0f0f;

        $dumpfile("tb_argmax_threshold.vcd");
        $dumpvars(0, tb_argmax_threshold);

        $display("============================================================");
        $display(" CNN-v4.0 argmax_threshold self-checking TB");
        $display("============================================================");

        apply_reset;

        $display("\n--- TEST: heatmap 16x16x17 ---");
        send_cfg(make_cfg(MODE_HEATMAP, 9'd17, OP_HEATMAP, 4'd14));
        drive_heatmap_stream;

        // Wait one clock so the one-cycle heatmap done pulse can clear before
        // accepting the next cfg.  best_* state must remain preserved.
        @(posedge clk);
        #1;
        checks = checks + 1;
        if (done !== 1'b0) begin
            errors = errors + 1;
            $display("[FAIL] heatmap done pulse wider than one cycle @ %0t", $time);
        end

        $display("\n--- TEST: offset 16x16x34 ---");
        send_cfg(make_cfg(MODE_OFFSET, 9'd34, OP_OFFSET, 4'd15));
        drive_offset_stream;

        $display("\n--- TEST: result joint0..16 with downstream stalls ---");
        check_result_stream_with_stall;

        test_offset_before_heatmap_fault;

        $display("\n============================================================");
        $display(" checks = %0d", checks);
        $display(" errors = %0d", errors);
        if (errors == 0)
            $display(" TEST PASS: argmax_threshold contract checks passed");
        else
            $display(" TEST FAIL: %0d error(s) detected", errors);
        $display("============================================================\n");

        if (errors != 0)
            $stop;
        else
            $finish;
    end

    // Safety timeout.
    initial begin
        #2000000;
        $display("[FAIL] global simulation timeout");
        $finish;
    end

endmodule
