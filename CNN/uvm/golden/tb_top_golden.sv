`timescale 1ns / 1ps

import uvm_pkg::*;
import cnn_base_pkg::*;

module tb_top_golden;

    logic clk;
    logic rst_n;

    cnn_if c_if (
        .clk  (clk),
        .rst_n(rst_n)
    );

    cnn_g02_probe_if g02_if (
        .clk  (clk),
        .rst_n(rst_n)
    );

    cnn_g05_probe_if g05_if (
        .clk  (clk),
        .rst_n(rst_n)
    );

    initial clk = 1'b0;
    cnn_g07_probe_if g07_if (.clk(clk), .rst_n(rst_n));
    assign g07_if.data = c_if.m_feature_data;
    assign g07_if.keep = c_if.m_feature_keep;
    assign g07_if.valid = c_if.m_feature_valid;
    assign g07_if.ready = c_if.m_feature_ready;
    assign g07_if.last = c_if.m_feature_last;
    always #5 clk = ~clk;

    initial begin
        rst_n = 1'b0;
        repeat (10) @(posedge clk);
        rst_n = 1'b1;
    end


    cnn_accelerator_top dut (
        .clk  (clk),
        .rst_n(rst_n),

        .m_axil_awaddr (c_if.m_axil_awaddr),
        .m_axil_awvalid(c_if.m_axil_awvalid),
        .m_axil_awready(c_if.m_axil_awready),
        .m_axil_awprot (c_if.m_axil_awprot),
        .m_axil_wdata  (c_if.m_axil_wdata),
        .m_axil_wstrb  (c_if.m_axil_wstrb),
        .m_axil_wvalid (c_if.m_axil_wvalid),
        .m_axil_wready (c_if.m_axil_wready),
        .m_axil_bresp  (c_if.m_axil_bresp),
        .m_axil_bvalid (c_if.m_axil_bvalid),
        .m_axil_bready (c_if.m_axil_bready),
        .m_axil_araddr (c_if.m_axil_araddr),
        .m_axil_arvalid(c_if.m_axil_arvalid),
        .m_axil_arready(c_if.m_axil_arready),
        .m_axil_arprot (c_if.m_axil_arprot),
        .m_axil_rdata  (c_if.m_axil_rdata),
        .m_axil_rresp  (c_if.m_axil_rresp),
        .m_axil_rvalid (c_if.m_axil_rvalid),
        .m_axil_rready (c_if.m_axil_rready),

        .s_axil_awaddr (c_if.s_axil_awaddr),
        .s_axil_awvalid(c_if.s_axil_awvalid),
        .s_axil_awready(c_if.s_axil_awready),
        .s_axil_awprot (c_if.s_axil_awprot),
        .s_axil_wdata  (c_if.s_axil_wdata),
        .s_axil_wstrb  (c_if.s_axil_wstrb),
        .s_axil_wvalid (c_if.s_axil_wvalid),
        .s_axil_wready (c_if.s_axil_wready),
        .s_axil_bresp  (c_if.s_axil_bresp),
        .s_axil_bvalid (c_if.s_axil_bvalid),
        .s_axil_bready (c_if.s_axil_bready),
        .s_axil_araddr (c_if.s_axil_araddr),
        .s_axil_arvalid(c_if.s_axil_arvalid),
        .s_axil_arready(c_if.s_axil_arready),
        .s_axil_arprot (c_if.s_axil_arprot),
        .s_axil_rdata  (c_if.s_axil_rdata),
        .s_axil_rresp  (c_if.s_axil_rresp),
        .s_axil_rvalid (c_if.s_axil_rvalid),
        .s_axil_rready (c_if.s_axil_rready),

        .s_image_data (c_if.s_image_data),
        .s_image_valid(c_if.s_image_valid),
        .s_image_ready(c_if.s_image_ready),
        .s_image_last (c_if.s_image_last),
        .s_image_keep (c_if.s_image_keep),

        .s_weight_data (c_if.s_weight_data),
        .s_weight_valid(c_if.s_weight_valid),
        .s_weight_ready(c_if.s_weight_ready),
        .s_weight_last (c_if.s_weight_last),
        .s_weight_keep (c_if.s_weight_keep),

        .s_feature_data (c_if.s_feature_data),
        .s_feature_valid(c_if.s_feature_valid),
        .s_feature_ready(c_if.s_feature_ready),
        .s_feature_last (c_if.s_feature_last),
        .s_feature_keep (c_if.s_feature_keep),

        .m_feature_data (c_if.m_feature_data),
        .m_feature_valid(c_if.m_feature_valid),
        .m_feature_ready(c_if.m_feature_ready),
        .m_feature_last (c_if.m_feature_last),
        .m_feature_keep (c_if.m_feature_keep),

        .irq(c_if.irq)
    );


    // Conv0 output
    assign g02_if.conv0_data      = dut.input_body_data;
    assign g02_if.conv0_valid     = dut.input_body_valid;
    assign g02_if.conv0_ready     = dut.input_body_ready;
    assign g02_if.conv0_mask      = dut.input_body_mask;
    assign g02_if.conv0_tag       = dut.input_body_tag;

    // Depthwise output
    assign g02_if.dw_data         = dut.dw_pixel_data;
    assign g02_if.dw_valid        = dut.dw_pixel_valid;
    assign g02_if.dw_ready        = dut.dw_pixel_ready;
    assign g02_if.dw_mask         = dut.dw_pixel_mask;
    assign g02_if.dw_tag          = dut.dw_pixel_tag;

    // Pointwise / head output
    assign g02_if.pw_data         = dut.pw_value_data;
    assign g02_if.pw_valid        = dut.pw_value_valid;
    assign g02_if.pw_ready        = dut.pw_value_ready;
    assign g02_if.pw_mask         = dut.pw_value_mask;
    assign g02_if.pw_tag          = dut.pw_value_tag;

    // G05 public result
    assign g05_if.busy            = dut.busy;
    assign g05_if.done_pending    = dut.done_pending;
    assign g05_if.result_seq      = dut.result_seq;
    assign g05_if.result_frame_id = dut.result_frame_id;
    assign g05_if.joint_words     = dut.joint_words;
    assign g05_if.joint_flags     = dut.joint_flags;
    assign g05_if.red_word        = dut.red_word;
    assign g05_if.blue_word       = dut.blue_word;
    assign g05_if.green_word      = dut.green_word;

    // G05 result banks
    assign g05_if.published_bank  = dut.u_top_level_fsm.published_bank;
    assign g05_if.bank0_joints    = dut.u_top_level_fsm.joint_words_r;
    assign g05_if.bank1_joints    = dut.u_top_level_fsm.shadow_joints;
    assign g05_if.bank0_flags     = dut.u_top_level_fsm.joint_flags_r;
    assign g05_if.bank1_flags     = dut.u_top_level_fsm.shadow_flags;

    initial begin

        uvm_config_db#(virtual cnn_if)::set(null, "*", "vif", c_if);

        uvm_config_db#(virtual cnn_g02_probe_if)::set(null, "*", "g02_vif", g02_if);

        uvm_config_db#(virtual cnn_g05_probe_if)::set(null, "*", "g05_vif", g05_if);

        run_test();

    end


`ifdef FSDB
    initial begin
        string fsdb_path;
        if ($value$plusargs("FSDB_FILE=%s", fsdb_path)) begin
            $fsdbDumpfile(fsdb_path);
            if ($test$plusargs("FSDB_G05")) $fsdbDumpvars(0, tb_top_golden.g05_if);
            else $fsdbDumpvars(0, tb_top_golden.g07_if);
        end
    end
`endif

endmodule
