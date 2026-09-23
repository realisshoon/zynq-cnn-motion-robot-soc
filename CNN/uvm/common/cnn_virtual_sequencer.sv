class cnn_virtual_sequencer extends uvm_sequencer;
    `uvm_component_utils(cnn_virtual_sequencer)

    uvm_sequencer #(cnn_seq_item) main_sqr;

    function new(string name, uvm_component parent);
        super.new(name, parent);
    endfunction
endclass
