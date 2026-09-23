package cnn_base_pkg;
    import uvm_pkg::*;
    `include "uvm_macros.svh"

    `include "cnn_seq_item.sv"
    `include "cnn_base_sequence.sv"
    `include "cnn_driver.sv"
    `include "cnn_monitor.sv"
    `include "cnn_agent.sv"
    `include "cnn_virtual_sequencer.sv"
    `include "cnn_m_axil_responder.sv"
    `include "cnn_scoreboard.sv"
    `include "cnn_coverage.sv"
    `include "cnn_env.sv"
    `include "cnn_base_test.sv"
endpackage
