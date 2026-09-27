cnn_golden_feature_memory feature_mem;

class cnn_golden_env extends cnn_env;
    `uvm_component_utils(cnn_golden_env)

    function new(string name, uvm_component parent);
        super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);

        cnn_scoreboard::type_id::set_type_override(cnn_golden_scoreboard::get_type());

        cnn_m_axil_responder::type_id::set_type_override(cnn_golden_dma_responder::get_type());

        super.build_phase(phase);


        feature_mem = cnn_golden_feature_memory::type_id::create("feature_mem", this);

    endfunction
    function void connect_phase(uvm_phase phase);

        super.connect_phase(phase);

        agt.mon.ap.connect(feature_mem.analysis_export);

    endfunction

endclass
