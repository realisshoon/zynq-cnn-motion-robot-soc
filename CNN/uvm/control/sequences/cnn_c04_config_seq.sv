class cnn_c04_config_seq extends cnn_ctrl_base_seq;

    `uvm_object_utils(cnn_c04_config_seq)


    function new(string name = "cnn_c04_config_seq");
        super.new(name);
    endfunction


    // ---------------------------------------------------------
    // Wait until STATUS.busy reaches expected value
    //
    // STATUS[1] = busy
    // ---------------------------------------------------------
    task wait_busy(
        bit expected_busy,
        int unsigned max_poll = 20
    );

        bit [31:0] status;
        bit [1:0]  resp;
        bit        found;

        found = 1'b0;


        for (int unsigned i = 0; i < max_poll; i++) begin

            read_reg(
                REG_STATUS,
                status,
                resp
            );


            if (resp != 2'b00) begin

                `uvm_error(
                    "C04_STATUS_RESP",
                    $sformatf(
                        "STATUS read response error: resp=%02b",
                        resp
                    )
                )

                return;

            end


            if (status[1] == expected_busy) begin

                found = 1'b1;

                `uvm_info(
                    "C04_BUSY_PASS",
                    $sformatf(
                        "STATUS busy reached expected value: busy=%0b status=0x%08h",
                        expected_busy,
                        status
                    ),
                    UVM_LOW
                )

                break;

            end

        end


        if (!found) begin

            `uvm_error(
                "C04_BUSY_TIMEOUT",
                $sformatf(
                    "STATUS busy did not reach expected value %0b within %0d polls",
                    expected_busy,
                    max_poll
                )
            )

        end

    endtask


    // ---------------------------------------------------------
    // Check internal snap_threshold through UVM HDL backdoor
    // ---------------------------------------------------------
    task check_snap_threshold(
        bit [7:0] expected
    );

        uvm_hdl_data_t actual;


        if (!uvm_hdl_read(
                "tb_top.dut.u_top_level_fsm.snap_threshold",
                actual
            )) begin

            `uvm_fatal(
                "C04_SNAPSHOT_READ",
                "Failed to read snap_threshold"
            )

        end


        if (actual[7:0] != expected) begin

            `uvm_error(
                "C04_SNAPSHOT_MISMATCH",
                $sformatf(
                    "snap_threshold mismatch: expected=0x%02h actual=0x%02h",
                    expected,
                    actual[7:0]
                )
            )

        end else begin

            `uvm_info(
                "C04_SNAPSHOT_PASS",
                $sformatf(
                    "snap_threshold PASS: expected=0x%02h actual=0x%02h",
                    expected,
                    actual[7:0]
                ),
                UVM_LOW
            )

        end

    endtask

        task check_snap_wgt_base(
        bit [31:0] expected
    );

        uvm_hdl_data_t actual;


        if (!uvm_hdl_read(
                "tb_top.dut.u_top_level_fsm.snap_wgt_base",
                actual
            )) begin

            `uvm_fatal(
                "C04_SNAPSHOT_READ",
                "Failed to read snap_wgt_base"
            )

        end


        if (actual[31:0] != expected) begin

            `uvm_error(
                "C04_SNAPSHOT_MISMATCH",
                $sformatf(
                    "snap_wgt_base mismatch: expected=0x%08h actual=0x%08h",
                    expected,
                    actual[31:0]
                )
            )

        end else begin

            `uvm_info(
                "C04_SNAPSHOT_PASS",
                $sformatf(
                    "snap_wgt_base PASS: expected=0x%08h actual=0x%08h",
                    expected,
                    actual[31:0]
                ),
                UVM_LOW
            )

        end

    endtask

    task body();

        `uvm_info(
            "C04",
            "C04 Configuration Snapshot / Protected Write test start",
            UVM_LOW
        )


        // -----------------------------------------------------
        // C04-1. THRESHOLD configuration snapshot
        //
        // Before START:
        //   THRESHOLD = 0x5A
        //
        // After START:
        //   busy = 1
        //   snap_threshold must capture 0x5A
        // -----------------------------------------------------

        read_check(
            REG_THRESHOLD,
            32'h0000_00D2,
            "THRESHOLD RESET VALUE"
        );


        write_resp_check(
            REG_THRESHOLD,
            32'h0000_005A,
            4'hF,
            2'b00,
            "THRESHOLD CONFIG BEFORE START"
        );


        read_check(
            REG_THRESHOLD,
            32'h0000_005A,
            "THRESHOLD BEFORE START"
        );


        write_resp_check(
            REG_CONTROL,
            32'h0000_0001,
            4'hF,
            2'b00,
            "START FOR SNAPSHOT"
        );


        wait_busy(
            1'b1,
            20
        );


        // Internal snapshot must contain the configuration
        // that existed when the operation started.
        check_snap_threshold(
            8'h5A
        );


        // -----------------------------------------------------
        // C04-2. Protected write while BUSY
        //
        // Try to change THRESHOLD from 0x5A to 0xA5.
        //
        // Expected:
        //   BRESP = SLVERR
        //   THRESHOLD remains 0x5A
        //   snap_threshold remains 0x5A
        // -----------------------------------------------------

        write_resp_check(
            REG_THRESHOLD,
            32'h0000_00A5,
            4'hF,
            2'b10,
            "THRESHOLD WRITE WHILE BUSY"
        );


        read_check(
            REG_THRESHOLD,
            32'h0000_005A,
            "THRESHOLD AFTER PROTECTED WRITE"
        );


        check_snap_threshold(
            8'h5A
        );


        // -----------------------------------------------------
        // Cleanup
        // -----------------------------------------------------

        write_resp_check(
            REG_CONTROL,
            32'h0000_0004,
            4'hF,
            2'b00,
            "SOFT RESET AFTER C04-1"
        );


        wait_busy(
            1'b0,
            20
        );


        read_check(
            REG_STATUS,
            32'h0000_0000,
            "STATUS AFTER C04 SOFT RESET"
        );


        read_check(
            REG_THRESHOLD,
            32'h0000_00D2,
            "THRESHOLD AFTER C04 SOFT RESET"
        );

                // -----------------------------------------------------
        // C04-3. WGT_BASE snapshot / protected write
        // -----------------------------------------------------

        read_check(
            REG_WGT_BASE,
            32'h1000_0000,
            "WGT_BASE RESET VALUE"
        );


        write_resp_check(
            REG_WGT_BASE,
            32'h1000_0040,
            4'hF,
            2'b00,
            "WGT_BASE CONFIG BEFORE START"
        );


        read_check(
            REG_WGT_BASE,
            32'h1000_0040,
            "WGT_BASE BEFORE START"
        );


        write_resp_check(
            REG_CONTROL,
            32'h0000_0001,
            4'hF,
            2'b00,
            "START FOR WGT_BASE SNAPSHOT"
        );


        wait_busy(
            1'b1,
            20
        );


        check_snap_wgt_base(
            32'h1000_0040
        );


        // Busy 중 다른 값으로 변경 시도
        write_resp_check(
            REG_WGT_BASE,
            32'h1000_0080,
            4'hF,
            2'b10,
            "WGT_BASE WRITE WHILE BUSY"
        );


        // 실제 config register가 바뀌지 않았는지 확인
        read_check(
            REG_WGT_BASE,
            32'h1000_0040,
            "WGT_BASE AFTER PROTECTED WRITE"
        );


        // Snapshot 역시 기존 값을 유지해야 함
        check_snap_wgt_base(
            32'h1000_0040
        );


        // Cleanup
        write_resp_check(
            REG_CONTROL,
            32'h0000_0004,
            4'hF,
            2'b00,
            "SOFT RESET AFTER WGT_BASE TEST"
        );


        wait_busy(
            1'b0,
            20
        );


        read_check(
            REG_WGT_BASE,
            32'h1000_0000,
            "WGT_BASE AFTER SOFT RESET"
        );

        `uvm_info(
            "C04",
            "C04 Configuration Snapshot / Protected Write test completed",
            UVM_LOW
        )

    endtask

endclass