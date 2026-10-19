class cnn_base_test extends uvm_test;
    `uvm_component_utils(cnn_base_test)

    cnn_env env;

    function new(string name = "cnn_base_test", uvm_component parent = null);
        super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);
        super.build_phase(phase);
        env = cnn_env::type_id::create("env", this);
    endfunction
endclass
