package cnn_dma_stream_pkg;

    import uvm_pkg::*;
    import cnn_base_pkg::*;

    `include "uvm_macros.svh"

    `include "cnn_dma_stream_cfg.sv"
    `include "cnn_dma_event_item.sv"

    `include "cnn_dma_responder.sv"
    `include "cnn_dma_master_monitor.sv"

    `include "cnn_dma_reference_model.sv"
    `include "cnn_dma_stream_scoreboard.sv"

    `include "cnn_dma_stream_env.sv"
    `include "cnn_dma_stream_base_seq.sv"

endpackage