class cnn_dma_control_smoke_test extends cnn_dma_stream_base_test;

    `uvm_component_utils(cnn_dma_control_smoke_test)

    cnn_dma_responder rsp;

    function new(
        string name = "cnn_dma_control_smoke_test",
        uvm_component parent = null
    );
        super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);
        super.build_phase(phase);

        // DMA가 너무 오래 걸리지 않도록
        // smoke test에서는 짧게 완료시킨다.
        cfg.dma_complete_latency = 2;
    endfunction

    // end_of_elaboration_phase
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

        cnn_dma_control_smoke_seq seq;

        int unsigned timeout;

        phase.raise_objection(this);

        // Sequence 생성
        seq =
            cnn_dma_control_smoke_seq::type_id::create(
                "seq"
            );

        seq.set_cfg(cfg);

        // DUT에 START 전달
        seq.start(dma_env.agt.sqr);

        // DUT가 실제로 WEIGHT DMA register를
        // programming할 때까지 기다림

        timeout = 0;

        while (
            rsp.weight_length == 0 &&
            timeout < 500
        ) begin
            @(posedge rsp.vif.clk);

            timeout++;
        end
        if (timeout >= 500) begin
            `uvm_fatal(
                get_type_name(),
                "Timeout waiting for DUT WEIGHT DMA programming"
            )
        end

        // CHECK 1
        // WEIGHT DMA Control
        `uvm_info(
            get_type_name(),
            $sformatf(
                "WEIGHT CR = 0x%08h",
                rsp.weight_cr
            ),
            UVM_LOW
        )
        if (rsp.weight_cr[0] !== 1'b1) begin

            `uvm_error(
                get_type_name(),
                $sformatf(
                    "WEIGHT DMA Run bit mismatch: CR=0x%08h",
                    rsp.weight_cr
                )
            )
        end

        // CHECK 2
        // WEIGHT Source Address
        //
        // default WGT_BASE = 0x10000000
        // Conv0 weight offset = 0
        `uvm_info(
            get_type_name(),
            $sformatf(
                "WEIGHT SRC_ADDR = 0x%08h",
                rsp.weight_src_addr
            ),
            UVM_LOW
        )
        if (
            rsp.weight_src_addr !==
            32'h1000_0000
        ) begin
            `uvm_error(
                get_type_name(),
                $sformatf(
                    "WEIGHT source address mismatch: expected=0x10000000 actual=0x%08h",
                    rsp.weight_src_addr
                )
            )
        end

        // CHECK 3
        // WEIGHT Length
        //
        // Conv0 descriptor DMA_BYTES = 960
        // 960 decimal = 0x3C0
        `uvm_info(
            get_type_name(),
            $sformatf(
                "WEIGHT LENGTH = %0d (0x%08h)",
                rsp.weight_length,
                rsp.weight_length
            ),
            UVM_LOW
        )
        if (
            rsp.weight_length !==
            32'd960
        ) begin
            `uvm_error(
                get_type_name(),
                $sformatf(
                    "WEIGHT length mismatch: expected=960 actual=%0d",
                    rsp.weight_length
                )
            )
        end

        // CHECK 4
        // Monitor가 실제 DUT M_AXI-Lite transaction을
        // 관찰했는지 확인
        if (
            dma_env.dma_scb.dma_write_count < 3
        ) begin
            `uvm_error(
                get_type_name(),
                $sformatf(
                    "Expected at least 3 DMA writes, observed=%0d",
                    dma_env.dma_scb.dma_write_count
                )
            )
        end
        if (
            dma_env.dma_scb.dma_read_count < 1
        ) begin
            `uvm_error(
                get_type_name(),
                $sformatf(
                    "Expected at least 1 DMA read, observed=%0d",
                    dma_env.dma_scb.dma_read_count
                )
            )
        end

        `uvm_info(
            get_type_name(),
            $sformatf(
                "DMA integration smoke PASS candidate: writes=%0d reads=%0d",
                dma_env.dma_scb.dma_write_count,
                dma_env.dma_scb.dma_read_count
            ),
            UVM_LOW
        )
        phase.drop_objection(this);

    endtask

endclass