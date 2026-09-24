package cnn_ctrl_pkg;

    import uvm_pkg::*;
    `include "uvm_macros.svh"

    import cnn_base_pkg::*;

    // ---------------------------------------------------------
    // Control Sequences
    // ---------------------------------------------------------
    `include "cnn_ctrl_base_seq.sv"
    `include "cnn_c01_reset_seq.sv"

    // ---------------------------------------------------------
    // Control Tests
    // ---------------------------------------------------------
    `include "cnn_ctrl_base_test.sv"
    `include "cnn_c01_reset_test.sv"

endpackage