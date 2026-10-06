class cnn_g02_checkpoint_test extends cnn_golden_base_test;

    `uvm_component_utils(cnn_g02_checkpoint_test)

    function new(
        string name = "cnn_g02_checkpoint_test",
        uvm_component parent = null
    );
        super.new(name, parent);
    endfunction


    function void build_phase(uvm_phase phase);

        uvm_config_db#(bit)::set(
            this,
            "env",
            "g02_enable",
            1'b1
        );

        super.build_phase(phase);

    endfunction


    task run_phase(uvm_phase phase);

        cnn_g01_e2e_seq seq;

        phase.raise_objection(this);

        `uvm_info(
            get_type_name(),
            "G02 checkpoint test started",
            UVM_LOW
        )

        seq = cnn_g01_e2e_seq::type_id::create("seq");

        seq.start(env.agt.sqr);

        `uvm_info(
            get_type_name(),
            "G02 checkpoint test finished",
            UVM_LOW
        )

        phase.drop_objection(this);

    endtask

endclass