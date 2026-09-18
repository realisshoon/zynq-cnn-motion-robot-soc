`timescale 1ns / 1ps

module tb_cnn_accelerator_top_partial;
    reg clk;
    reg rst_n;
    wire [31:0] m_axil_awaddr;
    wire m_axil_awvalid;
    reg m_axil_awready;
    wire [2:0] m_axil_awprot;
    wire [31:0] m_axil_wdata;
    wire [3:0] m_axil_wstrb;
    wire m_axil_wvalid;
    reg m_axil_wready;
    reg [1:0] m_axil_bresp;
    reg m_axil_bvalid;
    wire m_axil_bready;
    wire [31:0] m_axil_araddr;
    wire m_axil_arvalid;
    reg m_axil_arready;
    wire [2:0] m_axil_arprot;
    reg [31:0] m_axil_rdata;
    reg [1:0] m_axil_rresp;
    reg m_axil_rvalid;
    wire m_axil_rready;
    reg [11:0] s_axil_awaddr;
    reg s_axil_awvalid;
    wire s_axil_awready;
    reg [2:0] s_axil_awprot;
    reg [31:0] s_axil_wdata;
    reg [3:0] s_axil_wstrb;
    reg s_axil_wvalid;
    wire s_axil_wready;
    wire [1:0] s_axil_bresp;
    wire s_axil_bvalid;
    reg s_axil_bready;
    reg [11:0] s_axil_araddr;
    reg s_axil_arvalid;
    wire s_axil_arready;
    reg [2:0] s_axil_arprot;
    wire [31:0] s_axil_rdata;
    wire [1:0] s_axil_rresp;
    wire s_axil_rvalid;
    reg s_axil_rready;
    reg [63:0] s_image_data;
    reg s_image_valid;
    wire s_image_ready;
    reg s_image_last;
    reg [7:0] s_image_keep;
    reg [63:0] s_weight_data;
    reg s_weight_valid;
    wire s_weight_ready;
    reg s_weight_last;
    reg [7:0] s_weight_keep;
    reg [63:0] s_feature_data;
    reg s_feature_valid;
    wire s_feature_ready;
    reg s_feature_last;
    reg [7:0] s_feature_keep;
    wire [63:0] m_feature_data;
    wire m_feature_valid;
    reg m_feature_ready;
    wire m_feature_last;
    wire [7:0] m_feature_keep;
    wire irq;

    integer failures;
    integer cycle_index;

    cnn_accelerator_top dut (
        .clk(clk),
        .rst_n(rst_n),
        .m_axil_awaddr(m_axil_awaddr),
        .m_axil_awvalid(m_axil_awvalid),
        .m_axil_awready(m_axil_awready),
        .m_axil_awprot(m_axil_awprot),
        .m_axil_wdata(m_axil_wdata),
        .m_axil_wstrb(m_axil_wstrb),
        .m_axil_wvalid(m_axil_wvalid),
        .m_axil_wready(m_axil_wready),
        .m_axil_bresp(m_axil_bresp),
        .m_axil_bvalid(m_axil_bvalid),
        .m_axil_bready(m_axil_bready),
        .m_axil_araddr(m_axil_araddr),
        .m_axil_arvalid(m_axil_arvalid),
        .m_axil_arready(m_axil_arready),
        .m_axil_arprot(m_axil_arprot),
        .m_axil_rdata(m_axil_rdata),
        .m_axil_rresp(m_axil_rresp),
        .m_axil_rvalid(m_axil_rvalid),
        .m_axil_rready(m_axil_rready),
        .s_axil_awaddr(s_axil_awaddr),
        .s_axil_awvalid(s_axil_awvalid),
        .s_axil_awready(s_axil_awready),
        .s_axil_awprot(s_axil_awprot),
        .s_axil_wdata(s_axil_wdata),
        .s_axil_wstrb(s_axil_wstrb),
        .s_axil_wvalid(s_axil_wvalid),
        .s_axil_wready(s_axil_wready),
        .s_axil_bresp(s_axil_bresp),
        .s_axil_bvalid(s_axil_bvalid),
        .s_axil_bready(s_axil_bready),
        .s_axil_araddr(s_axil_araddr),
        .s_axil_arvalid(s_axil_arvalid),
        .s_axil_arready(s_axil_arready),
        .s_axil_arprot(s_axil_arprot),
        .s_axil_rdata(s_axil_rdata),
        .s_axil_rresp(s_axil_rresp),
        .s_axil_rvalid(s_axil_rvalid),
        .s_axil_rready(s_axil_rready),
        .s_image_data(s_image_data),
        .s_image_valid(s_image_valid),
        .s_image_ready(s_image_ready),
        .s_image_last(s_image_last),
        .s_image_keep(s_image_keep),
        .s_weight_data(s_weight_data),
        .s_weight_valid(s_weight_valid),
        .s_weight_ready(s_weight_ready),
        .s_weight_last(s_weight_last),
        .s_weight_keep(s_weight_keep),
        .s_feature_data(s_feature_data),
        .s_feature_valid(s_feature_valid),
        .s_feature_ready(s_feature_ready),
        .s_feature_last(s_feature_last),
        .s_feature_keep(s_feature_keep),
        .m_feature_data(m_feature_data),
        .m_feature_valid(m_feature_valid),
        .m_feature_ready(m_feature_ready),
        .m_feature_last(m_feature_last),
        .m_feature_keep(m_feature_keep),
        .irq(irq)
    );

    always #5 clk = ~clk;

    task check_post_reset;
        begin
            if ((^ {m_axil_awaddr, m_axil_awvalid, m_axil_awprot,
                    m_axil_wdata, m_axil_wstrb, m_axil_wvalid,
                    m_axil_bready, m_axil_araddr, m_axil_arvalid,
                    m_axil_arprot, m_axil_rready, s_axil_awready,
                    s_axil_wready, s_axil_bresp, s_axil_bvalid,
                    s_axil_arready, s_axil_rdata, s_axil_rresp,
                    s_axil_rvalid, s_image_ready, s_weight_ready,
                    s_feature_ready, m_feature_data, m_feature_valid,
                    m_feature_last, m_feature_keep, irq}) === 1'bx) begin
                $display("FAIL: public output X/Z at cycle %0d", cycle_index);
                failures = failures + 1;
            end
            if (irq || m_axil_awvalid || m_axil_wvalid || m_axil_arvalid ||
                m_feature_valid) begin
                $display("FAIL: spontaneous public transaction at cycle %0d", cycle_index);
                failures = failures + 1;
            end
            if (dut.input_done || dut.line_done || dut.dw_done ||
                dut.pw_done || dut.fm_done || dut.down_done ||
                dut.arg_done || dut.coord_done || dut.swap_load_done) begin
                $display("FAIL: spontaneous done at cycle %0d", cycle_index);
                failures = failures + 1;
            end
            if (dut.input_fault || dut.line_fault || dut.dw_fault ||
                dut.pw_fault || dut.fm_fault || dut.down_fault ||
                dut.arg_fault || dut.coord_fault || dut.swap_fault ||
                dut.color_fault || dut.rom_fault) begin
                $display("FAIL: spontaneous fault at cycle %0d", cycle_index);
                failures = failures + 1;
            end
        end
    endtask

    initial begin
        clk = 1'b0;
        rst_n = 1'b0;
        m_axil_awready = 1'd0;
        m_axil_wready = 1'd0;
        m_axil_bresp = 2'd0;
        m_axil_bvalid = 1'd0;
        m_axil_arready = 1'd0;
        m_axil_rdata = 32'd0;
        m_axil_rresp = 2'd0;
        m_axil_rvalid = 1'd0;
        s_axil_awaddr = 12'd0;
        s_axil_awvalid = 1'd0;
        s_axil_awprot = 3'd0;
        s_axil_wdata = 32'd0;
        s_axil_wstrb = 4'd0;
        s_axil_wvalid = 1'd0;
        s_axil_bready = 1'd0;
        s_axil_araddr = 12'd0;
        s_axil_arvalid = 1'd0;
        s_axil_arprot = 3'd0;
        s_axil_rready = 1'd0;
        s_image_data = 64'd0;
        s_image_valid = 1'd0;
        s_image_last = 1'd0;
        s_image_keep = 8'd0;
        s_weight_data = 64'd0;
        s_weight_valid = 1'd0;
        s_weight_last = 1'd0;
        s_weight_keep = 8'd0;
        s_feature_data = 64'd0;
        s_feature_valid = 1'd0;
        s_feature_last = 1'd0;
        s_feature_keep = 8'd0;
        m_feature_ready = 1'd0;
        failures = 0;
        cycle_index = 0;
        repeat (6) @(posedge clk);
        rst_n = 1'b1;
        repeat (2) @(posedge clk);
        for (cycle_index = 0; cycle_index < 40; cycle_index = cycle_index + 1) begin
            @(negedge clk);
            check_post_reset;
        end
        if (failures == 0) begin
            $display("RESET_SMOKE_PASS cycles=40 spontaneous_done=0 spontaneous_fault=0");
            $finish;
        end else begin
            $display("RESET_SMOKE_FAIL failures=%0d", failures);
            $fatal(1);
        end
    end
endmodule
