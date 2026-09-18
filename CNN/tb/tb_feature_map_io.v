`timescale 1ns / 1ps

module tb_feature_map_io;
    reg clk = 0;
    always #5 clk = ~clk;
    reg rst_n = 0;
    reg cfg_valid = 0;
    wire cfg_ready;
    reg [255:0] cfg_desc = 0;
    wire done, fault;
    reg [63:0] s_body_data = 0;
    reg s_body_valid = 0;
    wire s_body_ready;
    reg [3:0] s_body_mask = 0;
    reg [63:0] s_body_tag = 0;
    wire [255:0] m_pixel_data;
    wire m_pixel_valid;
    reg m_pixel_ready = 1;
    wire [31:0] m_pixel_mask;
    wire [63:0] m_pixel_tag;
    reg [31:0] src_addr = 32'h11000000;
    reg [31:0] dst_addr = 32'h11100000;
    reg [19:0] src_bytes = 0, dst_bytes = 0;
    reg read_en = 0, write_en = 0;
    reg read_dma_done = 0, write_dma_done = 0, dma_error = 0;
    wire read_done, write_done;
    reg [63:0] s_dma_read_data = 0;
    reg s_dma_read_valid = 0;
    wire s_dma_read_ready;
    reg s_dma_read_last = 0;
    reg [7:0] s_dma_read_keep = 8'hff;
    wire [63:0] m_dma_write_data;
    wire m_dma_write_valid;
    reg m_dma_write_ready = 1;
    wire m_dma_write_last;
    wire [7:0] m_dma_write_keep;

    feature_map_io dut (
        .clk(clk), .rst_n(rst_n), .cfg_valid(cfg_valid), .cfg_ready(cfg_ready),
        .cfg_desc(cfg_desc), .done(done), .fault(fault),
        .s_body_data(s_body_data), .s_body_valid(s_body_valid),
        .s_body_ready(s_body_ready), .s_body_mask(s_body_mask),
        .s_body_tag(s_body_tag), .m_pixel_data(m_pixel_data),
        .m_pixel_valid(m_pixel_valid), .m_pixel_ready(m_pixel_ready),
        .m_pixel_mask(m_pixel_mask), .m_pixel_tag(m_pixel_tag),
        .src_addr(src_addr), .dst_addr(dst_addr), .src_bytes(src_bytes),
        .dst_bytes(dst_bytes), .read_en(read_en), .write_en(write_en),
        .read_dma_done(read_dma_done), .write_dma_done(write_dma_done),
        .dma_error(dma_error), .read_done(read_done),
        .write_done(write_done), .s_dma_read_data(s_dma_read_data),
        .s_dma_read_valid(s_dma_read_valid), .s_dma_read_ready(s_dma_read_ready),
        .s_dma_read_last(s_dma_read_last), .s_dma_read_keep(s_dma_read_keep),
        .m_dma_write_data(m_dma_write_data),
        .m_dma_write_valid(m_dma_write_valid),
        .m_dma_write_ready(m_dma_write_ready),
        .m_dma_write_last(m_dma_write_last),
        .m_dma_write_keep(m_dma_write_keep)
    );

    integer passes = 0, fails = 0, cycles = 0, done_pulses = 0;
    integer pixel_count = 0, write_count = 0;
    integer exp_cin = 0, exp_pixels = 0, exp_width = 0;
    integer cfg_height = 1, exp_write_bytes = 0;
    integer exp_read_base = 0, exp_write_base = 0;
    reg check_pixels = 0, check_writes = 0;
    reg prev_pixel_stall = 0, prev_write_stall = 0;
    reg [255:0] held_pixel_data;
    reg [31:0] held_pixel_mask;
    reg [63:0] held_pixel_tag, held_write_data;
    reg [7:0] held_write_keep;
    reg held_write_last;
    reg completion_armed = 0, completed_once = 0;
    reg completion_rd = 0, completion_wr = 0;
    reg pixel_final_seen = 0, write_final_seen = 0;
    reg read_dma_seen = 0, write_dma_seen = 0;
    reg expect_done;
    integer i, p, b, n, value, tag_row, tag_col;
    reg [255:0] expected_data;
    reg [31:0] expected_mask;
    reg [63:0] expected_tag;
    reg [63:0] expected_write;

    task check;
        input condition;
        input [8*100-1:0] name;
        begin
            if (condition) passes = passes + 1;
            else begin
                fails = fails + 1;
                $display("FAIL cycle=%0d: %0s", cycles, name);
            end
        end
    endtask

    task check_reset_outputs;
        begin
            check(cfg_ready === 1'b0, "reset cfg_ready");
            check(done === 1'b0, "reset done");
            check(fault === 1'b0, "reset fault");
            check(s_body_ready === 1'b0, "reset s_body_ready");
            check(m_pixel_data === 256'd0, "reset m_pixel_data");
            check(m_pixel_valid === 1'b0, "reset m_pixel_valid");
            check(m_pixel_mask === 32'd0, "reset m_pixel_mask");
            check(m_pixel_tag === 64'd0, "reset m_pixel_tag");
            check(read_done === 1'b0, "reset read_done");
            check(write_done === 1'b0, "reset write_done");
            check(s_dma_read_ready === 1'b0, "reset s_dma_read_ready");
            check(m_dma_write_data === 64'd0, "reset m_dma_write_data");
            check(m_dma_write_valid === 1'b0, "reset m_dma_write_valid");
            check(m_dma_write_last === 1'b0, "reset m_dma_write_last");
            check(m_dma_write_keep === 8'd0, "reset m_dma_write_keep");
        end
    endtask

    // Edge N updates the TB prerequisite flags. Registered done must appear
    // after edge N+1, so the expectation uses flags from before this edge.
    always @(posedge clk) begin
        cycles = cycles + 1;
        if (rst_n) begin
            if (prev_pixel_stall && !fault)
                check(m_pixel_valid && m_pixel_data === held_pixel_data &&
                      m_pixel_mask === held_pixel_mask &&
                      m_pixel_tag === held_pixel_tag, "N pixel stall stability");
            if (prev_write_stall && !fault)
                check(m_dma_write_valid && m_dma_write_data === held_write_data &&
                      m_dma_write_keep === held_write_keep &&
                      m_dma_write_last === held_write_last,
                      "N write stall stability");
            prev_pixel_stall = m_pixel_valid && !m_pixel_ready;
            prev_write_stall = m_dma_write_valid && !m_dma_write_ready;
            held_pixel_data = m_pixel_data;
            held_pixel_mask = m_pixel_mask;
            held_pixel_tag = m_pixel_tag;
            held_write_data = m_dma_write_data;
            held_write_keep = m_dma_write_keep;
            held_write_last = m_dma_write_last;

            if (cfg_valid && cfg_ready) begin
                completion_armed = read_en || write_en;
                completion_rd = read_en;
                completion_wr = write_en;
                completed_once = 0;
                pixel_final_seen = 0;
                write_final_seen = 0;
                read_dma_seen = 0;
                write_dma_seen = 0;
                expect_done = 0;
            end else begin
                expect_done = completion_armed && !completed_once &&
                    (!completion_rd || (pixel_final_seen && read_dma_seen)) &&
                    (!completion_wr || (write_final_seen && write_dma_seen));
                if (expect_done) completed_once = 1;
                if (m_pixel_valid && m_pixel_ready && m_pixel_tag[38])
                    pixel_final_seen = 1;
                if (m_dma_write_valid && m_dma_write_ready && m_dma_write_last)
                    write_final_seen = 1;
                if (read_dma_done) read_dma_seen = 1;
                if (write_dma_done) write_dma_seen = 1;
            end

            if (m_pixel_valid && m_pixel_ready) begin
                if (check_pixels) begin
                    p = pixel_count / ((exp_cin + 31) / 32);
                    b = pixel_count % ((exp_cin + 31) / 32);
                    tag_row = p / exp_width;
                    tag_col = p % exp_width;
                    expected_data = 0;
                    expected_mask = 0;
                    for (i = 0; i < 32; i = i + 1)
                        if (b * 32 + i < exp_cin) begin
                            value = (exp_read_base + p * exp_cin + b * 32 + i) & 255;
                            expected_data[i * 8 +: 8] = value;
                            expected_mask[i] = 1'b1;
                        end
                    expected_tag = {24'd0, 1'b0,
                        (p == exp_pixels - 1 &&
                         b == (exp_cin + 31) / 32 - 1), 5'd7,
                        (b == (exp_cin + 31) / 32 - 1),
                        (b == (exp_cin + 31) / 32 - 1),
                        4'd0, 7'd0, b[3:0], tag_row[7:0], tag_col[7:0]};
                    check(m_pixel_data === expected_data, "B/C pixel data and zero tail");
                    check(m_pixel_mask === expected_mask, "B/C pixel mask");
                    if (m_pixel_tag !== expected_tag)
                        $display("TAG p=%0d b=%0d got=%h expected=%h",
                                 p, b, m_pixel_tag, expected_tag);
                    check(m_pixel_tag === expected_tag, "B/C pixel TAG64");
                end
                pixel_count = pixel_count + 1;
            end
            if (m_dma_write_valid && m_dma_write_ready) begin
                if (check_writes) begin
                    expected_write = 0;
                    n = exp_write_bytes - write_count * 8;
                    if (n > 8) n = 8;
                    for (i = 0; i < n; i = i + 1)
                        expected_write[i * 8 +: 8] =
                            (exp_write_base + write_count * 8 + i) & 255;
                    check(m_dma_write_data === expected_write,
                          "E/F write byte order and low8");
                    check(m_dma_write_keep === (8'hff >> (8 - n)),
                          "E/F write keep");
                    check(m_dma_write_last ===
                          ((write_count + 1) * 8 >= exp_write_bytes),
                          "E/F write last");
                end
                write_count = write_count + 1;
            end
            #1;
            if (fault) expect_done = 0;
            if (completion_armed)
                check(done === expect_done, "I/J/K done cycle and pulse width");
            if (done) done_pulses = done_pulses + 1;
        end else begin
            prev_pixel_stall = 0;
            prev_write_stall = 0;
            completion_armed = 0;
            #1;
            check_reset_outputs;
        end
    end

    task reset_dut;
        begin
            @(negedge clk);
            rst_n = 0;
            cfg_valid = 0;
            s_body_valid = 0;
            s_dma_read_valid = 0;
            read_dma_done = 0;
            write_dma_done = 0;
            dma_error = 0;
            m_pixel_ready = 1;
            m_dma_write_ready = 1;
            check_pixels = 0;
            check_writes = 0;
            cfg_height = 1;
            #1;
            check(cfg_ready === 1'b0 && m_dma_write_data === 64'd0,
                  "reset immediate blocker outputs");
            repeat (4) @(negedge clk);
            rst_n = 1;
            #1;
            check(cfg_ready && !done && !fault && !read_done && !write_done,
                  "A synchronous reset and idle");
        end
    endtask

    task configure;
        input rd, wr;
        input integer cin, width, source_bytes, dest_bytes;
        begin
            @(negedge clk);
            check(cfg_ready, "A cfg ready in idle");
            cfg_desc = 0;
            cfg_desc[4:0] = 5'd7;
            cfg_desc[17:9] = cfg_height;
            cfg_desc[26:18] = width;
            cfg_desc[53:45] = cin;
            exp_width = width;
            cfg_valid = 1;
            read_en = rd;
            write_en = wr;
            src_bytes = source_bytes;
            dst_bytes = dest_bytes;
            @(negedge clk);
            cfg_valid = 0;
            cfg_desc = 0;
            src_bytes = 0;
            dst_bytes = 0;
            read_en = 0;
            write_en = 0;
            if (rd || wr) check(!cfg_ready, "A cfg held active");
        end
    endtask

    task send_read;
        input integer total_bytes, base;
        input integer bad_kind;
        integer beat, lane;
        reg [63:0] word_data;
        begin
            for (beat = 0; beat < total_bytes / 8; beat = beat + 1) begin
                word_data = 0;
                for (lane = 0; lane < 8; lane = lane + 1)
                    word_data[lane * 8 +: 8] = (base + beat * 8 + lane) & 255;
                @(negedge clk);
                while (!s_dma_read_ready) begin
                    @(negedge clk);
                end
                s_dma_read_data = word_data;
                s_dma_read_keep = (bad_kind == 1 && beat == 0) ? 8'h7f : 8'hff;
                s_dma_read_last = bad_kind == 2 ? beat == 0 :
                                  bad_kind == 3 ? 1'b0 :
                                  beat == total_bytes / 8 - 1;
                s_dma_read_valid = 1;
                @(negedge clk);
                s_dma_read_valid = 0;
                if (bad_kind != 0) begin
                    s_dma_read_last = 0;
                    s_dma_read_keep = 8'hff;
                    if (bad_kind == 1 || bad_kind == 2) beat = total_bytes;
                end
            end
            s_dma_read_last = 0;
        end
    endtask

    task send_body;
        input integer total_bytes, base;
        integer beat, lane;
        reg [63:0] word_data;
        begin
            for (beat = 0; beat < total_bytes / 4; beat = beat + 1) begin
                word_data = 0;
                for (lane = 0; lane < 4; lane = lane + 1) begin
                    word_data[lane * 16 +: 8] = (base + beat * 4 + lane) & 255;
                    word_data[lane * 16 + 8 +: 8] = 8'hd0 + lane;
                end
                @(negedge clk);
                while (!s_body_ready) begin
                    @(negedge clk);
                end
                s_body_data = word_data;
                s_body_mask = 4'hf;
                s_body_valid = 1;
                @(negedge clk);
                s_body_valid = 0;
            end
        end
    endtask

    task wait_pixels;
        input integer target;
        integer timeout;
        begin
            timeout = 0;
            while (pixel_count < target && timeout < 20000) begin
                @(negedge clk);
                timeout = timeout + 1;
            end
            check(pixel_count == target, "read output count");
        end
    endtask

    task wait_writes;
        input integer target;
        integer timeout;
        begin
            timeout = 0;
            while (write_count < target && timeout < 20000) begin
                @(negedge clk);
                timeout = timeout + 1;
            end
            check(write_count == target, "write output count");
        end
    endtask

    task pulse_read_dma;
        begin
            @(negedge clk); read_dma_done = 1;
            @(negedge clk); read_dma_done = 0;
        end
    endtask
    task pulse_write_dma;
        begin
            @(negedge clk); write_dma_done = 1;
            @(negedge clk); write_dma_done = 0;
        end
    endtask

    initial begin
        #2000000;
        $display("TIMEOUT PASS=%0d FAIL=%0d", passes, fails);
        $stop;
    end

    initial begin
        reset_dut;
        $display("TEST A/B/D/G1/I/O: Cin24 read-only, output first");
        exp_cin = 24; exp_pixels = 2; exp_read_base = 10;
        pixel_count = 0; check_pixels = 1;
        configure(1, 0, 24, 2, 48, 0);
        check(!read_done && !write_done, "A completion cleared on cfg");
        m_pixel_ready = 0;
        send_read(48, 10, 0);
        repeat (40) @(negedge clk);
        check(m_pixel_valid && !read_done && !done, "D/G1 stalled before DMA done");
        m_pixel_ready = 1;
        wait_pixels(2);
        check(!read_done && !done, "G1 final pixel alone insufficient");
        pulse_read_dma;
        repeat (3) @(negedge clk);
        check(read_done && !write_done && !done && done_pulses == 1,
              "G1/I read completion and one done pulse");

        $display("TEST O: new cfg clears prior completion");
        check_pixels = 0;
        configure(0, 1, 0, 1, 0, 8);
        check(!read_done && !write_done && !done, "O completion clear");
        exp_write_bytes = 8; exp_write_base = 30;
        write_count = 0; check_writes = 1;
        m_dma_write_ready = 0;
        send_body(8, 30);
        repeat (20) @(negedge clk);
        check(m_dma_write_valid && !write_done, "F stalled DMA write");
        m_dma_write_ready = 1;
        wait_writes(1);
        check(!write_done && !done, "H1 final write alone insufficient");
        pulse_write_dma;
        repeat (3) @(negedge clk);
        check(write_done && !read_done && done_pulses == 2,
              "H1/J write completion and done");

        $display("TEST C/G2: Cin48 read, DMA done first");
        pixel_count = 0; exp_cin = 48; exp_pixels = 2;
        exp_read_base = 70; check_pixels = 1; check_writes = 0;
        configure(1, 0, 48, 2, 96, 0);
        check(!read_done && !write_done && !done,
              "O prior read completion does not reappear");
        m_pixel_ready = 0;
        send_read(96, 70, 0);
        pulse_read_dma;
        check(!read_done && !done, "G2 DMA done alone insufficient");
        m_pixel_ready = 1;
        wait_pixels(4);
        repeat (3) @(negedge clk);
        check(read_done && done_pulses == 3, "C/G2 final pixel completes");

        $display("TEST H2: write DMA done first");
        write_count = 0; exp_write_bytes = 8; exp_write_base = 90;
        check_writes = 1; check_pixels = 0;
        configure(0, 1, 0, 1, 0, 8);
        check(!read_done && !write_done && !done,
              "O prior write completion does not reappear");
        m_dma_write_ready = 0;
        send_body(8, 90);
        pulse_write_dma;
        check(!write_done && !done, "H2 DMA done alone insufficient");
        m_dma_write_ready = 1;
        wait_writes(1);
        repeat (3) @(negedge clk);
        check(write_done && done_pulses == 4, "H2 write accept completes");

        $display("TEST K: read+write, read path finishes first");
        pixel_count = 0; write_count = 0;
        exp_cin = 24; exp_pixels = 1; exp_read_base = 110;
        exp_write_bytes = 8; exp_write_base = 140;
        check_pixels = 1; check_writes = 1;
        configure(1, 1, 24, 1, 24, 8);
        send_read(24, 110, 0);
        wait_pixels(1);
        pulse_read_dma;
        repeat (2) @(negedge clk);
        check(read_done && !done, "K read path alone not complete");
        send_body(8, 140);
        wait_writes(1);
        pulse_write_dma;
        repeat (3) @(negedge clk);
        check(read_done && write_done && done_pulses == 5,
              "K both paths complete");

        $display("TEST K reverse: write path finishes first");
        pixel_count = 0; write_count = 0;
        exp_read_base = 150; exp_write_base = 180;
        configure(1, 1, 24, 1, 24, 8);
        send_body(8, 180);
        wait_writes(1);
        pulse_write_dma;
        repeat (2) @(negedge clk);
        check(write_done && !done, "K write path alone not complete");
        send_read(24, 150, 0);
        wait_pixels(1);
        pulse_read_dma;
        repeat (3) @(negedge clk);
        check(read_done && write_done && done_pulses == 6,
              "K reverse completion");

        $display("TEST L: fault and last prerequisite together");
        pixel_count = 0; check_pixels = 0; check_writes = 0;
        configure(1, 0, 24, 1, 24, 0);
        send_read(24, 0, 0);
        wait_pixels(1);
        @(negedge clk); read_dma_done = 1; dma_error = 1;
        @(negedge clk); read_dma_done = 0; dma_error = 0;
        check(fault && !done && !cfg_ready && done_pulses == 6,
              "L fault priority and sticky");
        repeat (3) @(negedge clk);
        check(fault && !done, "L sticky fault");

        $display("TEST M: invalid keep, early last, missing last, underflow, DMA error, disabled op");
        reset_dut;
        configure(1, 0, 24, 1, 24, 0);
        send_read(24, 0, 1);
        check(fault, "M invalid keep");
        reset_dut;
        configure(1, 0, 24, 1, 24, 0);
        send_read(24, 0, 2);
        check(fault, "M early last");
        reset_dut;
        configure(1, 0, 24, 1, 24, 0);
        send_read(24, 0, 3);
        check(fault, "M missing last");
        reset_dut;
        configure(1, 0, 24, 1, 16, 0);
        send_read(16, 0, 0);
        repeat (30) @(negedge clk);
        check(fault, "M byte count underflow");
        reset_dut;
        configure(0, 1, 0, 1, 0, 8);
        @(negedge clk); dma_error = 1;
        @(negedge clk); dma_error = 0;
        check(fault, "M DMA error");
        reset_dut;
        configure(0, 0, 0, 1, 0, 0);
        check(fault && !cfg_ready, "M disabled operation");
        reset_dut;
        configure(1, 0, 24, 1, 24, 0);
        send_read(24, 0, 0);
        @(negedge clk); s_dma_read_valid = 1;
        @(negedge clk); s_dma_read_valid = 0;
        check(fault, "M extra read beat after last");

        $display("TEST D: read FIFO full and resumed stream");
        reset_dut;
        pixel_count = 0;
        exp_cin = 24; exp_pixels = 256; exp_read_base = 0;
        check_pixels = 1; check_writes = 0;
        configure(1, 0, 24, 256, 6144, 0);
        m_pixel_ready = 0;
        fork
            send_read(6144, 0, 0);
            begin
                repeat (1200) @(negedge clk);
                check(!s_dma_read_ready && !fault, "D read FIFO full backpressure");
                m_pixel_ready = 1;
            end
        join
        wait_pixels(256);
        pulse_read_dma;
        repeat (3) @(negedge clk);
        check(read_done && !fault && done_pulses == 7,
              "D full read FIFO lossless drain");

        $display("TEST F: write FIFO full and resumed stream");
        write_count = 0;
        exp_write_bytes = 8192; exp_write_base = 0;
        check_pixels = 0; check_writes = 1;
        configure(0, 1, 0, 1, 0, 8192);
        m_dma_write_ready = 0;
        fork
            send_body(8192, 0);
            begin
                repeat (7000) @(negedge clk);
                check(!s_body_ready && !fault, "F write FIFO full backpressure");
                m_dma_write_ready = 1;
            end
        join
        wait_writes(1024);
        pulse_write_dma;
        repeat (3) @(negedge clk);
        check(write_done && !fault && done_pulses == 8,
              "F full write FIFO lossless drain");

        $display("TEST E: sparse body mask and partial final beat");
        write_count = 0;
        exp_write_bytes = 5; exp_write_base = 200;
        configure(0, 1, 0, 1, 0, 5);
        @(negedge clk);
        s_body_data = 64'd0;
        s_body_data[7:0] = 8'd200;
        s_body_data[39:32] = 8'd201;
        s_body_mask = 4'b0101;
        s_body_valid = 1;
        @(negedge clk); s_body_valid = 0;
        while (!s_body_ready) @(negedge clk);
        s_body_data = 64'd0;
        s_body_data[7:0] = 8'd202;
        s_body_data[23:16] = 8'd203;
        s_body_data[39:32] = 8'd204;
        s_body_mask = 4'b0111;
        s_body_valid = 1;
        @(negedge clk); s_body_valid = 0;
        wait_writes(1);
        pulse_write_dma;
        repeat (3) @(negedge clk);
        check(write_done && !fault && done_pulses == 9,
              "E sparse mask and final keep");

        $display("TEST K/D/F: simultaneous paths with write output stalled");
        pixel_count = 0; write_count = 0;
        exp_cin = 24; exp_pixels = 1; exp_read_base = 210;
        exp_write_bytes = 8; exp_write_base = 220;
        check_pixels = 1; check_writes = 1;
        configure(1, 1, 24, 1, 24, 8);
        m_dma_write_ready = 0;
        send_body(8, 220);
        send_read(24, 210, 0);
        wait_pixels(1);
        pulse_read_dma;
        repeat (3) @(negedge clk);
        check(read_done && !write_done && !done && m_dma_write_valid,
              "K independent read completion during write stall");
        m_dma_write_ready = 1;
        wait_writes(1);
        pulse_write_dma;
        repeat (3) @(negedge clk);
        check(write_done && !fault && done_pulses == 10,
              "K both paths after write stall release");

        $display("TEST P: extra body input after configured write byte count");
        reset_dut;
        configure(0, 1, 0, 1, 0, 4);
        send_body(4, 60);
        @(negedge clk);
        check(!s_body_ready && !fault, "P write byte limit closes ready");
        s_body_mask = 4'hf;
        s_body_valid = 1;
        @(negedge clk); s_body_valid = 0;
        check(fault && !cfg_ready, "P extra body sticky fault");

        $display("TEST Q: two-row TAG and column rollover");
        reset_dut;
        cfg_height = 2;
        pixel_count = 0;
        exp_cin = 24; exp_pixels = 6; exp_read_base = 40;
        check_pixels = 1; check_writes = 0;
        configure(1, 0, 24, 3, 144, 0);
        cfg_height = 1;
        send_read(144, 40, 0);
        wait_pixels(6);
        pulse_read_dma;
        repeat (3) @(negedge clk);
        check(read_done && !fault && done_pulses == 11,
              "Q multi-row TAG and frame_end");

        $display("TEST R: partial write reset and clean restart");
        reset_dut;
        configure(0, 1, 0, 1, 0, 8);
        send_body(4, 20);
        repeat (10) @(negedge clk);
        check(!m_dma_write_valid && !fault,
              "R partial write held before reset");
        reset_dut;
        write_count = 0;
        exp_write_bytes = 8; exp_write_base = 230;
        check_pixels = 0; check_writes = 1;
        configure(0, 1, 0, 1, 0, 8);
        send_body(8, 230);
        wait_writes(1);
        pulse_write_dma;
        repeat (3) @(negedge clk);
        check(write_done && !fault && done_pulses == 12,
              "R restarted write contains no old bytes");

        $display("REGRESSION PASS=%0d FAIL=%0d DONE_PULSES=%0d", passes, fails, done_pulses);
        if (fails != 0) $stop;
        $finish;
    end
endmodule
