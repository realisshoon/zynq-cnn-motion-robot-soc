`timescale 1ns/1ps

import uvm_pkg::*;
import cnn_base_pkg::*;

module tb_top;

    logic clk;
    logic rst_n;

    cnn_if c_if (
        .clk  (clk),
        .rst_n(rst_n)
    );

    initial clk = 1'b0;
    always #5 clk = ~clk;

    initial begin
        rst_n = 1'b0;
        repeat (10) @(posedge clk);
        rst_n = 1'b1;
    end

    cnn_accelerator_top dut (
        .clk              (clk),
        .rst_n            (rst_n),

        .m_axil_awaddr     (c_if.m_axil_awaddr),
        .m_axil_awvalid    (c_if.m_axil_awvalid),
        .m_axil_awready    (c_if.m_axil_awready),
        .m_axil_awprot     (c_if.m_axil_awprot),
        .m_axil_wdata      (c_if.m_axil_wdata),
        .m_axil_wstrb      (c_if.m_axil_wstrb),
        .m_axil_wvalid     (c_if.m_axil_wvalid),
        .m_axil_wready     (c_if.m_axil_wready),
        .m_axil_bresp      (c_if.m_axil_bresp),
        .m_axil_bvalid     (c_if.m_axil_bvalid),
        .m_axil_bready     (c_if.m_axil_bready),
        .m_axil_araddr     (c_if.m_axil_araddr),
        .m_axil_arvalid    (c_if.m_axil_arvalid),
        .m_axil_arready    (c_if.m_axil_arready),
        .m_axil_arprot     (c_if.m_axil_arprot),
        .m_axil_rdata      (c_if.m_axil_rdata),
        .m_axil_rresp      (c_if.m_axil_rresp),
        .m_axil_rvalid     (c_if.m_axil_rvalid),
        .m_axil_rready     (c_if.m_axil_rready),

        .s_axil_awaddr     (c_if.s_axil_awaddr),
        .s_axil_awvalid    (c_if.s_axil_awvalid),
        .s_axil_awready    (c_if.s_axil_awready),
        .s_axil_awprot     (c_if.s_axil_awprot),
        .s_axil_wdata      (c_if.s_axil_wdata),
        .s_axil_wstrb      (c_if.s_axil_wstrb),
        .s_axil_wvalid     (c_if.s_axil_wvalid),
        .s_axil_wready     (c_if.s_axil_wready),
        .s_axil_bresp      (c_if.s_axil_bresp),
        .s_axil_bvalid     (c_if.s_axil_bvalid),
        .s_axil_bready     (c_if.s_axil_bready),
        .s_axil_araddr     (c_if.s_axil_araddr),
        .s_axil_arvalid    (c_if.s_axil_arvalid),
        .s_axil_arready    (c_if.s_axil_arready),
        .s_axil_arprot     (c_if.s_axil_arprot),
        .s_axil_rdata      (c_if.s_axil_rdata),
        .s_axil_rresp      (c_if.s_axil_rresp),
        .s_axil_rvalid     (c_if.s_axil_rvalid),
        .s_axil_rready     (c_if.s_axil_rready),

        .s_image_data      (c_if.s_image_data),
        .s_image_valid     (c_if.s_image_valid),
        .s_image_ready     (c_if.s_image_ready),
        .s_image_last      (c_if.s_image_last),
        .s_image_keep      (c_if.s_image_keep),

        .s_weight_data     (c_if.s_weight_data),
        .s_weight_valid    (c_if.s_weight_valid),
        .s_weight_ready    (c_if.s_weight_ready),
        .s_weight_last     (c_if.s_weight_last),
        .s_weight_keep     (c_if.s_weight_keep),

        .s_feature_data    (c_if.s_feature_data),
        .s_feature_valid   (c_if.s_feature_valid),
        .s_feature_ready   (c_if.s_feature_ready),
        .s_feature_last    (c_if.s_feature_last),
        .s_feature_keep    (c_if.s_feature_keep),

        .m_feature_data    (c_if.m_feature_data),
        .m_feature_valid   (c_if.m_feature_valid),
        .m_feature_ready   (c_if.m_feature_ready),
        .m_feature_last    (c_if.m_feature_last),
        .m_feature_keep    (c_if.m_feature_keep),

        .irq               (c_if.irq)
    );

    initial begin
        uvm_config_db#(virtual cnn_if)::set(null, "*", "vif", c_if);
        run_test();
    end

`ifdef FSDB
    initial begin
        $fsdbDumpfile("cnn_uvm.fsdb");
        $fsdbDumpvars(0, tb_top);
    end
`endif

endmodule
