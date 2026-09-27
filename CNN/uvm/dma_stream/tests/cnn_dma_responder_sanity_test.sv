class cnn_dma_responder_sanity_test extends cnn_dma_stream_base_test;
    `uvm_component_utils(cnn_dma_responder_sanity_test)

    cnn_dma_responder rsp;

    localparam bit [31:0] WEIGHT_BASE = 32'h4041_0000;

    function new(
        string name = "cnn_dma_responder_sanity_test",
        uvm_component parent = null
    );
        super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);
        super.build_phase(phase);

        // 현재 V1에서는 latency=0이면
        // 내부적으로 최소 1 cycle completion으로 동작
        cfg.dma_complete_latency = 0;

    endfunction


    function void end_of_elaboration_phase(uvm_phase phase);
        super.end_of_elaboration_phase(phase);

        // Factory override되어 생성된 responder handle 획득
        if (!$cast(rsp, dma_env.m_axil_rsp)) begin
            `uvm_fatal(
                get_type_name(),
                "Failed to cast m_axil_rsp to cnn_dma_responder"
            )
        end
        if (rsp == null) begin

            `uvm_fatal(
                get_type_name(),
                "cnn_dma_responder handle is null"
            )
        end
    endfunction

    task run_phase(uvm_phase phase);
        bit [31:0] status;

        phase.raise_objection(this);

        // DUT / responder reset 해제 대기
        wait (rsp.vif.rst_n === 1'b1);

        // STEP 1
        // Reset 상태 확인
        status = rsp.dma_reg_read(
            WEIGHT_BASE + 32'h04
        );

        `uvm_info(
            get_type_name(),
            $sformatf(
                "[STEP1] Initial WEIGHT status = 0x%08h",
                status
            ),
            UVM_LOW
        )

        if (status !== 32'h0000_0001) begin
            `uvm_error(
                get_type_name(),
                $sformatf(
                    "Initial status mismatch: expected=0x00000001 actual=0x%08h",
                    status
                )
            )
        end

        // STEP 2
        // DMA Run
        rsp.dma_reg_write(
            WEIGHT_BASE + 32'h00,
            32'h0000_0001
        );

        // STEP 3
        // Source Address 설정
        rsp.dma_reg_write(
            WEIGHT_BASE + 32'h18,
            32'h1000_1000
        );

        // STEP 4
        // Length 설정 -> DMA 시작
        rsp.dma_reg_write(
            WEIGHT_BASE + 32'h28,
            32'd256
        );

        status = rsp.dma_reg_read(
            WEIGHT_BASE + 32'h04
        );

        `uvm_info(
            get_type_name(),
            $sformatf(
                "[STEP2] DMA active status = 0x%08h",
                status
            ),
            UVM_LOW
        )

        // DMA 수행 중:
        // Halted = 0
        // Idle   = 0
        if (
            status[0] !== 1'b0 ||
            status[1] !== 1'b0
        ) begin
            `uvm_error(
                get_type_name(),
                $sformatf(
                    "DMA active status mismatch: status=0x%08h",
                    status
                )
            )
        end

        // STEP 5
        // Completion 대기
        repeat (2)
            @(posedge rsp.vif.clk);

        status = rsp.dma_reg_read(
            WEIGHT_BASE + 32'h04
        );

        `uvm_info(
            get_type_name(),
            $sformatf(
                "[STEP3] DMA complete status = 0x%08h",
                status
            ),
            UVM_LOW
        )

        // 완료 후:
        //
        // bit 1  = Idle = 1
        // bit 12 = IOC  = 1
        //
        // 0x00001002
        if (status !== 32'h0000_1002) begin
            `uvm_error(
                get_type_name(),
                $sformatf(
                    "DMA complete status mismatch: expected=0x00001002 actual=0x%08h",
                    status
                )
            )
        end

        // STEP 6
        // IOC interrupt W1C clear
        rsp.dma_reg_write(
            WEIGHT_BASE + 32'h04,
            32'h0000_1000
        );

        status = rsp.dma_reg_read(
            WEIGHT_BASE + 32'h04
        );

        `uvm_info(
            get_type_name(),
            $sformatf(
                "[STEP4] IOC clear status = 0x%08h",
                status
            ),
            UVM_LOW
        )

        // IOC는 clear되고 Idle은 유지
        //
        // 0x00000002
        if (status !== 32'h0000_0002) begin
            `uvm_error(
                get_type_name(),
                $sformatf(
                    "IOC clear mismatch: expected=0x00000002 actual=0x%08h",
                    status
                )
            )
        end

        phase.drop_objection(this);
    endtask

endclass