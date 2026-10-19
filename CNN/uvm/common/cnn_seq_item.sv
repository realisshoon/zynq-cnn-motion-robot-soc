typedef enum int unsigned {
    CNN_AXIL_WRITE,
    CNN_AXIL_READ,
    CNN_IMAGE_BEAT,
    CNN_WEIGHT_BEAT,
    CNN_FEATURE_IN_BEAT,
    CNN_FEATURE_OUT_BEAT,
    CNN_SET_FEATURE_READY,
    CNN_IRQ_EVENT
} cnn_item_kind_e;

class cnn_seq_item extends uvm_sequence_item;
    rand cnn_item_kind_e kind;

    // AXI4-Lite control payload.
    rand bit [11:0] addr;
    rand bit [31:0] data32;
    rand bit [3:0]  strb;
         bit [31:0] rdata;
         bit [1:0]  resp;

    // AXI-stream payload.
    rand bit [63:0] data64;
    rand bit [7:0]  keep;
    rand bit        last;

    // Sink/backpressure helper.
    rand bit          ready_value;
    rand int unsigned hold_cycles;

    constraint c_defaults {
        strb inside {[4'h1:4'hf]};
        keep inside {[8'h01:8'hff]};
        hold_cycles inside {[1:1000]};
    }

    `uvm_object_utils_begin(cnn_seq_item)
        `uvm_field_enum(cnn_item_kind_e, kind, UVM_ALL_ON)
        `uvm_field_int(addr,        UVM_ALL_ON)
        `uvm_field_int(data32,      UVM_ALL_ON)
        `uvm_field_int(strb,        UVM_ALL_ON)
        `uvm_field_int(rdata,       UVM_ALL_ON)
        `uvm_field_int(resp,        UVM_ALL_ON)
        `uvm_field_int(data64,      UVM_ALL_ON)
        `uvm_field_int(keep,        UVM_ALL_ON)
        `uvm_field_int(last,        UVM_ALL_ON)
        `uvm_field_int(ready_value, UVM_ALL_ON)
        `uvm_field_int(hold_cycles, UVM_ALL_ON)
    `uvm_object_utils_end

    function new(string name = "cnn_seq_item");
        super.new(name);
        strb = 4'hf;
        keep = 8'hff;
        hold_cycles = 1;
    endfunction
endclass
