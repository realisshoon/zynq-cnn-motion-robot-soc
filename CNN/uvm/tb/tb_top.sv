`timescale 1ns/1ps

import uvm_pkg::*;
import gpio_pkg::*;

module tb_top;

    logic clk;
    logic resetn;

    wire [7:0] io_port;

    gpio_if g_if (clk);

    initial clk = 1'b0;
    always #5 clk = ~clk;

    initial begin
        resetn = 1'b0;
        repeat (10) @(posedge clk);
        resetn = 1'b1;
    end

    assign g_if.reset = ~resetn;
    assign g_if.io_port_value = io_port;

    genvar i;
    generate
        for (i = 0; i < 8; i++) begin : gen_gpio_drive
            assign io_port[i] = g_if.gpio_drive_en[i] ? g_if.gpio_drive_data[i] : 1'bz;
        end
    endgenerate

    gpio_v1_0 dut (
        .io_port         (io_port),

        .s00_axi_aclk    (clk),
        .s00_axi_aresetn (resetn),

        .s00_axi_awaddr  (g_if.awaddr),
        .s00_axi_awprot  (3'b000),
        .s00_axi_awvalid (g_if.awvalid),
        .s00_axi_awready (g_if.awready),

        .s00_axi_wdata   (g_if.wdata),
        .s00_axi_wstrb   (g_if.wstrb),
        .s00_axi_wvalid  (g_if.wvalid),
        .s00_axi_wready  (g_if.wready),

        .s00_axi_bresp   (g_if.bresp),
        .s00_axi_bvalid  (g_if.bvalid),
        .s00_axi_bready  (g_if.bready),

        .s00_axi_araddr  (g_if.araddr),
        .s00_axi_arprot  (3'b000),
        .s00_axi_arvalid (g_if.arvalid),
        .s00_axi_arready (g_if.arready),

        .s00_axi_rdata   (g_if.rdata),
        .s00_axi_rresp   (g_if.rresp),
        .s00_axi_rvalid  (g_if.rvalid),
        .s00_axi_rready  (g_if.rready)
    );

    initial begin
        uvm_config_db#(virtual gpio_if)::set(null, "*", "g_if", g_if);
        run_test();
    end

    initial begin
        $fsdbDumpfile("wave.fsdb");
        $fsdbDumpvars(0, tb_top);
    end

endmodule
