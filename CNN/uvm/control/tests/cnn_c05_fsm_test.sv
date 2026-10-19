class cnn_c05_fsm_test extends cnn_ctrl_base_test;
    `uvm_component_utils(cnn_c05_fsm_test)

    function new(string name = "cnn_c05_fsm_test", uvm_component parent = null);
        super.new(name, parent);
    endfunction

    task run_phase(uvm_phase phase);
        cnn_c05_fsm_seq seq;
        phase.raise_objection(this);
        wait_reset_release();
        seq = cnn_c05_fsm_seq::type_id::create("seq");
        seq.start(env.agt.sqr);
        phase.drop_objection(this);
    endtask
endclass
