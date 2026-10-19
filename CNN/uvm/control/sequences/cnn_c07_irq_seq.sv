class cnn_c07_irq_seq extends cnn_c05_full_seq;
    `uvm_object_utils(cnn_c07_irq_seq)

    int sampled_enable, sampled_pending_kind;
    bit [5:0] irq_condition_hits;

    covergroup irq_cg;
        option.per_instance = 1;
        // All six enable x pending conditions have distinct IRQ expectations.
        cp_enable: coverpoint sampled_enable {
            bins disabled = {0}; bins enabled = {1};
        }
        cp_pending: coverpoint sampled_pending_kind {
            bins none = {0}; bins done = {1}; bins error = {2};
        }
        enable_x_pending: cross cp_enable, cp_pending;
    endgroup

    function new(string name = "cnn_c07_irq_seq");
        super.new(name);
        irq_cg = new();
    endfunction

    task sample_irq(int enable_value, int pending_kind);
        bit expected_irq;
        expected_irq = (enable_value != 0 && pending_kind != 0);
        sampled_enable = enable_value;
        sampled_pending_kind = pending_kind;
        repeat (2) @(posedge vif.clk);
        if (vif.irq !== expected_irq)
            `uvm_error("C07_IRQ_LEVEL", $sformatf("enable=%0d pending=%0d expected_irq=%0b actual_irq=%0b",
                                                 enable_value, pending_kind, expected_irq, vif.irq))
        irq_condition_hits[enable_value * 3 + pending_kind] = 1;
        irq_cg.sample();
    endtask

    task body();
        bit [31:0] status, count_before, count_after;
        bit [1:0] resp;
        if (vif == null) `uvm_fatal("C07_VIF", "Test did not pass cnn_if")

        read_check(REG_IRQ_ENABLE, 32'd0, "IRQ_ENABLE default");
        sample_irq(0, 0);

        // A short preliminary operation gives frontdoor evidence that the
        // counter increments only while BUSY, then reset clears it.
        write_resp_check(REG_CONTROL, 32'h1, 4'hf, 2'b00, "C07_COUNT_START");
        read_reg(REG_STATUS, status, resp);
        if (resp != 0 || !status[1])
            `uvm_error("C07_COUNT_BUSY", $sformatf("Expected BUSY, status=%08h", status))
        read_reg(REG_CYCLE_COUNT, count_before, resp);
        if (resp != 0) `uvm_error("C07_COUNT_RESP", "Cycle count read failed")
        repeat (100) @(posedge vif.clk);
        read_reg(REG_CYCLE_COUNT, count_after, resp);
        if (resp != 0 || count_after <= count_before)
            `uvm_error("C07_COUNT_GROW", $sformatf("BUSY count did not grow: %0d -> %0d",
                                                  count_before, count_after))
        write_resp_check(REG_CONTROL, 32'h4, 4'hf, 2'b00, "C07_COUNT_RESET");
        read_check(REG_CYCLE_COUNT, 32'd0, "CYCLE_COUNT after reset");

        // The existing full-frame stimulus generates a genuine PUBLISH event.
        super.body();
        // STATUS[3] is the sticky image_read_done indication for this frame.
        read_check(REG_STATUS, 32'h9, "done_pending and image_read_done after publish");
        sample_irq(0, 1);
        read_reg(REG_CYCLE_COUNT, count_before, resp);
        if (resp != 0 || count_before == 0)
            `uvm_error("C07_COUNT_DONE", "Completed frame has no cycle count")
        repeat (50) @(posedge vif.clk);
        read_check(REG_CYCLE_COUNT, count_before, "CYCLE_COUNT holds after completion");

        write_resp_check(REG_IRQ_ENABLE, 32'h1, 4'hf, 2'b00, "IRQ_ENABLE=1");
        read_check(REG_IRQ_ENABLE, 32'h1, "IRQ_ENABLE readback");
        sample_irq(1, 1);
        write_resp_check(REG_CONTROL, 32'h2, 4'hf, 2'b00, "CLEAR_DONE");
        read_check(REG_STATUS, 32'h8, "done_pending cleared, image_read_done retained");
        sample_irq(1, 0);

        // A real module-fault input creates error_pending; the IRQ checker
        // observes both enable settings while the pending bit remains set.
        write_resp_check(REG_CONTROL, 32'h1, 4'hf, 2'b00, "C07_ERROR_START");
        read_reg(REG_STATUS, status, resp);
        if (resp != 0 || !status[1])
            `uvm_error("C07_ERROR_BUSY", "Error scenario did not enter BUSY")
        if (!uvm_hdl_force("tb_top.dut.u_top_level_fsm.input_fault", 1'b1))
            `uvm_fatal("C07_FAULT_FORCE", "Cannot inject module fault input")
        repeat (3) @(posedge vif.clk);
        if (!uvm_hdl_release("tb_top.dut.u_top_level_fsm.input_fault"))
            `uvm_fatal("C07_FAULT_RELEASE", "Cannot release module fault input")
        read_check(REG_STATUS, 32'h4, "error_pending after fault");
        read_check(REG_ERROR_CODE, 32'h1, "module error code");
        sample_irq(1, 2);
        write_resp_check(REG_IRQ_ENABLE, 32'h0, 4'hf, 2'b00, "IRQ_DISABLE");
        sample_irq(0, 2);
        write_resp_check(REG_IRQ_ENABLE, 32'h1, 4'hf, 2'b00, "IRQ_REENABLE");
        sample_irq(1, 2);
        write_resp_check(REG_ERROR_CODE, 32'h1, 4'hf, 2'b00, "CLEAR_ERROR");
        read_check(REG_STATUS, 32'h0, "error_pending cleared");
        sample_irq(1, 0);
        write_resp_check(REG_CONTROL, 32'h4, 4'hf, 2'b00, "C07_CLEANUP_RESET");
        read_check(REG_IRQ_ENABLE, 32'd0, "IRQ_ENABLE reset clear");
        read_check(REG_CYCLE_COUNT, 32'd0, "CYCLE_COUNT reset clear");
        sample_irq(0, 0);

        `uvm_info("C07_COVERAGE", $sformatf("irq_conditions=%06b coverage=%0.1f%%",
                                           irq_condition_hits, irq_cg.get_coverage()), UVM_LOW)
    endtask
endclass
