package cnn_ctrl_pkg;

    import uvm_pkg::*;
    `include "uvm_macros.svh"

    import cnn_base_pkg::*;


    // ---------------------------------------------------------
    // C02 Control Extensions
    // ---------------------------------------------------------
    `include "cnn_c02_axil_item.sv"
    `include "cnn_c02_driver.sv"


    // ---------------------------------------------------------
    // Control Sequences
    // ---------------------------------------------------------
    `include "cnn_ctrl_base_seq.sv"
    `include "cnn_c01_reset_seq.sv"
    `include "cnn_c02_axil_seq.sv"
    `include "cnn_c03_start_seq.sv"
    `include "cnn_c04_config_seq.sv"

    // ---------------------------------------------------------
    // Control Tests
    // ---------------------------------------------------------
    `include "cnn_ctrl_base_test.sv"
    `include "cnn_c01_reset_test.sv"
    `include "cnn_c02_axil_test.sv"
    `include "cnn_c03_start_test.sv"
    `include "cnn_c04_config_test.sv"

endpackage