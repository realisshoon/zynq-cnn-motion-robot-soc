// First positive C05 milestone: load the real stage-0 ROM descriptor's
// 960-byte Conv0 weight payload and reach RUN using DMA status semantics.
class cnn_c05_progress_seq extends cnn_ctrl_base_seq;
    `uvm_object_utils(cnn_c05_progress_seq)

    localparam bit [11:0] REG_DEBUG_STATE = 12'h0b4;
    bit run_image;

    function new(string name = "cnn_c05_progress_seq");
        super.new(name);
        run_image = 0;
    endfunction

    task body();
        bit [31:0] debug, status, error_code;
        bit [1:0] resp;
        bit reached_run;
        uvm_hdl_data_t hdl_value;

        write_resp_check(REG_CONTROL, 32'h1, 4'hf, 2'b00, "C05_START");

        // The ROM's Conv0 descriptor specifies 960 DMA bytes. A full 8-byte
        // keep and TLAST on beat 120 satisfy the swap loader's own protocol.
        for (int beat_index = 0; beat_index < 120; beat_index++)
            send_stream(CNN_WEIGHT_BEAT, 64'd0, 8'hff, beat_index == 119);

        reached_run = 0;
        for (int sample_index = 0; sample_index < 500; sample_index++) begin
            read_reg(REG_DEBUG_STATE, debug, resp);
            if (resp != 2'b00 || debug[31:24] != 8'hd1) begin
                `uvm_error("C05_PROGRESS_DEBUG", $sformatf("Bad debug read %08h resp=%02b", debug, resp))
                break;
            end
            if (debug[4:0] == 17 || debug[17]) begin
                `uvm_error("C05_PROGRESS_FAULT", $sformatf("Fault at state=%0d stage=%0d debug=%08h",
                                                         debug[4:0], debug[8:5], debug))
                break;
            end
            if (debug[4:0] == 11 && debug[8:5] == 0) begin
                reached_run = 1;
                break;
            end
        end

        if (!reached_run) begin
            // Diagnostic reads only: expose which independent LOAD_WAIT gate
            // is missing without modifying any DUT result or control signal.
            if (uvm_hdl_read("tb_top.dut.u_top_level_fsm.seen_swap_load_done", hdl_value))
                `uvm_info("C05_GATE", $sformatf("seen_swap_load_done=%0d", hdl_value[0]), UVM_LOW)
            if (uvm_hdl_read("tb_top.dut.u_top_level_fsm.weight_dma_idle", hdl_value))
                `uvm_info("C05_GATE", $sformatf("weight_dma_idle=%0d", hdl_value[0]), UVM_LOW)
            if (uvm_hdl_read("tb_top.dut.u_weight_bram_swap_fsm.byte_count", hdl_value))
                `uvm_info("C05_GATE", $sformatf("swap_byte_count=%0d", hdl_value[19:0]), UVM_LOW)
            if (uvm_hdl_read("tb_top.dut.u_weight_bram_swap_fsm.state", hdl_value))
                `uvm_info("C05_GATE", $sformatf("swap_state=%0d", hdl_value[2:0]), UVM_LOW)
            `uvm_error("C05_PROGRESS_TIMEOUT", $sformatf("Stage-0 RUN not reached; debug=%08h", debug))
        end

        read_reg(REG_STATUS, status, resp);
        if (resp != 0 || !status[1] || status[2] || status[0])
            `uvm_error("C05_PROGRESS_STATUS", $sformatf("Unexpected STATUS=%08h resp=%02b", status, resp))
        read_reg(REG_ERROR_CODE, error_code, resp);
        if (resp != 0 || error_code != 0)
            `uvm_error("C05_PROGRESS_CODE", $sformatf("Unexpected ERROR_CODE=%08h resp=%02b", error_code, resp))

        if (reached_run)
            `uvm_info("C05_PROGRESS_RESULT", "Real Conv0 weight stream and modeled DMA status reached stage-0 RUN", UVM_LOW)

        if (reached_run && (run_image || $test$plusargs("C05_STAGE0_IMAGE"))) begin
            // Downsample's line protocol is 144 rows of 480 full 64-bit
            // beats; TLAST marks each row. The zero payload is real stream
            // stimulus and still traverses the complete stage-0 datapath.
            for (int beat_index = 0; beat_index < 144 * 480; beat_index++)
                send_stream(CNN_IMAGE_BEAT, 64'd0, 8'hff,
                            (beat_index % 480) == 479);

            reached_run = 0;
            for (int sample_index = 0; sample_index < 200000; sample_index++) begin
                read_reg(REG_DEBUG_STATE, debug, resp);
                if (resp != 0 || debug[31:24] != 8'hd1) begin
                    `uvm_error("C05_IMAGE_DEBUG", $sformatf("Bad debug read %08h resp=%02b", debug, resp))
                    break;
                end
                if (debug[4:0] == 17 || debug[17]) begin
                    `uvm_error("C05_IMAGE_FAULT", $sformatf("Fault after image stream: debug=%08h", debug))
                    break;
                end
                if (debug[8:5] == 1) begin
                    reached_run = 1;
                    break;
                end
            end
            if (!reached_run) begin
                if (uvm_hdl_read("tb_top.dut.u_top_level_fsm.seen_down_done", hdl_value))
                    `uvm_info("C05_IMAGE_GATE", $sformatf("down=%0d", hdl_value[0]), UVM_LOW)
                if (uvm_hdl_read("tb_top.dut.u_top_level_fsm.seen_input_done", hdl_value))
                    `uvm_info("C05_IMAGE_GATE", $sformatf("input=%0d", hdl_value[0]), UVM_LOW)
                if (uvm_hdl_read("tb_top.dut.u_top_level_fsm.seen_fm_done", hdl_value))
                    `uvm_info("C05_IMAGE_GATE", $sformatf("fm=%0d", hdl_value[0]), UVM_LOW)
                if (uvm_hdl_read("tb_top.dut.u_top_level_fsm.seen_feature_s2mm_done", hdl_value))
                    `uvm_info("C05_IMAGE_GATE", $sformatf("feature_dma=%0d", hdl_value[0]), UVM_LOW)
                if (uvm_hdl_read("tb_top.dut.u_top_level_fsm.seen_image_dma_done", hdl_value))
                    `uvm_info("C05_IMAGE_GATE", $sformatf("image_dma=%0d", hdl_value[0]), UVM_LOW)
                `uvm_error("C05_IMAGE_TIMEOUT", $sformatf("Stage 1 not reached after image stream; debug=%08h", debug))
            end
            else
                `uvm_info("C05_IMAGE_RESULT", "Stage 0 completed and controller advanced to stage 1", UVM_LOW)
        end
    endtask
endclass
