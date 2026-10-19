package cnn_dma_stream_pkg;

    // Read-only stage bridge populated by the bound checker (single DUT).
    int unsigned observed_stage;
    import uvm_pkg::*;
    import cnn_base_pkg::*;

    `include "uvm_macros.svh"

    `include "cnn_dma_gap_event_item.sv"

    `include "cnn_dma_stream_cfg.sv"
    `include "cnn_dma_event_item.sv"

    `include "cnn_dma_responder.sv"
    `include "cnn_dma_master_monitor.sv"

    `include "cnn_dma_reference_model.sv"
    `include "cnn_dma_stream_coverage.sv"
    `include "cnn_dma_command_checker.sv"
    `include "cnn_dma_stream_scoreboard.sv"

    `include "cnn_dma_random_driver.sv"
    `include "cnn_dma_stream_observer.sv"
    `include "cnn_dma_stream_feeder.sv"
    `include "cnn_dma_stream_env.sv"
    `include "cnn_dma_stream_base_seq.sv"
    `include "cnn_dma_control_smoke_seq.sv"

    `include "cnn_dma_s01_no_stall_seq.sv"
    `include "cnn_dma_s02_input_gap_seq.sv"
    `include "cnn_dma_s03_backpressure_seq.sv"
    `include "cnn_dma_s08_full_flow_seq.sv"

    `include "cnn_dma_stream_base_test.sv"
    `include "cnn_dma_responder_sanity_test.sv"
    `include "cnn_dma_control_smoke_test.sv"
    
    `include "cnn_dma_s01_no_stall_test.sv"
    `include "cnn_dma_s02_input_gap_test.sv"
    `include "cnn_dma_s03_backpressure_test.sv"
    `include "cnn_dma_s04_random_stall_test.sv"
    `include "cnn_dma_s05_axil_protocol_test.sv"
    `include "cnn_dma_s06_weight_command_test.sv"
    `include "cnn_dma_s07_completion_race_test.sv"
    `include "cnn_dma_s08_fm_mapping_test.sv"
    `include "cnn_dma_s04_full_flow_test.sv"
    
endpackage