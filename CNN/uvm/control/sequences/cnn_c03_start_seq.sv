class cnn_c03_start_seq extends cnn_ctrl_base_seq;

    `uvm_object_utils(cnn_c03_start_seq)


    function new(string name = "cnn_c03_start_seq");
        super.new(name);
    endfunction


    // ---------------------------------------------------------
    // Wait until STATUS.busy reaches expected value
    //
    // STATUS[1] = busy
    // ---------------------------------------------------------
    task wait_busy(bit expected_busy, int unsigned max_poll = 20);

        bit [31:0] status;
        bit [ 1:0] resp;
        bit        found;

        found = 1'b0;


        for (int unsigned i = 0; i < max_poll; i++) begin

            read_reg(REG_STATUS, status, resp);


            if (resp != 2'b00) begin

                `uvm_error("C03_STATUS_RESP",
                           $sformatf("STATUS read response error: resp=%02b",
                                     resp))

                return;

            end


            if (status[1] == expected_busy) begin

                found = 1'b1;

                `uvm_info(
                    "C03_BUSY_PASS",
                    $sformatf(
                        "STATUS busy reached expected value: busy=%0b status=0x%08h",
                        expected_busy, status), UVM_LOW)

                break;

            end

        end


        if (!found) begin

            `uvm_error(
                "C03_BUSY_TIMEOUT",
                $sformatf(
                    "STATUS busy did not reach expected value %0b within %0d polls",
                    expected_busy, max_poll))

        end

    endtask


    task body();

        `uvm_info("C03", "C03 START Acceptance Policy test start", UVM_LOW)


        // -----------------------------------------------------
        // C03-1. START accepted while IDLE
        //
        // Initial:
        //   busy          = 0
        //   error_pending = 0
        //   done_pending  = 0
        //
        // Expected:
        //   START write BRESP = OKAY
        //   busy becomes 1
        // -----------------------------------------------------

        read_check(REG_STATUS, 32'h0000_0000, "STATUS BEFORE START");


        write_resp_check(REG_CONTROL, 32'h0000_0001, 4'hF, 2'b00,
                         "START WHILE IDLE");


        wait_busy(1'b1, 20);


        // -----------------------------------------------------
        // C03-2. START rejected while BUSY
        //
        // Expected:
        //   second START BRESP = SLVERR
        // -----------------------------------------------------

        write_resp_check(REG_CONTROL, 32'h0000_0001, 4'hF, 2'b10,
                         "START WHILE BUSY");


        // CONTROL is a command register.
        // It does not store 0x1.
        read_check(REG_CONTROL, 32'h0000_0000, "CONTROL AFTER START");


        // -----------------------------------------------------
        // Reset after C03-2
        //
        // C03-3 must check DONE_PENDING independently.
        // Therefore busy must return to 0 before forcing
        // done_pending.
        // -----------------------------------------------------

        write_resp_check(REG_CONTROL, 32'h0000_0004, 4'hF, 2'b00,
                         "SOFT RESET AFTER BUSY TEST");


        wait_busy(1'b0, 20);


        read_check(REG_STATUS, 32'h0000_0000, "STATUS AFTER SOFT RESET");


        // -----------------------------------------------------
        // C03-3. START rejected while DONE_PENDING
        //
        // Initial:
        //   busy          = 0
        //   error_pending = 0
        //   done_pending  = 1 (forced)
        //
        // Expected:
        //   STATUS = 0x00000001
        //   START write BRESP = SLVERR
        // -----------------------------------------------------

        if (!uvm_hdl_force(
                "tb_top.dut.u_top_level_fsm.done_pending_r", 1'b1
            )) begin

            `uvm_fatal("C03_FORCE", "Failed to force done_pending_r")

        end


        // STATUS[0] = done_pending
        // busy is already 0 because of the soft reset above.
        read_check(REG_STATUS, 32'h0000_0001, "STATUS WITH DONE_PENDING");


        write_resp_check(REG_CONTROL, 32'h0000_0001, 4'hF, 2'b10,
                         "START WHILE DONE_PENDING");


        // Release forced done_pending state.
        if (!uvm_hdl_release("tb_top.dut.u_top_level_fsm.done_pending_r")) begin

            `uvm_fatal("C03_RELEASE", "Failed to release done_pending_r")

        end


        // Clear done_pending after release.
        write_resp_check(REG_CONTROL, 32'h0000_0002, 4'hF, 2'b00,
                         "CLEAR_DONE AFTER C03-3");


        read_check(REG_STATUS, 32'h0000_0000, "STATUS AFTER CLEAR_DONE");

        // -----------------------------------------------------
        // C03-4. START rejected while ERROR_PENDING
        //
        // Initial:
        //   busy          = 0
        //   done_pending  = 0
        //   error_pending = 1 (forced)
        //
        // Expected:
        //   STATUS = 0x00000004
        //   START write BRESP = SLVERR
        // -----------------------------------------------------

        if (!uvm_hdl_force(
                "tb_top.dut.u_top_level_fsm.error_pending_r", 1'b1
            )) begin

            `uvm_fatal("C03_FORCE", "Failed to force error_pending_r")

        end


        // STATUS[2] = error_pending
        read_check(REG_STATUS, 32'h0000_0004, "STATUS WITH ERROR_PENDING");


        write_resp_check(REG_CONTROL, 32'h0000_0001, 4'hF, 2'b10,
                         "START WHILE ERROR_PENDING");


        if (!uvm_hdl_release(
                "tb_top.dut.u_top_level_fsm.error_pending_r"
            )) begin

            `uvm_fatal("C03_RELEASE", "Failed to release error_pending_r")

        end


        // ERROR_CODE register uses a non-zero write
        // to generate clear_error.
        write_resp_check(REG_ERROR_CODE, 32'h0000_0001, 4'hF, 2'b00,
                         "CLEAR_ERROR AFTER C03-4");


        read_check(REG_STATUS, 32'h0000_0000, "STATUS AFTER CLEAR_ERROR");

        // -----------------------------------------------------
        // C03-5. START rejected when WSTRB[0] = 0
        //
        // START command is located in byte 0 of CONTROL.
        // Therefore WSTRB[0] must be asserted.
        //
        // Expected:
        //   START write BRESP = SLVERR
        //   busy remains 0
        // -----------------------------------------------------

        write_resp_check(REG_CONTROL, 32'h0000_0001, 4'b1110, 2'b10,
                         "START WITH WSTRB0 DISABLED");


        // START must not have been accepted.
        read_check(REG_STATUS, 32'h0000_0000,
                   "STATUS AFTER INVALID START WSTRB");


        // CONTROL is still a command register,
        // so read value must remain 0.
        read_check(REG_CONTROL, 32'h0000_0000,
                   "CONTROL AFTER INVALID START WSTRB");

        `uvm_info("C03", "C03 START Acceptance Policy test completed", UVM_LOW)

    endtask

endclass
