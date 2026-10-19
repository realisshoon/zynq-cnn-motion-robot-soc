class cnn_c08_fault_seq extends cnn_ctrl_base_seq;
    `uvm_object_utils(cnn_c08_fault_seq)

    virtual cnn_if vif;
    cnn_c08_m_axil_responder rsp;
    cnn_c08_fault_checker checker;
    localparam string FSM_PATH = "tb_top.dut.u_top_level_fsm.";
    localparam bit [31:0] WEIGHT_STATUS = 32'h4041_0004;

    function new(string name = "cnn_c08_fault_seq");
        super.new(name);
    endfunction

    task wait_injected();
        fork : injection_wait
            begin rsp.injected_handshake.wait_trigger(); end
            begin
                repeat (10000) @(posedge vif.clk);
                `uvm_fatal("C08_INJECT_TIMEOUT", "Armed AXI fault was never accepted")
            end
        join_any
        disable injection_wait;
    endtask

    task wait_error(output bit [31:0] status);
        bit [1:0] resp;
        for (int tries = 0; tries < 1000; tries++) begin
            read_reg(REG_STATUS, status, resp);
            if (resp == 0 && status[2] && !status[1]) return;
        end
        `uvm_fatal("C08_ERROR_TIMEOUT", "Injected source did not create error_pending")
    endtask

    task check_fault(string label, int fault_type, int scenario,
                     bit axi_error, bit status_error,
                     bit descriptor_or_swap, bit module_error,
                     bit watchdog_error);
        bit [31:0] status, code;
        bit [1:0] resp;
        uvm_hdl_data_t value;
        wait_error(status);
        read_reg(REG_ERROR_CODE, code, resp);
        if (resp != 0) `uvm_error("C08_CODE_RESP", "ERROR_CODE read failed")
        if (!uvm_hdl_read({FSM_PATH, "fault_lock"}, value))
            `uvm_fatal("C08_LOCK_PATH", "Cannot observe fault_lock")
        checker.check_fault(label, fault_type, scenario,
                            axi_error, status_error, descriptor_or_swap,
                            module_error, watchdog_error,
                            code, status, value[0]);
    endtask

    task check_competing_sources(bit expect_status);
        uvm_hdl_data_t value;
        // The injected read handshake sets txn_done through NBA. A 1 ps
        // observation delay stays before the next fault-sampling clock edge.
        #1ps;
        if (!uvm_hdl_read({FSM_PATH, "module_fault"}, value) || !value[0])
            `uvm_error("C08_COMPETITION", "Module fault was not active at the priority boundary")
        if (!uvm_hdl_read({FSM_PATH, "txn_done"}, value) || !value[0])
            `uvm_error("C08_COMPETITION", "AXI transaction had not completed at the priority boundary")
        if (expect_status) begin
            if (!uvm_hdl_read({FSM_PATH, "txn_status"}, value) || !value[0])
                `uvm_error("C08_COMPETITION", "Status fault was not selected")
            if (!uvm_hdl_read({FSM_PATH, "txn_rdata"}, value) ||
                (value[31:0] & 32'h0000_0770) == 0)
                `uvm_error("C08_COMPETITION", "DMA status error bits were absent")
        end else if (!uvm_hdl_read({FSM_PATH, "txn_error"}, value) || !value[0])
            `uvm_error("C08_COMPETITION", "AXI response error was absent")
    endtask

    task soft_recover();
        uvm_hdl_data_t value;
        write_resp_check(REG_CONTROL, 32'h4, 4'hf, 2'b00, "C08_SOFT_RESET");
        read_check(REG_STATUS, 32'd0, "STATUS after C08 soft reset");
        read_check(REG_ERROR_CODE, 32'd0, "ERROR_CODE after C08 soft reset");
        if (!uvm_hdl_read({FSM_PATH, "fault_lock"}, value) || value[0])
            `uvm_error("C08_RECOVERY_LOCK", "Soft reset did not clear fault_lock")
        checker.record_recovery(1);
    endtask

    task hard_recover();
        uvm_hdl_data_t value;
        // Only the testbench reset input is driven; no expected DUT output is
        // forced. A procedural variable needs HIGH before release (C06).
        if (!uvm_hdl_force("tb_top.rst_n", 1'b0))
            `uvm_fatal("C08_HARD_RESET", "Cannot assert tb_top.rst_n")
        repeat (5) @(posedge vif.clk);
        if (!uvm_hdl_force("tb_top.rst_n", 1'b1))
            `uvm_fatal("C08_HARD_RESET", "Cannot deassert tb_top.rst_n")
        repeat (2) @(posedge vif.clk);
        if (!uvm_hdl_release("tb_top.rst_n"))
            `uvm_fatal("C08_HARD_RESET", "Cannot release tb_top.rst_n")
        repeat (3) @(posedge vif.clk);
        if (!uvm_hdl_read("tb_top.rst_n", value) || !value[0])
            `uvm_fatal("C08_HARD_RESET", "tb_top.rst_n remained LOW")
        read_check(REG_STATUS, 32'd0, "STATUS after C08 hard reset");
        read_check(REG_ERROR_CODE, 32'd0, "ERROR_CODE after C08 hard reset");
        if (!uvm_hdl_read({FSM_PATH, "fault_lock"}, value) || value[0])
            `uvm_error("C08_RECOVERY_LOCK", "Hard reset did not clear fault_lock")
        checker.record_recovery(2);
    endtask

    task start_fault_frame();
        write_resp_check(REG_CONTROL, 32'h1, 4'hf, 2'b00, "C08_START");
    endtask

    task body();
        bit [31:0] status, code;
        bit [1:0] resp;
        uvm_hdl_data_t valid, ready;
        bit descriptor_window;
        if (vif == null || rsp == null || checker == null)
            `uvm_fatal("C08_SETUP", $sformatf("Missing handle: vif=%0b responder=%0b checker=%0b",
                                             vif == null, rsp == null, checker == null))

        // A genuine slave SLVERR on the first weight-DMA status read.
        rsp.arm_response_error(WEIGHT_STATUS);
        start_fault_frame();
        wait_injected();
        check_fault("AXI response error", 0, 0, 1, 0, 0, 0, 0);

        // fault_lock must retain the first code even if a later module input
        // is faulty. CLEAR_ERROR clears pending, not the retained code/lock.
        if (!uvm_hdl_force({FSM_PATH, "input_fault"}, 1'b1))
            `uvm_fatal("C08_MODULE_FORCE", "Cannot inject secondary module fault")
        repeat (3) @(posedge vif.clk);
        if (!uvm_hdl_release({FSM_PATH, "input_fault"}))
            `uvm_fatal("C08_MODULE_RELEASE", "Cannot release secondary module fault")
        read_reg(REG_ERROR_CODE, code, resp);
        checker.check_retained("secondary module fault", code);
        write_resp_check(REG_ERROR_CODE, 32'h1, 4'hf, 2'b00, "CLEAR_ERROR");
        read_check(REG_STATUS, 32'd0, "pending cleared while locked");
        read_reg(REG_ERROR_CODE, code, resp);
        checker.check_retained("CLEAR_ERROR retains first code", code);
        if (!uvm_hdl_read({FSM_PATH, "fault_lock"}, valid) || !valid[0])
            `uvm_error("C08_CLEAR_LOCK", "CLEAR_ERROR incorrectly released fault_lock")
        checker.record_recovery(0);
        soft_recover();

        // Competing AXI response and module inputs on the same fault edge.
        rsp.arm_response_error(WEIGHT_STATUS);
        start_fault_frame();
        wait_injected();
        if (!uvm_hdl_force({FSM_PATH, "input_fault"}, 1'b1))
            `uvm_fatal("C08_MODULE_FORCE", "Cannot inject competing module fault")
        check_competing_sources(0);
        check_fault("AXI over module", 0, 1, 1, 0, 0, 1, 0);
        if (!uvm_hdl_release({FSM_PATH, "input_fault"}))
            `uvm_fatal("C08_MODULE_RELEASE", "Cannot release competing module fault")
        soft_recover();

        // AXI OKAY with real DMA error bits set tests status decoding and
        // status-over-module priority independently of response priority.
        rsp.arm_status_error(WEIGHT_STATUS);
        start_fault_frame();
        wait_injected();
        if (!uvm_hdl_force({FSM_PATH, "input_fault"}, 1'b1))
            `uvm_fatal("C08_MODULE_FORCE", "Cannot inject competing module fault")
        check_competing_sources(1);
        check_fault("DMA status over module", 1, 1, 0, 1, 0, 1, 0);
        if (!uvm_hdl_release({FSM_PATH, "input_fault"}))
            `uvm_fatal("C08_MODULE_RELEASE", "Cannot release competing module fault")
        soft_recover();

        // Corrupt the ROM response descriptor at its real valid/ready window.
        // The RTL compares the descriptor OP ID against requested_op.
        start_fault_frame();
        descriptor_window = 0;
        for (int cycles = 0; cycles < 1000; cycles++) begin
            @(negedge vif.clk);
            if (!uvm_hdl_read({FSM_PATH, "rom_rsp_valid"}, valid) ||
                !uvm_hdl_read({FSM_PATH, "rom_rsp_ready"}, ready))
                `uvm_fatal("C08_ROM_PATH", "Cannot observe ROM response handshake")
            if (valid[0] && ready[0]) begin
                descriptor_window = 1;
                break;
            end
        end
        if (!descriptor_window) `uvm_fatal("C08_ROM_WINDOW", "ROM response never became valid")
        if (!uvm_hdl_force({FSM_PATH, "rom_rsp_desc"}, 256'h1))
            `uvm_fatal("C08_ROM_FORCE", "Cannot corrupt ROM descriptor input")
        @(posedge vif.clk);
        @(negedge vif.clk);
        if (!uvm_hdl_release({FSM_PATH, "rom_rsp_desc"}))
            `uvm_fatal("C08_ROM_RELEASE", "Cannot release ROM descriptor input")
        check_fault("ROM descriptor OP mismatch", 2, 0, 0, 0, 1, 0, 0);
        soft_recover();

        // A malformed early TLAST is accepted by the real swap loader and
        // causes its own fault output, without forcing error_pending.
        start_fault_frame();
        send_stream(CNN_WEIGHT_BEAT, 64'd0, 8'hff, 1'b1);
        check_fault("swap early TLAST", 5, 0, 0, 0, 1, 0, 0);
        soft_recover();

        // Standalone module fault exercises the final non-watchdog source.
        start_fault_frame();
        if (!uvm_hdl_force({FSM_PATH, "input_fault"}, 1'b1))
            `uvm_fatal("C08_MODULE_FORCE", "Cannot inject standalone module fault")
        check_fault("module input", 3, 0, 0, 0, 0, 1, 0);
        if (!uvm_hdl_release({FSM_PATH, "input_fault"}))
            `uvm_fatal("C08_MODULE_RELEASE", "Cannot release standalone module fault")
        soft_recover();

        // Status polling is not a progress event in LOAD_WAIT. A low legal
        // timeout therefore produces the RTL's no-progress watchdog fault.
        write_resp_check(REG_TIMEOUT, 32'd200, 4'hf, 2'b00, "WATCHDOG_LIMIT");
        start_fault_frame();
        check_fault("no-progress watchdog", 4, 0, 0, 0, 0, 0, 1);
        hard_recover();
        start_fault_frame();
        read_reg(REG_STATUS, status, resp);
        if (resp != 0 || !status[1] || status[2])
            `uvm_error("C08_RECOVERY_START", $sformatf("START after hard reset failed: %08h", status))
        soft_recover();
    endtask
endclass
