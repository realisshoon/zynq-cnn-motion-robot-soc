package cnn_ctrl_pkg;

    import uvm_pkg::*;
    `include "uvm_macros.svh"

    import cnn_base_pkg::*;


    // ---------------------------------------------------------
    // C02 Control Extensions
    // ---------------------------------------------------------
    `include "cnn_c02_axil_item.sv"
    `include "cnn_c05_event_item.sv"
    `include "cnn_c02_driver.sv"
    `include "cnn_c05_m_axil_responder.sv"
    `include "cnn_c08_m_axil_responder.sv"
    `include "cnn_c05_fsm_monitor.sv"
    `include "cnn_c05_fsm_checker.sv"
    `include "cnn_c08_fault_checker.sv"
    `include "cnn_c05_fsm_coverage.sv"


    // ---------------------------------------------------------
    // Control Sequences
    // ---------------------------------------------------------
    `include "cnn_ctrl_base_seq.sv"
    `include "cnn_c01_reset_seq.sv"
    `include "cnn_c02_axil_seq.sv"
    `include "cnn_c03_start_seq.sv"
    `include "cnn_c04_config_seq.sv"
    `include "cnn_c05_fsm_seq.sv"
    `include "cnn_c05_progress_seq.sv"
    `include "cnn_c05_full_seq.sv"
    `include "cnn_c06_reset_seq.sv"
    `include "cnn_c07_irq_seq.sv"
    `include "cnn_c08_fault_seq.sv"

    // ---------------------------------------------------------
    // Control Tests
    // ---------------------------------------------------------
    `include "cnn_ctrl_base_test.sv"
    `include "cnn_c01_reset_test.sv"
    `include "cnn_c02_axil_test.sv"
    `include "cnn_c03_start_test.sv"
    `include "cnn_c04_config_test.sv"
    `include "cnn_c05_fsm_test.sv"
    `include "cnn_c05_progress_test.sv"
    `include "cnn_c05_full_test.sv"
    `include "cnn_c06_reset_test.sv"
    `include "cnn_c07_irq_test.sv"
    `include "cnn_c08_fault_test.sv"
endpackage
