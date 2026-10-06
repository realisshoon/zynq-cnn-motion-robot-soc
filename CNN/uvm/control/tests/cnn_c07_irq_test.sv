class cnn_c07_irq_test extends cnn_c05_progress_test;
    `uvm_component_utils(cnn_c07_irq_test)

    function new(string name = "cnn_c07_irq_test", uvm_component parent = null);
        super.new(name, parent);
    endfunction

    task run_phase(uvm_phase phase);
        cnn_c07_irq_seq seq;
        phase.raise_objection(this);
        wait_reset_release();
        seq = cnn_c07_irq_seq::type_id::create("seq");
        seq.vif = vif;
        seq.start(env.agt.sqr);
        phase.drop_objection(this);
    endtask
endclass
