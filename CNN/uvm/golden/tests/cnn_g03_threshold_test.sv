class cnn_g03_threshold_test extends cnn_golden_base_test;

    `uvm_component_utils(cnn_g03_threshold_test)

    function new(string name = "cnn_g03_threshold_test", uvm_component parent = null);
        super.new(name, parent);
    endfunction

    task run_phase(uvm_phase phase);

        cnn_g01_e2e_seq seq;

        phase.raise_objection(this);

        `uvm_info(get_type_name(), "G03 threshold boundary test started", UVM_LOW)

        seq = cnn_g01_e2e_seq::type_id::create("seq");
        seq.start(env.agt.sqr);

        `uvm_info(get_type_name(), "G03 threshold boundary test finished", UVM_LOW)

        phase.drop_objection(this);

    endtask

endclass
