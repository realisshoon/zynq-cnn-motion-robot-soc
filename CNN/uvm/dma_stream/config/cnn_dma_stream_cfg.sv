typedef enum {DMA_SCENARIO_UNSET, DMA_SCENARIO_S01, DMA_SCENARIO_S02,
    DMA_SCENARIO_S03, DMA_SCENARIO_S04, DMA_SCENARIO_S05,
    DMA_SCENARIO_S06, DMA_SCENARIO_S07, DMA_SCENARIO_S08} cnn_dma_scenario_e;

class cnn_dma_stream_cfg extends uvm_object;
    `uvm_object_utils(cnn_dma_stream_cfg)

    mailbox #(bit [31:0]) weight_commands = new();
    mailbox #(bit [31:0]) feature_commands = new();
    mailbox #(bit [31:0]) image_commands = new();
    cnn_dma_scenario_e scenario_id = DMA_SCENARIO_UNSET;
    int unsigned boundary_stall_cycles = 3;
    int unsigned normal_stalls, last_stalls, observed_stall_cycles, recoveries;
    int unsigned output_accepts, output_last_accepts, simultaneous_cycles;
    int unsigned gap_events[3], gap_cycles[3];
    bit gap_active[3];
    bit random_stall_enable;
    bit full_flow_enable;
    int unsigned random_seed = 1;
    bit axil_order_cycle;
    int unsigned dma_ar_delay = 0;
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

    mailbox #(cnn_dma_gap_event_item) gap_cov_mbox;

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

        gap_cov_mbox = new();
        
    endfunction

endclass
