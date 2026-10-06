class cnn_dma_s03_backpressure_test extends cnn_dma_stream_base_test;

    `uvm_component_utils(cnn_dma_s03_backpressure_test)

    cnn_dma_responder rsp;

    localparam int unsigned EXP_WEIGHT_BEATS = 120;
    localparam int unsigned EXP_IMAGE_BEATS = 69120;
    localparam int unsigned EXP_FEATURE_OUT_BEATS = 49152;

    function new(string name = "cnn_dma_s03_backpressure_test",

                 uvm_component parent = null);

        super.new(name, parent);

    endfunction

    function void build_phase(uvm_phase phase);

        super.build_phase(phase);
        cfg.scenario_id = DMA_SCENARIO_S03;

        // S03
        //
        // Input gap 없음
        // Output READY stall 활성화
        cfg.image_gap_enable = 1'b0;
        cfg.weight_gap_enable = 1'b0;
        cfg.feature_gap_enable = 1'b0;

        cfg.output_stall_enable = 1'b1;

        cfg.min_gap_cycles = 0;
        cfg.max_gap_cycles = 0;

        // S03 directed stall
        cfg.min_stall_cycles = 1;
        cfg.max_stall_cycles = 3;

        cfg.dma_read_latency = 0;
        cfg.dma_write_latency = 0;
        cfg.dma_complete_latency = 2;

        cfg.inject_dma_error = 1'b0;

    endfunction

    function void end_of_elaboration_phase(uvm_phase phase);

        super.end_of_elaboration_phase(phase);

        if (!$cast(rsp, dma_env.m_axil_rsp)) begin

            `uvm_fatal(get_type_name(), "Failed to cast DMA responder")

        end

    endfunction

    task run_phase(uvm_phase phase);

        cnn_dma_s03_backpressure_seq seq;

        phase.raise_objection(this);

        seq = cnn_dma_s03_backpressure_seq::type_id::create("seq");

        seq.set_cfg(cfg);

        seq.vif = rsp.vif;

        seq.start(dma_env.agt.sqr);

        // CHECK 1
        // 1/2/3-cycle stall 모두 발생했는가?
        if (seq.stall_events != 3) begin

            `uvm_error(get_type_name(), $sformatf({"Stall event mismatch ",
                                                   "expected=3 actual=%0d"},
                                                      seq.stall_events))

        end

        if (seq.stall_cycles_total != 6) begin

            `uvm_error(get_type_name(), $sformatf({"Stall cycle mismatch ",
                                                   "expected=6 actual=%0d"},
                                                      seq.stall_cycles_total))

        end

        // CHECK 2
        // Weight 입력 손실 없음
        if (dma_env.dma_scb.weight_beats != EXP_WEIGHT_BEATS) begin

            `uvm_error(get_type_name(), "Weight beat count mismatch")

        end

        // CHECK 3
        // Image 입력 손실 없음
        if (dma_env.dma_scb.image_beats != EXP_IMAGE_BEATS) begin

            `uvm_error(get_type_name(), "Image beat count mismatch")

        end

        // CHECK 4
        //
        // Backpressure가 있어도 최종 output accept 수는
        // S01/S02와 동일해야 한다.
        if (dma_env.dma_scb.feature_out_beats != EXP_FEATURE_OUT_BEATS) begin

            `uvm_error(get_type_name(),
                       $sformatf({"Feature output mismatch ",
                                  "expected=%0d actual=%0d"},
                                     EXP_FEATURE_OUT_BEATS,
                                     dma_env.dma_scb.feature_out_beats))

        end

        // CHECK 5
        // IMAGE_READ_DONE / ERROR
        if (!seq.image_read_done_seen) begin

            `uvm_error(get_type_name(), "IMAGE_READ_DONE was not observed")

        end

        if (seq.final_status[2]) begin

            `uvm_error(get_type_name(),
                       $sformatf("ERROR_PENDING asserted STATUS=0x%08h",
                                 seq.final_status))

        end

        `uvm_info(get_type_name(),
                  $sformatf({"S03 SUMMARY: ", "weight=%0d ", "image=%0d ",
                             "feature_out=%0d ", "stall_events=%0d ",
                             "stall_cycles=%0d ", "status=0x%08h"},

                                dma_env.dma_scb.weight_beats,

                                dma_env.dma_scb.image_beats,

                                dma_env.dma_scb.feature_out_beats,

                                seq.stall_events, seq.stall_cycles_total,

                                seq.final_status), UVM_LOW)

        if (cfg.normal_stalls != 3 || cfg.last_stalls != 1 ||
            cfg.output_last_accepts != 1 || cfg.recoveries != 4)
            `uvm_error("S03_OBSERVED", $sformatf("normal=%0d last=%0d last_accept=%0d recovery=%0d",
                cfg.normal_stalls,cfg.last_stalls,cfg.output_last_accepts,cfg.recoveries))
        `uvm_info("S03 SCOREBOARD SUMMARY", $sformatf(
            "normal_stall=%0d last_stall=%0d stall_events=%0d stall_cycles=%0d feature_out=%0d last_accept=%0d",
            cfg.normal_stalls,cfg.last_stalls,cfg.recoveries,cfg.observed_stall_cycles,
            dma_env.dma_scb.feature_out_beats,cfg.output_last_accepts), UVM_NONE)
        phase.drop_objection(this);

    endtask

endclass
