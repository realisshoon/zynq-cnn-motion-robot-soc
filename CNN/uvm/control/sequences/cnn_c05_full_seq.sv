// Drives each ROM-described weight transfer and each feature MM2S transfer.
// State reads only pace stimulus. The separate C05 monitor/checker will judge
// the full state, stage, and requested-op trace.
class cnn_c05_full_seq extends cnn_c05_progress_seq;
    `uvm_object_utils(cnn_c05_full_seq)

    virtual cnn_if vif;
    localparam bit [11:0] REG_DEBUG_STATE = 12'h0b4;

    function new(string name = "cnn_c05_full_seq");
        super.new(name);
    endfunction

    task wait_weight_ready(int stage_index, int part_index);
        for (int cycles = 0; cycles < 2000000; cycles++) begin
            @(posedge vif.clk);
            if (vif.s_weight_ready === 1'b1) return;
        end
        `uvm_fatal("C05_WEIGHT_WAIT", $sformatf("No weight request at stage=%0d part=%0d", stage_index, part_index))
    endtask

    task send_current_weight(int stage_index, int part_index);
        uvm_hdl_data_t hdl_value;
        int unsigned bytes;
        wait_weight_ready(stage_index, part_index);
        if (!uvm_hdl_read("tb_top.dut.u_weight_bram_swap_fsm.expected_dma_bytes", hdl_value))
            `uvm_fatal("C05_WEIGHT_SIZE", "Cannot read the captured ROM descriptor DMA length")
        bytes = int'(hdl_value[19:0]);
        if (bytes == 0 || bytes[2:0] != 0)
            `uvm_fatal("C05_WEIGHT_SIZE", $sformatf("Invalid stage=%0d part=%0d DMA bytes=%0d",
                                                    stage_index, part_index, bytes))
        `uvm_info("C05_WEIGHT", $sformatf("stage=%0d part=%0d bytes=%0d", stage_index, part_index, bytes), UVM_LOW)
        for (int beat_index = 0; beat_index < bytes / 8; beat_index++)
            send_stream(CNN_WEIGHT_BEAT, 64'd0, 8'hff, beat_index == bytes / 8 - 1);

        // Wait for the swap FSM to leave LOAD before the next request. Without
        // this edge, the just-finished transfer could be mistaken for a new one.
        for (int cycles = 0; cycles < 1000; cycles++) begin
            @(posedge vif.clk);
            if (vif.s_weight_ready === 1'b0) return;
        end
        `uvm_fatal("C05_WEIGHT_END", "Swap loader did not retire its transfer")
    endtask

    task wait_stage_run(int stage_index);
        bit [31:0] debug;
        bit [1:0] resp;
        for (int sample_index = 0; sample_index < 100000; sample_index++) begin
            read_reg(REG_DEBUG_STATE, debug, resp);
            if (resp != 0 || debug[31:24] != 8'hd1)
                `uvm_fatal("C05_RUN_DEBUG", $sformatf("Bad debug register %08h resp=%02b", debug, resp))
            if (debug[4:0] == 17 || debug[17])
                `uvm_fatal("C05_RUN_FAULT", $sformatf("Fault before stage %0d RUN: %08h", stage_index, debug))
            if (debug[8:5] == stage_index && debug[4:0] == 11) return;
        end
        `uvm_fatal("C05_RUN_WAIT", $sformatf("Stage %0d did not reach RUN", stage_index))
    endtask

    task send_current_feature(int stage_index);
        uvm_hdl_data_t hdl_value;
        int unsigned bytes;
        if (!uvm_hdl_read("tb_top.dut.u_top_level_fsm.fm_src_bytes", hdl_value))
            `uvm_fatal("C05_FEATURE_SIZE", "Cannot read the configured feature source length")
        bytes = int'(hdl_value[19:0]);
        if (bytes == 0 || bytes[2:0] != 0)
            `uvm_fatal("C05_FEATURE_SIZE", $sformatf("Invalid stage=%0d source bytes=%0d", stage_index, bytes))
        `uvm_info("C05_FEATURE", $sformatf("stage=%0d bytes=%0d", stage_index, bytes), UVM_LOW)
        for (int beat_index = 0; beat_index < bytes / 8; beat_index++)
            send_stream(CNN_FEATURE_IN_BEAT, 64'd0, 8'hff, beat_index == bytes / 8 - 1);
    endtask

    task wait_next_stage_or_done(int stage_index);
        bit [31:0] debug, status;
        bit [1:0] resp;
        for (int sample_index = 0; sample_index < 200000; sample_index++) begin
            read_reg(REG_DEBUG_STATE, debug, resp);
            if (resp != 0 || debug[31:24] != 8'hd1)
                `uvm_fatal("C05_STAGE_DEBUG", $sformatf("Bad debug register %08h resp=%02b", debug, resp))
            if (debug[4:0] == 17 || debug[17])
                `uvm_fatal("C05_STAGE_FAULT", $sformatf("Fault after stage %0d: %08h", stage_index, debug))
            if (stage_index < 15 && debug[8:5] == stage_index + 1) return;
            if (stage_index == 15 && debug[4:0] == 0 && !debug[16]) begin
                read_reg(REG_STATUS, status, resp);
                if (resp == 0 && status[0] && !status[1] && !status[2]) return;
            end
        end
        `uvm_fatal("C05_STAGE_WAIT", $sformatf("No completion after stage %0d; debug=%08h", stage_index, debug))
    endtask

    task body();
        int max_stage;
        if (vif == null) `uvm_fatal("C05_VIF", "Test did not pass cnn_if to C05 full sequence")
        run_image = 1;
        super.body();
        if (!$value$plusargs("C05_MAX_STAGE=%d", max_stage)) max_stage = 15;
        if (max_stage < 1 || max_stage > 15)
            `uvm_fatal("C05_MAX_STAGE", $sformatf("Invalid C05_MAX_STAGE=%0d", max_stage))

        for (int stage_index = 1; stage_index <= max_stage; stage_index++) begin
            int part_count;
            part_count = (stage_index <= 13) ? 2 : 1;
            for (int part_index = 0; part_index < part_count; part_index++)
                send_current_weight(stage_index, part_index);
            wait_stage_run(stage_index);
            send_current_feature(stage_index);
            wait_next_stage_or_done(stage_index);
            `uvm_info("C05_STAGE_DONE", $sformatf("stage=%0d completed", stage_index), UVM_LOW)
        end
    endtask
endclass
