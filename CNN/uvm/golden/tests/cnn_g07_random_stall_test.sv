class cnn_g07_random_stall_test extends cnn_g01_e2e_test;
    `uvm_component_utils(cnn_g07_random_stall_test)
    function new(string name, uvm_component parent); super.new(name, parent); endfunction
    function void build_phase(uvm_phase phase);
        cnn_driver::type_id::set_type_override(cnn_g07_random_driver::get_type());
        super.build_phase(phase);
    endfunction
endclass
