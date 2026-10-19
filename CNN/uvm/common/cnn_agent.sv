class cnn_agent extends uvm_agent;
    `uvm_component_utils(cnn_agent)

    uvm_sequencer #(cnn_seq_item) sqr;
    cnn_driver                    drv;
    cnn_monitor                   mon;

    function new(string name, uvm_component parent);
        super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);
        super.build_phase(phase);

        mon = cnn_monitor::type_id::create("mon", this);

        if (get_is_active() == UVM_ACTIVE) begin
            sqr = uvm_sequencer#(cnn_seq_item)::type_id::create("sqr", this);
            drv = cnn_driver::type_id::create("drv", this);
        end
    endfunction

    function void connect_phase(uvm_phase phase);
        super.connect_phase(phase);
        if (get_is_active() == UVM_ACTIVE)
            drv.seq_item_port.connect(sqr.seq_item_export);
    endfunction
endclass
