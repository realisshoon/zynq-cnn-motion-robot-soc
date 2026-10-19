class cnn_c06_reset_test extends cnn_ctrl_base_test;
    `uvm_component_utils(cnn_c06_reset_test)

    function new(string name = "cnn_c06_reset_test", uvm_component parent = null);
        super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);
        // The common placeholder does not drain a response interrupted by
        // hard reset; use the control-local reset-aware responder instead.
        cnn_m_axil_responder::type_id::set_type_override(cnn_c05_m_axil_responder::get_type());
        super.build_phase(phase);
    endfunction

    task run_phase(uvm_phase phase);
        cnn_c06_reset_seq seq;
        phase.raise_objection(this);
        wait_reset_release();
        seq = cnn_c06_reset_seq::type_id::create("seq");
        seq.vif = vif;
        seq.start(env.agt.sqr);
        phase.drop_objection(this);
    endtask
endclass
