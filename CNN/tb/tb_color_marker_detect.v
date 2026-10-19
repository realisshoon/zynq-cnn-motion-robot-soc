`timescale 1ns / 1ps

// Directed self-checking Verilog-2001 testbench for color_marker_detect.
// The downsample producer is modeled by directly driving the public tap port.

module tb_color_marker_detect;

reg clk;
reg rst_n;
wire fault;
reg [23:0] tap_data;
reg [9:0] tap_row;
reg [10:0] tap_col;
reg tap_accept;
reg tap_last;
reg frame_start;
reg [23:0] red_cfg;
reg [23:0] blue_cfg;
reg [17:0] min_count;
wire [31:0] red_word;
wire [31:0] blue_word;
wire results_valid;

color_marker_detect dut (
    .clk(clk),
    .rst_n(rst_n),
    .fault(fault),
    .tap_data(tap_data),
    .tap_row(tap_row),
    .tap_col(tap_col),
    .tap_accept(tap_accept),
    .tap_last(tap_last),
    .frame_start(frame_start),
    .red_cfg(red_cfg),
    .blue_cfg(blue_cfg),
    .min_count(min_count),
    .red_word(red_word),
    .blue_word(blue_word),
    .results_valid(results_valid)
);

initial clk = 1'b0;
always #5 clk = ~clk;

integer errors;
integer cycle_no;
integer launch_events;
integer done_events;
integer result_events;
integer launch_cycle_log [0:31];
integer done_cycle_log [0:31];
integer launch_target_log [0:31];
reg previous_results_valid;

always @(posedge clk) begin
    cycle_no = cycle_no + 1;
end

// Divider timing is measured independently from the DUT scheduler.
always @(negedge clk) begin
    if (rst_n) begin
        if (dut.div_launch_pulse) begin
            launch_cycle_log[launch_events] = cycle_no;
            launch_target_log[launch_events] = dut.div_target;
            launch_events = launch_events + 1;
        end
        if (dut.div_done_pulse) begin
            done_cycle_log[done_events] = cycle_no;
            if ((cycle_no - launch_cycle_log[done_events] + 1) != 30) begin
                $display("FAIL divider %0d latency=%0d expected=30",
                         done_events,
                         cycle_no - launch_cycle_log[done_events] + 1);
                errors = errors + 1;
            end
            done_events = done_events + 1;
        end
        if (results_valid) begin
            result_events = result_events + 1;
            if (previous_results_valid) begin
                $display("FAIL results_valid wider than one cycle at cycle %0d",
                         cycle_no);
                errors = errors + 1;
            end
        end
        previous_results_valid = results_valid;
    end else begin
        previous_results_valid = 1'b0;
    end
end

function [31:0] make_word;
    input found;
    input [10:0] x;
    input [9:0] y;
    begin
        make_word = found ? {1'b1, 10'd0, y, x} : 32'd0;
    end
endfunction

task fail;
    input [8*96-1:0] message;
    begin
        $display("FAIL %0s", message);
        errors = errors + 1;
    end
endtask

task reset_dut;
    begin
        @(negedge clk);
        rst_n = 1'b0;
        frame_start = 1'b0;
        tap_accept = 1'b0;
        tap_last = 1'b0;
        repeat (4) @(negedge clk);
        rst_n = 1'b1;
        repeat (2) @(negedge clk);
        if (fault !== 1'b0) fail("fault did not clear on reset");
        if (results_valid !== 1'b0)
            fail("results_valid did not clear on reset");
    end
endtask

task start_color_frame;
    input [23:0] red_value;
    input [23:0] blue_value;
    input [17:0] min_value;
    begin
        @(negedge clk);
        red_cfg = red_value;
        blue_cfg = blue_value;
        min_count = min_value;
        frame_start = 1'b1;
        @(negedge clk);
        frame_start = 1'b0;
    end
endtask

task send_tap;
    input [7:0] r;
    input [7:0] g;
    input [7:0] b;
    input [9:0] row;
    input [10:0] col;
    input is_last;
    begin
        @(negedge clk);
        tap_data = {b, g, r};
        tap_row = row;
        tap_col = col;
        tap_accept = 1'b1;
        tap_last = is_last;
        @(negedge clk);
        tap_accept = 1'b0;
        tap_last = 1'b0;
        tap_data = 24'd0;
    end
endtask

task wait_for_result;
    integer timeout;
    begin
        timeout = 0;
        while ((results_valid !== 1'b1) && (timeout < 1000)) begin
            @(negedge clk);
            timeout = timeout + 1;
        end
        if (results_valid !== 1'b1) begin
            fail("timeout waiting for results_valid");
        end else begin
            @(negedge clk);
            if (results_valid !== 1'b0)
                fail("results_valid was not exactly one cycle");
        end
    end
endtask

task check_words;
    input [31:0] expected_red;
    input [31:0] expected_blue;
    begin
        if (red_word !== expected_red) begin
            $display("FAIL red_word actual=%08x expected=%08x",
                     red_word, expected_red);
            errors = errors + 1;
        end
        if (blue_word !== expected_blue) begin
            $display("FAIL blue_word actual=%08x expected=%08x",
                     blue_word, expected_blue);
            errors = errors + 1;
        end
    end
endtask

task check_hold;
    input [31:0] expected_red;
    input [31:0] expected_blue;
    begin
        repeat (5) begin
            @(negedge clk);
            if ((red_word !== expected_red) ||
                (blue_word !== expected_blue))
                fail("result word changed before next result");
        end
    end
endtask

integer launch_base;
integer done_base;
integer result_base;
integer first_launch;
integer last_done;
reg [31:0] held_red;
reg [31:0] held_blue;

initial begin
    errors = 0;
    cycle_no = 0;
    launch_events = 0;
    done_events = 0;
    result_events = 0;
    previous_results_valid = 1'b0;

    rst_n = 1'b0;
    tap_data = 24'd0;
    tap_row = 10'd0;
    tap_col = 11'd0;
    tap_accept = 1'b0;
    tap_last = 1'b0;
    frame_start = 1'b0;
    red_cfg = 24'h6464A0;
    blue_cfg = 24'hA06464;
    min_count = 18'd8;

    repeat (2) @(negedge clk);
    reset_dut;

    // ------------------------------------------------------------------
    // Test 1: snapshot, inclusive red/blue boundaries, last-pixel
    // accumulation, floor quotient, shared divider, and 4*30 cycles.
    // red={Bmax120,Gmax60,Rmin100}; blue={Bmin140,Gmax70,Rmax90}.
    // ------------------------------------------------------------------
    $display("TEST1 snapshot/boundary/last/floor/four-divider");
    launch_base = launch_events;
    done_base = done_events;
    result_base = result_events;
    start_color_frame({8'd120,8'd60,8'd100},
                      {8'd140,8'd70,8'd90}, 18'd1);

    // Change live inputs after frame_start. The current frame must retain
    // the snapshotted values above.
    red_cfg = {8'd0,8'd0,8'd255};
    blue_cfg = {8'd255,8'd0,8'd0};
    min_count = 18'd100;

    // Inclusive red boundary: R=Rmin, G=Gmax, B=Bmax.
    send_tap(8'd100, 8'd60, 8'd120, 10'd5, 11'd1, 1'b0);
    // Matching RGB and large coordinates are held while tap_accept=0.  If an
    // idle cycle were counted, the expected centroid below would fail.
    tap_data = {8'd0, 8'd0, 8'd200};
    tap_row = 10'd100;
    tap_col = 11'd1000;
    repeat (3) @(negedge clk);
    tap_data = 24'd0;
    // Inclusive blue boundary: R=Rmax, G=Gmax, B=Bmin.
    send_tap(8'd90, 8'd70, 8'd140, 10'd10, 11'd8, 1'b0);
    // Non-hit must not affect any count or sum.
    send_tap(8'd0, 8'd255, 8'd0, 10'd20, 11'd100, 1'b0);
    // Final pixel is red and must be included: red x=(1+10)/2=floor(5.5)=5.
    send_tap(8'd200, 8'd0, 8'd0, 10'd15, 11'd10, 1'b1);
    wait_for_result;

    check_words(make_word(1'b1, 11'd5, 10'd10),
                make_word(1'b1, 11'd8, 10'd10));
    if ((launch_events - launch_base) != 4)
        fail("four nonzero coordinates did not launch four divisions");
    if ((done_events - done_base) != 4)
        fail("four launched divisions did not complete");
    first_launch = launch_cycle_log[launch_base];
    last_done = done_cycle_log[done_base + 3];
    if ((last_done - first_launch + 1) != 120) begin
        $display("FAIL four-divider latency=%0d expected=120",
                 last_done - first_launch + 1);
        errors = errors + 1;
    end
    if ((result_events - result_base) != 1)
        fail("TEST1 results_valid event count was not one");
    held_red = red_word;
    held_blue = blue_word;
    check_hold(held_red, held_blue);

    // ------------------------------------------------------------------
    // Test 2: simultaneous red+blue hit and result replacement.
    // ------------------------------------------------------------------
    $display("TEST2 simultaneous dual-color hit");
    launch_base = launch_events;
    start_color_frame({8'd200,8'd100,8'd100},
                      {8'd100,8'd100,8'd200}, 18'd1);
    // While this frame is active, old public words must remain held.
    if ((red_word !== held_red) || (blue_word !== held_blue))
        fail("starting a new frame cleared held result words");
    send_tap(8'd150, 8'd100, 8'd150, 10'd55, 11'd77, 1'b1);
    wait_for_result;
    check_words(make_word(1'b1, 11'd77, 10'd55),
                make_word(1'b1, 11'd77, 10'd55));
    if ((launch_events - launch_base) != 4)
        fail("dual-color hit did not launch both color coordinate pairs");

    // ------------------------------------------------------------------
    // Test 3: both color counts are zero; no divider launch is allowed.
    // ------------------------------------------------------------------
    $display("TEST3 count zero skips divider");
    launch_base = launch_events;
    result_base = result_events;
    start_color_frame({8'd100,8'd100,8'd160},
                      {8'd160,8'd100,8'd100}, 18'd1);
    send_tap(8'd0, 8'd255, 8'd0, 10'd0, 11'd0, 1'b1);
    wait_for_result;
    check_words(32'd0, 32'd0);
    if ((launch_events - launch_base) != 0)
        fail("count-zero coordinate launched divider");
    if ((result_events - result_base) != 1)
        fail("count-zero frame did not produce one results_valid pulse");

    // ------------------------------------------------------------------
    // Test 4: MIN_COUNT-1. Nonzero red count still executes two divisions,
    // but found=0 and the published word is zero.
    // ------------------------------------------------------------------
    $display("TEST4 MIN_COUNT-1");
    launch_base = launch_events;
    start_color_frame({8'd100,8'd100,8'd100},
                      {8'd250,8'd10,8'd10}, 18'd3);
    send_tap(8'd120, 8'd20, 8'd20, 10'd5, 11'd2, 1'b0);
    send_tap(8'd130, 8'd30, 8'd30, 10'd5, 11'd4, 1'b1);
    wait_for_result;
    check_words(32'd0, 32'd0);
    if ((launch_events - launch_base) != 2)
        fail("nonzero below-threshold red count did not run x/y divisions");

    // ------------------------------------------------------------------
    // Test 5: count==MIN_COUNT. red x=(1+2+8)/3=floor(3.666)=3.
    // ------------------------------------------------------------------
    $display("TEST5 MIN_COUNT exact");
    launch_base = launch_events;
    start_color_frame({8'd100,8'd100,8'd100},
                      {8'd250,8'd10,8'd10}, 18'd3);
    send_tap(8'd120, 8'd20, 8'd20, 10'd5, 11'd1, 1'b0);
    send_tap(8'd130, 8'd30, 8'd30, 10'd10, 11'd2, 1'b0);
    send_tap(8'd140, 8'd40, 8'd40, 10'd15, 11'd8, 1'b1);
    wait_for_result;
    check_words(make_word(1'b1, 11'd3, 10'd10), 32'd0);
    if ((launch_events - launch_base) != 2)
        fail("MIN_COUNT red frame did not run exactly two divisions");
    check_hold(red_word, blue_word);

    // ------------------------------------------------------------------
    // Test 6: tap_last without tap_accept => sticky fault; reset recovers.
    // ------------------------------------------------------------------
    $display("TEST6 sticky tap protocol fault and reset recovery");
    start_color_frame(24'h6464A0, 24'hA06464, 18'd8);
    @(negedge clk);
    tap_last = 1'b1;
    tap_accept = 1'b0;
    @(negedge clk);
    tap_last = 1'b0;
    if (fault !== 1'b1)
        fail("tap_last without tap_accept did not set fault");
    repeat (3) begin
        @(negedge clk);
        if (fault !== 1'b1) fail("protocol fault was not sticky");
    end
    reset_dut;

    // ------------------------------------------------------------------
    // Test 7: frame_start during divider activity => sticky fault.
    // ------------------------------------------------------------------
    $display("TEST7 active-operation frame_start fault");
    start_color_frame({8'd200,8'd100,8'd100},
                      {8'd100,8'd100,8'd200}, 18'd1);
    send_tap(8'd150, 8'd50, 8'd150, 10'd25, 11'd25, 1'b1);
    while (dut.div_active !== 1'b1) @(negedge clk);
    frame_start = 1'b1;
    @(negedge clk);
    frame_start = 1'b0;
    if (fault !== 1'b1)
        fail("frame_start during divider did not set fault");
    repeat (2) @(negedge clk);
    if (fault !== 1'b1) fail("active-operation fault was not sticky");
    reset_dut;

    // ------------------------------------------------------------------
    // Test 8: explicit public coordinate range violation.
    // ------------------------------------------------------------------
    $display("TEST8 coordinate range fault");
    start_color_frame(24'h6464A0, 24'hA06464, 18'd8);
    send_tap(8'd0, 8'd0, 8'd0, 10'd716, 11'd0, 1'b0);
    if (fault !== 1'b1)
        fail("tap_row > 715 did not set range fault");
    reset_dut;

    if (errors == 0) begin
        $display("COLOR_MARKER_DIRECTED_TEST PASS");
        $display("DIVISION_LATENCY 30 cycles PASS");
        $display("FOUR_DIVISION_LATENCY 120 cycles PASS");
        $display("RESULT_EVENTS %0d", result_events);
    end else begin
        $display("COLOR_MARKER_DIRECTED_TEST FAIL errors=%0d", errors);
    end

    $finish;
end

endmodule
