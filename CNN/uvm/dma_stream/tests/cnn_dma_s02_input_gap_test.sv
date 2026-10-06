class cnn_dma_s02_input_gap_test extends cnn_dma_stream_base_test;

    `uvm_component_utils(cnn_dma_s02_input_gap_test)

    cnn_dma_responder rsp;

    localparam int unsigned EXP_WEIGHT_BEATS = 120;
    localparam int unsigned EXP_IMAGE_BEATS = 69120;
    localparam int unsigned EXP_FEATURE_OUT_BEATS = 49152;

    function new(string name = "cnn_dma_s02_input_gap_test",
                 uvm_component parent = null);

        super.new(name, parent);

    endfunction

    function void build_phase(uvm_phase phase);

        super.build_phase(phase);
        cfg.scenario_id = DMA_SCENARIO_S02;

        // S02
        //
        // INPUT VALID gap 활성화
        cfg.image_gap_enable = 1'b1;

        cfg.weight_gap_enable = 1'b1;

        // Stage0에서는 s_feature 입력 없음
        cfg.feature_gap_enable = 1'b0;

        // 출력 backpressure는 S03 이후
        cfg.output_stall_enable = 1'b0;

        // gap = 1 ~ 3 cycle
        cfg.min_gap_cycles = 1;
        cfg.max_gap_cycles = 3;

        cfg.min_stall_cycles = 0;
        cfg.max_stall_cycles = 0;

        cfg.dma_read_latency = 0;
        cfg.dma_write_latency = 0;

        cfg.dma_complete_latency = 2;

        cfg.inject_dma_error = 0;

    endfunction

    function void end_of_elaboration_phase(uvm_phase phase);

        super.end_of_elaboration_phase(phase);

        if (!$cast(rsp, dma_env.m_axil_rsp)) begin

            `uvm_fatal(get_type_name(), "Failed to cast DMA responder")

        end

    endfunction

    task run_phase(uvm_phase phase);

        cnn_dma_s02_input_gap_seq seq;

        phase.raise_objection(this);

        seq = cnn_dma_s02_input_gap_seq::type_id::create("seq");

        seq.set_cfg(cfg);

        seq.vif = rsp.vif;

        // S02 실행
        seq.start(dma_env.agt.sqr);

        // CHECK 1
        //
        // 실제로 gap을 넣었는지
        if (seq.weight_gap_events == 0) begin

            `uvm_error(get_type_name(), "No Weight gap was inserted")

        end

        if (seq.image_gap_events == 0) begin

            `uvm_error(get_type_name(), "No Image gap was inserted")

        end

        // CHECK 2
        //
        // Weight accept count
        //
        // gap이 있어도 120개 그대로
        if (dma_env.dma_scb.weight_beats != EXP_WEIGHT_BEATS) begin

            `uvm_error(get_type_name(),
                       $sformatf({"Weight beat mismatch ",
                                  "expected=%0d actual=%0d"}, EXP_WEIGHT_BEATS,
                                     dma_env.dma_scb.weight_beats))

        end

        // CHECK 3
        //
        // Image accept count
        if (dma_env.dma_scb.image_beats != EXP_IMAGE_BEATS) begin

            `uvm_error(get_type_name(),
                       $sformatf({"Image beat mismatch ",
                                  "expected=%0d actual=%0d"}, EXP_IMAGE_BEATS,
                                     dma_env.dma_scb.image_beats))

        end

        // CHECK 4
        //
        // DUT output은 S01과 동일해야 함
        if (dma_env.dma_scb.feature_out_beats != EXP_FEATURE_OUT_BEATS) begin

            `uvm_error(get_type_name(),
                       $sformatf({"Feature output mismatch ",
                                  "expected=%0d actual=%0d"},
                                     EXP_FEATURE_OUT_BEATS,
                                     dma_env.dma_scb.feature_out_beats))

        end

        // CHECK 5
        // IMAGE_READ_DONE
        if (!seq.image_read_done_seen) begin

            `uvm_error(get_type_name(), "IMAGE_READ_DONE was not observed")

        end

        // CHECK 6
        // ERROR_PENDING
        if (seq.final_status[2]) begin

            `uvm_error(get_type_name(),
                       $sformatf("ERROR_PENDING asserted STATUS=0x%08h",
                                 seq.final_status))

        end

        // S02 SUMMARY
        `uvm_info(
            get_type_name(),
            $sformatf(
                {"S02 SUMMARY: ", "weight=%0d image=%0d feature_out=%0d ",
                 "weight_gap_events=%0d weight_gap_cycles=%0d ",
                 "image_gap_events=%0d image_gap_cycles=%0d ", "status=0x%08h"},
                    dma_env.dma_scb.weight_beats, dma_env.dma_scb.image_beats,
                    dma_env.dma_scb.feature_out_beats, seq.weight_gap_events,
                    seq.weight_gap_cycles_total, seq.image_gap_events,
                    seq.image_gap_cycles_total, seq.final_status), UVM_LOW)

        phase.drop_objection(this);

    endtask

endclass
