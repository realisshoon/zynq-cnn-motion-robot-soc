class cnn_c05_progress_test extends cnn_ctrl_base_test;
    `uvm_component_utils(cnn_c05_progress_test)

    function new(string name = "cnn_c05_progress_test", uvm_component parent = null);
        super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);
        // common env creates its responder inside super.build_phase(). Register
        // this C05-only replacement first so no common source needs editing.
        cnn_m_axil_responder::type_id::set_type_override(cnn_c05_m_axil_responder::get_type());
        super.build_phase(phase);
    endfunction

    function void connect_phase(uvm_phase phase);
        cnn_c05_m_axil_responder c05_rsp;
        super.connect_phase(phase);
        // Child build phases have completed by connect_phase, so the factory
        // result can be checked here (env.m_axil_rsp is null in test build).
        if (!$cast(c05_rsp, env.m_axil_rsp) || c05_rsp == null)
            `uvm_fatal("C05_FACTORY", "C05 DMA responder override was not installed")
    endfunction

    task run_phase(uvm_phase phase);
        cnn_c05_progress_seq seq;
        phase.raise_objection(this);
        wait_reset_release();
        seq = cnn_c05_progress_seq::type_id::create("seq");
        seq.start(env.agt.sqr);
        phase.drop_objection(this);
    endtask
endclass
