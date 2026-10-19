class cnn_dma_control_smoke_seq extends cnn_dma_stream_base_seq;

    `uvm_object_utils(cnn_dma_control_smoke_seq)

    function new(string name = "cnn_dma_control_smoke_seq");
        super.new(name);
    endfunction

    task body();
        `uvm_info(
            get_type_name(),
            "Starting DUT DMA-control integration smoke sequence",
            UVM_LOW
        )

        // DUT START
        //
        // cnn_accelerator_top
        // S_AXI-Lite 0x000
        //
        // bit0 = START

        axil_write(
            12'h000,
            32'h0000_0001
        );

        `uvm_info(
            get_type_name(),
            "START command written to DUT",
            UVM_LOW
        )
    endtask

endclass