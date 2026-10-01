package cnn_golden_pkg;

    import uvm_pkg::*;
    import cnn_base_pkg::*;

    `include "uvm_macros.svh"

    `include "cnn_golden_scoreboard.sv"
    `include "cnn_golden_dma_responder.sv"
    `include "cnn_golden_feature_memory.sv"

    `include "cnn_g02_checkpoint_monitor.sv"

    `include "cnn_golden_env.sv"

    `include "cnn_golden_base_seq.sv"
    `include "cnn_g01_e2e_seq.sv"

    `include "cnn_golden_base_test.sv"
    `include "cnn_g01_e2e_test.sv"
    `include "cnn_g02_checkpoint_test.sv"

endpackage
