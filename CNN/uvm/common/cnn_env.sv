class cnn_env extends uvm_env;
    `uvm_component_utils(cnn_env)

    cnn_agent             agt;
    cnn_m_axil_responder  m_axil_rsp;
    cnn_scoreboard        scb;
    cnn_coverage          cov;
    cnn_virtual_sequencer vseqr;

    function new(string name, uvm_component parent);
        super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);
        super.build_phase(phase);
        agt        = cnn_agent::type_id::create("agt", this);
        m_axil_rsp = cnn_m_axil_responder::type_id::create("m_axil_rsp", this);
        scb        = cnn_scoreboard::type_id::create("scb", this);
        cov        = cnn_coverage::type_id::create("cov", this);
        vseqr      = cnn_virtual_sequencer::type_id::create("vseqr", this);
    endfunction

    function void connect_phase(uvm_phase phase);
        super.connect_phase(phase);
        agt.mon.ap.connect(scb.imp);
        agt.mon.ap.connect(cov.analysis_export);
        vseqr.main_sqr = agt.sqr;
    endfunction
endclass
