class cnn_c08_fault_test extends cnn_ctrl_base_test;
    `uvm_component_utils(cnn_c08_fault_test)

    cnn_c08_fault_checker c08_checker;
    cnn_c08_m_axil_responder c08_rsp;

    function new(string name = "cnn_c08_fault_test", uvm_component parent = null);
        super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);
        // The common env creates m_axil_rsp in super.build_phase(), so the
        // control-only factory override must be registered first.
        cnn_m_axil_responder::type_id::set_type_override(cnn_c08_m_axil_responder::get_type());
        super.build_phase(phase);
        c08_checker = cnn_c08_fault_checker::type_id::create("c08_checker", this);
    endfunction

    function void connect_phase(uvm_phase phase);
        super.connect_phase(phase);
        if (!$cast(c08_rsp, env.m_axil_rsp) || c08_rsp == null)
            `uvm_fatal("C08_FACTORY", "C08 fault responder override was not installed")
    endfunction

    task run_phase(uvm_phase phase);
        cnn_c08_fault_seq seq;
        phase.raise_objection(this);
        wait_reset_release();
        seq = cnn_c08_fault_seq::type_id::create("seq");
        seq.vif = vif;
        seq.rsp = c08_rsp;
        seq.checker = c08_checker;
        seq.start(env.agt.sqr);
        phase.drop_objection(this);
    endtask
endclass
