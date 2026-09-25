class cnn_dma_responder extends cnn_m_axil_responder;

    `uvm_component_utils(cnn_dma_responder)

    cnn_dma_stream_cfg cfg;

    function new(string name = "cnn_dma_responder",
                 uvm_component parent = null);
        super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);
        super.build_phase(phase);
        if (!uvm_config_db#(cnn_dma_stream_cfg)::get(
                this, "", "dma_stream_cfg", cfg
            )) begin
            cfg = cnn_dma_stream_cfg::type_id::create("cfg");
            `uvm_warning(get_type_name(), "dma_stream_cfg not found !")
        end
    endfunction

endclass
