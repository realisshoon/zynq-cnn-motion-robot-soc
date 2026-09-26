class cnn_dma_stream_env extends cnn_env;
    `uvm_component_utils(cnn_dma_stream_env)

    cnn_dma_master_monitor dma_mon;

    cnn_dma_stream_scoreboard dma_scb;

    function new(string name = "cnn_dma_stream_env", uvm_component parent = null);
        super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);
        // common responder -> DMA 전용 responder로 override
        cnn_m_axil_responder::type_id::set_type_override(cnn_dma_responder::get_type());

        // common scoreboard -> DMA/Stream 전용 scoreboard로 override
        cnn_scoreboard::type_id::set_type_override(cnn_dma_stream_scoreboard::get_type());

        // override를 먼저 해야 상속 가능
        super.buiid_phase(phase);
        
        // DMA 전용 monitor 생성
        dma_mon = cnn_dma_master_monitor::type_id::create("dma_mon", this);

        if (!$cast(dma_scb, scb)) begin
            `uvm_fatal(get_type_name(), "Failed to cast common scoreboard to cnn_dma_stream_scoreboard")
        end
    endfunction

    function void connect_phase(uvm_phase phase);
        super.connect_phase(phase);

        // DMA monitor -> DMA scoreboard 연결
        dma_mon.ap.connect(dma_scb.dma_imp);
    endfunction

endclass