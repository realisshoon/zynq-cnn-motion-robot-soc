package cnn_golden_pkg;

    import uvm_pkg::*;
    import cnn_base_pkg::*;

    `include "uvm_macros.svh"

    `include "cnn_golden_scoreboard.sv"
    `include "cnn_golden_dma_responder.sv"
    `include "cnn_golden_feature_memory.sv"

    `include "cnn_golden_coverage.sv"
    `include "cnn_g07_random_driver.sv"

    `include "cnn_g02_checkpoint_monitor.sv"
    `include "cnn_g05_atomic_monitor.sv"
    `include "cnn_golden_env.sv"

    `include "cnn_golden_base_seq.sv"
    `include "cnn_g01_e2e_seq.sv"
    `include "cnn_g04_multiframe_seq.sv"
    `include "cnn_g05_atomic_publish_seq.sv"

    `include "cnn_golden_base_test.sv"
    `include "cnn_g01_e2e_test.sv"
    `include "cnn_g02_checkpoint_test.sv"
    `include "cnn_g03_threshold_test.sv"
    `include "cnn_g04_multiframe_test.sv"
    `include "cnn_g05_atomic_publish_test.sv"
    `include "cnn_g06_multisample_test.sv"
    `include "cnn_g07_random_stall_test.sv"

endpackage
