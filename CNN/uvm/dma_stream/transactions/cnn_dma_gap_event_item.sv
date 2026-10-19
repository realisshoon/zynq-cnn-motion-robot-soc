typedef enum int unsigned {
    DMA_GAP_WEIGHT = 0,
    DMA_GAP_IMAGE  = 1,
    DMA_GAP_FEATURE = 2
} cnn_dma_gap_stream_e;

class cnn_dma_gap_event_item extends uvm_object;

    `uvm_object_utils_begin(cnn_dma_gap_event_item)
        `uvm_field_enum(cnn_dma_gap_stream_e, stream, UVM_ALL_ON)
        `uvm_field_int(gap_cycles, UVM_ALL_ON)
    `uvm_object_utils_end

    cnn_dma_gap_stream_e stream;

    int unsigned gap_cycles;

    function new(string name = "cnn_dma_gap_event_item");

        super.new(name);

        stream     = DMA_GAP_WEIGHT;
        gap_cycles = 0;

    endfunction

endclass