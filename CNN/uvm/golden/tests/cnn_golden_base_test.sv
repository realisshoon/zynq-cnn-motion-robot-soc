class cnn_golden_base_test extends cnn_base_test;
    `uvm_component_utils(cnn_golden_base_test)
    function new(string name = "cnn_golden_base_test", uvm_component parent = null);

        super.new(name, parent);
    endfunction  //new()

    function void build_phase(uvm_phase phase);
        cnn_env::type_id::set_type_override(cnn_golden_env::get_type());
        super.build_phase(phase);
    endfunction

    function void end_of_elaboration_phase(uvm_phase phase);
        super.end_of_elaboration_phase(phase);
        uvm_top.print_topolgy();
    endfunction

endclass  //cnn_golden_base_test extends cnn_base_test
