class cnn_g01_e2e_test extends cnn_golden_base_test;

    `uvm_component_utils(cnn_g01_e2e_test)

    function new(string name = "cnn_g01_e2e_test", uvm_component parent = null);
        super.new(name, parent);
    endfunction


    task run_phase(uvm_phase phase);

        cnn_g01_e2e_seq seq;

        phase.raise_objection(this);

        `uvm_info(get_type_name(), "G01 E2E test started", UVM_LOW)

        seq = cnn_g01_e2e_seq::type_id::create("seq");

        // env.ag t 안의 main sequencer에서 G01 실행
        seq.start(env.agt.sqr);

        `uvm_info(get_type_name(), "G01 E2E test finished", UVM_LOW)

        phase.drop_objection(this);

    endtask

endclass
