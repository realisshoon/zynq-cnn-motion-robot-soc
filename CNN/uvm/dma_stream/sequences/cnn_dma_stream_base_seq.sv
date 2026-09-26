class cnn_dma_stream_base_seq extends cnn_base_sequence;
    `uvm_object_utils(cnn_dma_stream_base_seq)

    // DMA / Stream Verification configuration
    cnn_dma_stream_cfg cfg;

    function new(string name = "cnn_dma_stream_base_seq");
        super.new(name);
    endfunction

    function void set_cfg(cnn_dma_stream_cfg cfg_i);
        cfg = cfg_i;
    endfunction

    // cfg 안넣고 sequence 실행한 경우 표시 (디버깅을 위한 body)
    task pre_body();
        if (cfg == null) begin
            `uvm_fatal(get_type_name(), "cnn_dma_stream_cfg was not assigned to sequence")
        end
    endtask

    task body();
        // intentionally empty
        // S01 ~ S08 scenario sequences derive from this class
    endtask
    
endclass