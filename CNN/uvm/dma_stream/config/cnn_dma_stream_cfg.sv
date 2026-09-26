class cnn_dma_stream_cfg extends uvm_object;
    `uvm_object_utils(cnn_dma_stream_cfg)

    bit image_gap_enable;  // s_image_valid에 공백을 넣을지 설정 
    // image_gap_enable = 0 -> valid 1 1 1 1 1, 1 -> valid 1 1 0 0 1 0 1 (gap을 넣을 수 있음)

    bit weight_gap_enable;  // s_weight_valid에 gap을 넣을지 설정

    bit feature_gap_enable;  // s_feature_valid에 gap을 넣을지 설정 
    // (DDR에서 CNN으로 feature map을 보낼 때 매 clock data를 줄 수 없다는 상황을 만듦)

    bit output_stall_enable;  // m_feature_ready를 설정 
    // CNN에서 DMA로 data를 보낼 때 DMA가 못받는 상태 (READY = 0)로 설정

    // gap(몇 cycle 쉬는지 결정)의 최소, 최대값 결정
    int unsigned min_gap_cycles;
    int unsigned max_gap_cycles;

    // ready stall의 최소, 최대값 결정
    int unsigned min_stall_cycles;
    int unsigned max_stall_cycles;

    // DMA / AXI response timing 설정
    int unsigned dma_read_latency;
    int unsigned dma_write_latency;
    int unsigned dma_complete_latency;

    // DMA error
    bit inject_dma_error;

    function new(string name = "cnn_dma_stream_cfg");
        super.new(name);
        image_gap_enable     = 0;
        weight_gap_enable    = 0;
        feature_gap_enable   = 0;
        output_stall_enable  = 0;

        min_gap_cycles       = 0;
        max_gap_cycles       = 0;

        min_stall_cycles     = 0;
        max_stall_cycles     = 0;

        dma_read_latency     = 0;
        dma_write_latency    = 0;
        dma_complete_latency = 0;

        inject_dma_error     = 0;
    endfunction

endclass
