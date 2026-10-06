// A compact control event emitted only when state or stage changes.
class cnn_c05_event_item extends uvm_sequence_item;
    `uvm_object_utils(cnn_c05_event_item)

    bit [4:0] state, previous_state, requested_op;
    bit [3:0] stage, previous_stage;
    bit load_part;
    bit [6:0] cfg_seen, cfg_required;
    bit stage_complete;
    bit busy, done_pending, error_pending;

    function new(string name = "cnn_c05_event_item");
        super.new(name);
    endfunction
endclass
