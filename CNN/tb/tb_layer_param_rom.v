`timescale 1ns / 1ps

module tb_layer_param_rom;

    // ============================================================
    // DUT I/O
    // ============================================================
    reg          clk;
    reg          rst_n;

    wire         fault;

    reg          req_valid;
    wire         req_ready;
    reg  [4:0]   req_op;

    wire         rsp_valid;
    reg          rsp_ready;
    wire [255:0] rsp_desc;


    // ============================================================
    // DUT Instance
    // ============================================================
    layer_param_rom dut (
        .clk       (clk),
        .rst_n     (rst_n),

        .fault     (fault),

        .req_valid (req_valid),
        .req_ready (req_ready),
        .req_op    (req_op),

        .rsp_valid (rsp_valid),
        .rsp_ready (rsp_ready),
        .rsp_desc  (rsp_desc)
    );


    // ============================================================
    // Clock
    // 100 MHz = 10 ns
    // ============================================================
    initial begin
        clk = 1'b0;

        forever #5 clk = ~clk;
    end


    // ============================================================
    // Expected ROM Data
    // ============================================================
    function [255:0] expected_desc;

        input [4:0] op;

        begin
            case (op)

                5'd0:
                    expected_desc =
                    256'h00000000000000000000000000000078001800000000022b0600680404020000;

                5'd1:
                    expected_desc =
                    256'h00000000000000000000000000020040000a0000001e020a8603080402010021;

                5'd2:
                    expected_desc =
                    256'h000000000000000000000000000200f000300000002e02028c03080402010042;

                5'd3:
                    expected_desc =
                    256'h0000000000000000000000000004007800120000006a020b0c06040202010023;

                5'd4:
                    expected_desc =
                    256'h0000000000000000000000000004036000c00000008802029806040201008044;

                5'd5:
                    expected_desc =
                    256'h000000000000000000000000000600d0001c00000160020a980c040201008025;

                5'd6:
                    expected_desc =
                    256'h000000000000000000000000000604e00120000001940202980c040201008046;

                5'd7:
                    expected_desc =
                    256'h000000000000000000000000000800d0001c000002cc020b180c020101008027;

                5'd8:
                    expected_desc =
                    256'h000000000000000000000000000809c00240000003000202b00c020100804048;

                5'd9:
                    expected_desc =
                    256'h000000000000000000000000000a0198003600000570020ab018020100804029;

                5'd10:
                    expected_desc =
                    256'h000000000000000000000000000a12c00480000005d60202b01802010080404a;

                5'd11:
                    expected_desc =
                    256'h000000000000000000000000000c0198003600000a86020b301801008080402b;

                5'd12:
                    expected_desc =
                    256'h000000000000000000000000000c2580090000000aec0202e01801008040204c;

                5'd13:
                    expected_desc =
                    256'h000000000000000000000000000e0330006c0000144c020ae03001008040202d;

                5'd14:
                    expected_desc =
                    256'h000000000000000000000000000e49801200000015180202e03001008040204e;

                5'd15:
                    expected_desc =
                    256'h00000000000000000000000000100330006c00002778020ae03001008040202f;

                5'd16:
                    expected_desc =
                    256'h000000000000000000000000001049801200000028440202e030010080402050;

                5'd17:
                    expected_desc =
                    256'h00000000000000000000000000120330006c00003aa4020ae030010080402031;

                5'd18:
                    expected_desc =
                    256'h00000000000000000000000000124980120000003b700202e030010080402052;

                5'd19:
                    expected_desc =
                    256'h00000000000000000000000000140330006c00004dd0020ae030010080402033;

                5'd20:
                    expected_desc =
                    256'h00000000000000000000000000144980120000004e9c0202e030010080402054;

                5'd21:
                    expected_desc =
                    256'h00000000000000000000000000160330006c000060fc020ae030010080402035;

                5'd22:
                    expected_desc =
                    256'h000000000000000000000000001649801200000061c80202e030010080402056;

                5'd23:
                    expected_desc =
                    256'h00000000000000000000000000180330006c00007428020ae030010080402037;

                5'd24:
                    expected_desc =
                    256'h000000000000000000000000001849801200000074f40202e030010080402058;

                5'd25:
                    expected_desc =
                    256'h000000000000000000000000001a0330006c00008754020ae030010080402039;

                5'd26:
                    expected_desc =
                    256'h000000000000000000000000001a49801200000088200202e03001008040205a;

                5'd27:
                    expected_desc =
                    256'h000000000000000000000000001c03d800f000009a80020284700100804020db;

                5'd28:
                    expected_desc =
                    256'h000000000000000000000000001e06e801b000009b76020288b001008040215c;

                default:
                    expected_desc = 256'd0;

            endcase
        end

    endfunction


    // ============================================================
    // Reset Task
    // synchronous active-low
    // rst_n >= 4 cycles
    // ============================================================
    task reset_dut;

        begin

            @(negedge clk);

            rst_n     = 1'b0;
            req_valid = 1'b0;
            req_op    = 5'd0;
            rsp_ready = 1'b0;

            // Reset 4 cycles
            repeat (4)
                @(posedge clk);

            #1;

            // Reset state check
            if (rsp_valid !== 1'b0) begin
                $display("[ERROR] Reset: rsp_valid must be 0");
                $stop;
            end

            if (fault !== 1'b0) begin
                $display("[ERROR] Reset: fault must be 0");
                $stop;
            end

            if (rsp_desc !== 256'd0) begin
                $display("[ERROR] Reset: rsp_desc must be 0");
                $stop;
            end

            if (req_ready !== 1'b0) begin
                $display("[ERROR] Reset: req_ready must be 0");
                $stop;
            end


            // Reset release
            @(negedge clk);
            rst_n = 1'b1;

            @(posedge clk);
            #1;

            if (req_ready !== 1'b1) begin
                $display("[ERROR] Reset release: req_ready must become 1");
                $stop;
            end

            $display("[PASS] Reset test");

        end

    endtask


    // ============================================================
    // Request Task
    //
    // rsp_ready = 0 상태에서 요청
    // request accept 후 response 확인
    // ============================================================
    task request_op;

        input [4:0] op;

        begin

            // Request를 clock edge 전에 준비
            @(negedge clk);

            req_op    = op;
            req_valid = 1'b1;
            rsp_ready = 1'b0;


            // req_ready가 올라올 때까지 기다림
            while (req_ready !== 1'b1)
                @(negedge clk);


            // 다음 rising edge에서 request accept
            @(posedge clk);

            #1;


            // 정상 Operation일 경우
            if (op < 5'd29) begin

                if (rsp_valid !== 1'b1) begin
                    $display(
                        "[ERROR] op%0d : rsp_valid is not asserted",
                        op
                    );
                    $stop;
                end


                if (rsp_desc !== expected_desc(op)) begin
                    $display(
                        "[ERROR] op%0d : descriptor mismatch",
                        op
                    );

                    $display(
                        "Expected = %064h",
                        expected_desc(op)
                    );

                    $display(
                        "Actual   = %064h",
                        rsp_desc
                    );

                    $stop;
                end


                if (rsp_desc[4:0] !== op) begin
                    $display(
                        "[ERROR] op%0d : descriptor op_id mismatch",
                        op
                    );
                    $stop;
                end


                if (rsp_desc[255:149] !== 107'd0) begin
                    $display(
                        "[ERROR] op%0d : reserved bits are not zero",
                        op
                    );
                    $stop;
                end


                if (fault !== 1'b0) begin
                    $display(
                        "[ERROR] op%0d : fault unexpectedly asserted",
                        op
                    );
                    $stop;
                end

            end


            // Request 제거
            @(negedge clk);

            req_valid = 1'b0;

        end

    endtask


    // ============================================================
    // Response Accept Task
    // ============================================================
    task accept_response;

        begin

            @(negedge clk);

            rsp_ready = 1'b1;


            // rsp_valid && rsp_ready accept
            @(posedge clk);

            #1;


            if (rsp_valid !== 1'b0) begin
                $display(
                    "[ERROR] rsp_valid must clear after response accept"
                );
                $stop;
            end


            @(negedge clk);

            rsp_ready = 1'b0;


            #1;

            if (req_ready !== 1'b1) begin
                $display(
                    "[ERROR] req_ready must return to 1 after response"
                );
                $stop;
            end

        end

    endtask


    // ============================================================
    // Main Test
    // ============================================================
    integer i;

    reg [255:0] stalled_desc;


    initial begin

        // --------------------------------------------------------
        // 초기값
        // --------------------------------------------------------
        rst_n     = 1'b0;
        req_valid = 1'b0;
        req_op    = 5'd0;
        rsp_ready = 1'b0;


        $display("");
        $display("==============================================");
        $display(" layer_param_rom Testbench Start");
        $display("==============================================");
        $display("");


        // ========================================================
        // Scenario 0 : Reset
        // ========================================================
        reset_dut();


        // ========================================================
        // Scenario 1
        // op0 정상 요청 + 1cycle response
        // ========================================================
        $display("");
        $display("[TEST 1] op0 normal request / response");


        request_op(5'd0);


        if (rsp_desc !==
            256'h00000000000000000000000000000078001800000000022b0600680404020000)
        begin

            $display("[ERROR] TEST1 : op0 descriptor mismatch");
            $stop;

        end


        $display("[PASS] op0 descriptor received");


        accept_response();


        $display("[PASS] TEST 1");


        // ========================================================
        // Scenario 2
        // Response Stall
        // ========================================================
        $display("");
        $display("[TEST 2] Response stall / payload hold");


        // op3 request
        request_op(5'd3);


        stalled_desc = rsp_desc;


        // 현재 response를 Top이 받지 않도록 함
        rsp_ready = 1'b0;


        // 새로운 req_op를 일부러 변경
        @(negedge clk);

        req_op    = 5'd10;
        req_valid = 1'b1;


        // stall 3 cycles
        repeat (3) begin

            @(posedge clk);

            #1;


            // response는 계속 valid
            if (rsp_valid !== 1'b1) begin

                $display(
                    "[ERROR] TEST2 : rsp_valid dropped during stall"
                );

                $stop;

            end


            // descriptor 유지
            if (rsp_desc !== stalled_desc) begin

                $display(
                    "[ERROR] TEST2 : rsp_desc changed during stall"
                );

                $display(
                    "Before = %064h",
                    stalled_desc
                );

                $display(
                    "After  = %064h",
                    rsp_desc
                );

                $stop;

            end


            // outstanding response가 있으므로 새 request 금지
            if (req_ready !== 1'b0) begin

                $display(
                    "[ERROR] TEST2 : req_ready must remain 0 during stall"
                );

                $stop;

            end

        end


        @(negedge clk);

        req_valid = 1'b0;


        $display(
            "[PASS] rsp_desc remained stable while rsp_valid && !rsp_ready"
        );


        // 기존 op3 response 처리
        accept_response();


        $display("[PASS] TEST 2");


        // ========================================================
        // Scenario 3
        // op1 ~ op28 전체 조회
        // DW / PW / Head Mapping
        // ========================================================
        $display("");
        $display("[TEST 3] op1 ~ op28 descriptor mapping");


        for (i = 1; i <= 28; i = i + 1) begin

            request_op(i[4:0]);


            if (rsp_desc[4:0] !== i[4:0]) begin

                $display(
                    "[ERROR] TEST3 : request op%0d, response op_id=%0d",
                    i,
                    rsp_desc[4:0]
                );

                $stop;

            end


            $display(
                "[PASS] op%d descriptor correct",
                i
            );


            accept_response();

        end


        $display("[PASS] TEST 3");


        // ========================================================
        // Scenario 4
        // op28 boundary + op29~31 sticky fault
        // ========================================================
        $display("");
        $display("[TEST 4] op28 boundary / op29~31 sticky fault");


        // --------------------------------------------------------
        // op28는 정상
        // --------------------------------------------------------
        request_op(5'd28);


        if (fault !== 1'b0) begin
            $display("[ERROR] TEST4 : op28 must not assert fault");
            $stop;
        end


        $display("[PASS] op28 is valid");


        accept_response();


        // --------------------------------------------------------
        // op29 invalid
        // --------------------------------------------------------
        @(negedge clk);

        req_op    = 5'd29;
        req_valid = 1'b1;
        rsp_ready = 1'b0;


        if (req_ready !== 1'b1) begin
            $display("[ERROR] TEST4 : DUT was not ready for op29 request");
            $stop;
        end


        @(posedge clk);

        #1;


        req_valid = 1'b0;


        if (fault !== 1'b1) begin

            $display(
                "[ERROR] TEST4 : op29 must assert fault"
            );

            $stop;

        end


        if (rsp_valid !== 1'b0) begin

            $display(
                "[ERROR] TEST4 : invalid op must not create response"
            );

            $stop;

        end


        if (req_ready !== 1'b0) begin

            $display(
                "[ERROR] TEST4 : req_ready must be 0 after sticky fault"
            );

            $stop;

        end


        $display("[PASS] op29 generated sticky fault");


        // --------------------------------------------------------
        // 정상 op0을 넣어도 fault가 유지되는지 확인
        // --------------------------------------------------------
        @(negedge clk);

        req_op    = 5'd0;
        req_valid = 1'b1;


        repeat (2)
            @(posedge clk);


        #1;


        if (fault !== 1'b1) begin

            $display(
                "[ERROR] TEST4 : fault must remain sticky"
            );

            $stop;

        end


        if (req_ready !== 1'b0) begin

            $display(
                "[ERROR] TEST4 : new work must remain blocked"
            );

            $stop;

        end


        @(negedge clk);

        req_valid = 1'b0;


        $display("[PASS] fault remains sticky");


        // --------------------------------------------------------
        // Reset 후 fault 복구
        // --------------------------------------------------------
        reset_dut();


        if (fault !== 1'b0) begin

            $display(
                "[ERROR] TEST4 : reset did not clear fault"
            );

            $stop;

        end


        $display("[PASS] reset cleared sticky fault");


        // --------------------------------------------------------
        // op30 test
        // --------------------------------------------------------
        @(negedge clk);

        req_op    = 5'd30;
        req_valid = 1'b1;


        @(posedge clk);

        #1;


        if (fault !== 1'b1) begin

            $display(
                "[ERROR] TEST4 : op30 must assert fault"
            );

            $stop;

        end


        @(negedge clk);

        req_valid = 1'b0;


        $display("[PASS] op30 generated fault");


        // Reset
        reset_dut();


        // --------------------------------------------------------
        // op31 test
        // --------------------------------------------------------
        @(negedge clk);

        req_op    = 5'd31;
        req_valid = 1'b1;


        @(posedge clk);

        #1;


        if (fault !== 1'b1) begin

            $display(
                "[ERROR] TEST4 : op31 must assert fault"
            );

            $stop;

        end


        @(negedge clk);

        req_valid = 1'b0;


        $display("[PASS] op31 generated fault");


        // ========================================================
        // Final
        // ========================================================
        $display("");
        $display("==============================================");
        $display(" PASS : layer_param_rom all tests passed");
        $display("==============================================");
        $display("");


        #20;

        $finish;

    end


endmodule