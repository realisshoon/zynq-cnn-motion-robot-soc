class cnn_g04_multiframe_test extends cnn_golden_base_test;

    `uvm_component_utils(cnn_g04_multiframe_test)


    function new(string name = "cnn_g04_multiframe_test", uvm_component parent = null);

        super.new(name, parent);

    endfunction


    task run_phase(uvm_phase phase);

        cnn_g04_multiframe_seq seq;

        phase.raise_objection(this);


        `uvm_info(get_type_name(), "G04 multi-frame test started", UVM_LOW)


        seq = cnn_g04_multiframe_seq::type_id::create("seq");


        seq.start(env.agt.sqr);


        `uvm_info(get_type_name(), "G04 multi-frame test finished", UVM_LOW)


        phase.drop_objection(this);

    endtask

endclass
