class cnn_coverage extends uvm_subscriber #(cnn_seq_item);
    `uvm_component_utils(cnn_coverage)

    cnn_item_kind_e sampled_kind;
    bit [7:0] sampled_keep;
    bit sampled_last;

    covergroup cg;
        option.per_instance = 1;
        cp_kind: coverpoint sampled_kind;
        cp_last: coverpoint sampled_last;
        cp_keep: coverpoint sampled_keep {
            bins full    = {8'hff};
            bins partial = {[8'h01:8'hfe]};
        }
    endgroup

    function new(string name, uvm_component parent);
        super.new(name, parent);
        cg = new();
    endfunction

    function void write(cnn_seq_item t);
        sampled_kind = t.kind;
        sampled_keep = t.keep;
        sampled_last = t.last;
        cg.sample();
    endfunction
endclass
