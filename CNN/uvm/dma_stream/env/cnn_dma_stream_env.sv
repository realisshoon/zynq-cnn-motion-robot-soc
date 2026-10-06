class cnn_dma_stream_env extends cnn_env;
    `uvm_component_utils(cnn_dma_stream_env)

    cnn_dma_master_monitor dma_mon;
    cnn_dma_stream_observer observer;
    cnn_dma_stream_feeder feeder;

    cnn_dma_stream_scoreboard dma_scb;

    function new(string name = "cnn_dma_stream_env",
                 uvm_component parent = null);
        super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);
        // common responder -> DMA 전용 responder로 override
        cnn_m_axil_responder::type_id::set_type_override(
            cnn_dma_responder::get_type());

        // common scoreboard -> DMA/Stream 전용 scoreboard로 override
        cnn_scoreboard::type_id::set_type_override(
            cnn_dma_stream_scoreboard::get_type());

        // common coverage -> DMA/Stream coverage
        cnn_coverage::type_id::set_type_override(
            cnn_dma_stream_coverage::get_type());

        // override를 먼저 해야 상속 가능
        super.build_phase(phase);

        feeder = cnn_dma_stream_feeder::type_id::create("feeder", this);
        observer = cnn_dma_stream_observer::type_id::create("observer", this);

        // DMA 전용 monitor 생성
        dma_mon = cnn_dma_master_monitor::type_id::create("dma_mon", this);

        if (!$cast(dma_scb, scb)) begin
            `uvm_fatal(
                get_type_name(),
                "Failed to cast common scoreboard to cnn_dma_stream_scoreboard")
        end
    endfunction

    function void connect_phase(uvm_phase phase);
        cnn_dma_stream_coverage coverage_handle;
        super.connect_phase(phase);
        if (!$cast(coverage_handle,cov)) `uvm_fatal("DMA_ENV", "Coverage cast failed")
        dma_mon.ap.connect(coverage_handle.dma_imp);
        dma_mon.ap.connect(feeder.imp);

        // DMA monitor -> DMA scoreboard 연결
        dma_mon.ap.connect(dma_scb.dma_imp);
        agt.mon.ap.connect(dma_scb.checked_stream_imp);
    endfunction

endclass
