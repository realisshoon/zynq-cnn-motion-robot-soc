class cnn_dma_s01_no_stall_test extends cnn_dma_stream_base_test;

    `uvm_component_utils(cnn_dma_s01_no_stall_test)

    cnn_dma_responder rsp;

    localparam int unsigned EXP_WEIGHT_BEATS     = 120;
    localparam int unsigned EXP_IMAGE_BEATS      = 69120;
    localparam int unsigned EXP_FEATURE_OUT_BEATS = 49152;

    function new(
        string name = "cnn_dma_s01_no_stall_test",
        uvm_component parent = null
    );
        super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);

        super.build_phase(phase);

        // S01 = baseline
        //
        // intentional gap 없음
        // output backpressure 없음
        // DMA error 없음
        cfg.image_gap_enable    = 0;
        cfg.weight_gap_enable   = 0;
        cfg.feature_gap_enable  = 0;
        cfg.output_stall_enable = 0;

        cfg.min_gap_cycles      = 0;
        cfg.max_gap_cycles      = 0;

        cfg.min_stall_cycles    = 0;
        cfg.max_stall_cycles    = 0;

        cfg.dma_read_latency     = 0;
        cfg.dma_write_latency    = 0;

        // DMA 완료 자체는 빠르게 응답시키되,
        // 실제 Stream drain 완료 여부는 DUT가 별도로 확인
        cfg.dma_complete_latency = 2;

        cfg.inject_dma_error     = 0;

    endfunction

    function void end_of_elaboration_phase(
        uvm_phase phase
    );

        super.end_of_elaboration_phase(phase);

        if (!$cast(rsp, dma_env.m_axil_rsp)) begin

            `uvm_fatal(
                get_type_name(),
                "Failed to cast m_axil_rsp to cnn_dma_responder"
            )

        end

    endfunction

    task run_phase(uvm_phase phase);

        cnn_dma_s01_no_stall_seq seq;

        phase.raise_objection(this);

        seq =
            cnn_dma_s01_no_stall_seq::type_id::create(
                "seq"
            );

        seq.set_cfg(cfg);

        // sequence의 polling delay용 clock handle
        seq.vif = rsp.vif;

        // S01 실행
        seq.start(
            dma_env.agt.sqr
        );

        // CHECK 1
        // IMAGE_READ_DONE
        if (!seq.image_read_done_seen) begin

            `uvm_error(
                get_type_name(),
                "S01 IMAGE_READ_DONE was not observed"
            )

        end

        if (seq.final_status[2]) begin

            `uvm_error(
                get_type_name(),
                $sformatf(
                    "S01 error_pending asserted. status=0x%08h",
                    seq.final_status
                )
            )

        end

        // CHECK 2
        // Accepted Weight Stream
        if (
            dma_env.dma_scb.weight_beats !=
            EXP_WEIGHT_BEATS
        ) begin

            `uvm_error(
                get_type_name(),
                $sformatf(
                    "Weight beat mismatch expected=%0d actual=%0d",
                    EXP_WEIGHT_BEATS,
                    dma_env.dma_scb.weight_beats
                )
            )

        end

        // CHECK 3
        // Accepted Image Stream
        if (
            dma_env.dma_scb.image_beats !=
            EXP_IMAGE_BEATS
        ) begin

            `uvm_error(
                get_type_name(),
                $sformatf(
                    "Image beat mismatch expected=%0d actual=%0d",
                    EXP_IMAGE_BEATS,
                    dma_env.dma_scb.image_beats
                )
            )

        end

        // CHECK 4
        // Conv0 -> feature_map_io -> DMA output
        //
        // 128 x 128 x 24 bytes
        // = 393216 bytes
        // / 8 = 49152 AXIS beats
        if (
            dma_env.dma_scb.feature_out_beats !=
            EXP_FEATURE_OUT_BEATS
        ) begin

            `uvm_error(
                get_type_name(),
                $sformatf(
                    "Feature output beat mismatch expected=%0d actual=%0d",
                    EXP_FEATURE_OUT_BEATS,
                    dma_env.dma_scb.feature_out_beats
                )
            )

        end

        // CHECK 5
        // FEATURE S2MM
        //
        // Conv0 result -> FM_A
        if (
            rsp.feature_dst_addr !==
            32'h1100_0000
        ) begin

            `uvm_error(
                get_type_name(),
                $sformatf(
                    "Feature dst mismatch expected=0x11000000 actual=0x%08h",
                    rsp.feature_dst_addr
                )
            )

        end

        if (
            rsp.feature_dst_length !==
            32'h0006_0000
        ) begin

            `uvm_error(
                get_type_name(),
                $sformatf(
                    "Feature length mismatch expected=0x00060000 actual=0x%08h",
                    rsp.feature_dst_length
                )
            )

        end

        // CHECK 6
        // IMAGE SG programming
        if (
            rsp.image_curdesc !==
            32'h1120_0000
        ) begin

            `uvm_error(
                get_type_name(),
                $sformatf(
                    "IMAGE CURDESC mismatch expected=0x11200000 actual=0x%08h",
                    rsp.image_curdesc
                )
            )

        end

        if (
            rsp.image_taildesc !==
            32'h1120_23C0
        ) begin

            `uvm_error(
                get_type_name(),
                $sformatf(
                    "IMAGE TAILDESC mismatch expected=0x112023C0 actual=0x%08h",
                    rsp.image_taildesc
                )
            )

        end

        // CHECK 7
        // IMAGE_READ_DONE 시점에는 IMAGE DMA가 stop되어
        // Halted 상태여야 함
        if (
            rsp.image_cr[0] !== 1'b0 ||
            rsp.image_sr[0] !== 1'b1
        ) begin

            `uvm_error(
                get_type_name(),
                $sformatf(
                    "IMAGE DMA did not halt correctly: CR=0x%08h SR=0x%08h",
                    rsp.image_cr,
                    rsp.image_sr
                )
            )

        end

        `uvm_info(
            get_type_name(),
            $sformatf(
                {"S01 SUMMARY: ",
                 "weight=%0d image=%0d feature_out=%0d ",
                 "status=0x%08h"},
                dma_env.dma_scb.weight_beats,
                dma_env.dma_scb.image_beats,
                dma_env.dma_scb.feature_out_beats,
                seq.final_status
            ),
            UVM_LOW
        )

        phase.drop_objection(this);

    endtask

endclass