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

        super.buiid_phase(phase);
        
    endfunction
endclass