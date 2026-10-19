// Each frame is a separate simulation, reusing G01 expected data and checker.
class cnn_g06_multisample_test extends cnn_g01_e2e_test;
    `uvm_component_utils(cnn_g06_multisample_test)
    function new(string name, uvm_component parent); super.new(name, parent); endfunction
endclass
