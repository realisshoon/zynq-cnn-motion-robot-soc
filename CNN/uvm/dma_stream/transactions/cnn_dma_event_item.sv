typedef enum {
    CNN_DMA_AXIL_WRITE,
    CNN_DMA_AXIL_READ
} cnn_dma_event_kind_e;

class cnn_dma_event_item extends uvm_sequence_item;
    `uvm_object_utils_begin(cnn_dma_event_item)
        `uvm_field_enum(cnn_dma_event_kind_e, kind, UVM_ALL_ON)
        `uvm_field_int(addr, UVM_ALL_ON)
        `uvm_field_int(data, UVM_ALL_ON)
        `uvm_field_int(strb, UVM_ALL_ON)
        `uvm_field_int(resp, UVM_ALL_ON)
    `uvm_object_utils_end

    cnn_dma_event_kind_e kind;

    bit [31:0] addr;
    bit [31:0] data;
    bit [3:0] strb;
    bit [1:0] resp;

    function new(string name = "cnn_dma_event_item");
        super.new(name);

        addr = 32'd0;
        data = 32'd0;
        strb = 4'hF;
        resp = 2'b00;
    endfunction

endclass
