`timescale 1ns / 1ps
module tb_cnn_accelerator_top_reset4;
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
    integer reset_sampled_edges = 0;
    integer reset_xz_failures = 0;
    integer reset_value_failures = 0;
    integer post_reset_idle_cycles = 0;
    integer post_reset_idle_xz_failures = 0;
    integer feature_invalid_data_zero_failures = 0;
    integer spontaneous_done = 0;
    integer spontaneous_fault = 0;
    integer spontaneous_feature_valid = 0;
    integer spontaneous_dma_axil_valid = 0;
    integer spontaneous_result_valid = 0;
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
    task check_reset;
    begin
        reset_sampled_edges = reset_sampled_edges + 1;
        if (^m_axil_awaddr === 1'bx) begin reset_xz_failures = reset_xz_failures + 1; $display("RESET_XZ %s", "m_axil_awaddr"); end
        if (m_axil_awaddr !== 32'd0) begin reset_value_failures = reset_value_failures + 1; $display("RESET_VALUE %s", "m_axil_awaddr"); end
        if (^m_axil_awvalid === 1'bx) begin reset_xz_failures = reset_xz_failures + 1; $display("RESET_XZ %s", "m_axil_awvalid"); end
        if (m_axil_awvalid !== 1'd0) begin reset_value_failures = reset_value_failures + 1; $display("RESET_VALUE %s", "m_axil_awvalid"); end
        if (^m_axil_awprot === 1'bx) begin reset_xz_failures = reset_xz_failures + 1; $display("RESET_XZ %s", "m_axil_awprot"); end
        if (m_axil_awprot !== 3'd0) begin reset_value_failures = reset_value_failures + 1; $display("RESET_VALUE %s", "m_axil_awprot"); end
        if (^m_axil_wdata === 1'bx) begin reset_xz_failures = reset_xz_failures + 1; $display("RESET_XZ %s", "m_axil_wdata"); end
        if (m_axil_wdata !== 32'd0) begin reset_value_failures = reset_value_failures + 1; $display("RESET_VALUE %s", "m_axil_wdata"); end
        if (^m_axil_wstrb === 1'bx) begin reset_xz_failures = reset_xz_failures + 1; $display("RESET_XZ %s", "m_axil_wstrb"); end
        if (m_axil_wstrb !== 4'd0) begin reset_value_failures = reset_value_failures + 1; $display("RESET_VALUE %s", "m_axil_wstrb"); end
        if (^m_axil_wvalid === 1'bx) begin reset_xz_failures = reset_xz_failures + 1; $display("RESET_XZ %s", "m_axil_wvalid"); end
        if (m_axil_wvalid !== 1'd0) begin reset_value_failures = reset_value_failures + 1; $display("RESET_VALUE %s", "m_axil_wvalid"); end
        if (^m_axil_bready === 1'bx) begin reset_xz_failures = reset_xz_failures + 1; $display("RESET_XZ %s", "m_axil_bready"); end
        if (m_axil_bready !== 1'd0) begin reset_value_failures = reset_value_failures + 1; $display("RESET_VALUE %s", "m_axil_bready"); end
        if (^m_axil_araddr === 1'bx) begin reset_xz_failures = reset_xz_failures + 1; $display("RESET_XZ %s", "m_axil_araddr"); end
        if (m_axil_araddr !== 32'd0) begin reset_value_failures = reset_value_failures + 1; $display("RESET_VALUE %s", "m_axil_araddr"); end
        if (^m_axil_arvalid === 1'bx) begin reset_xz_failures = reset_xz_failures + 1; $display("RESET_XZ %s", "m_axil_arvalid"); end
        if (m_axil_arvalid !== 1'd0) begin reset_value_failures = reset_value_failures + 1; $display("RESET_VALUE %s", "m_axil_arvalid"); end
        if (^m_axil_arprot === 1'bx) begin reset_xz_failures = reset_xz_failures + 1; $display("RESET_XZ %s", "m_axil_arprot"); end
        if (m_axil_arprot !== 3'd0) begin reset_value_failures = reset_value_failures + 1; $display("RESET_VALUE %s", "m_axil_arprot"); end
        if (^m_axil_rready === 1'bx) begin reset_xz_failures = reset_xz_failures + 1; $display("RESET_XZ %s", "m_axil_rready"); end
        if (m_axil_rready !== 1'd0) begin reset_value_failures = reset_value_failures + 1; $display("RESET_VALUE %s", "m_axil_rready"); end
        if (^s_axil_awready === 1'bx) begin reset_xz_failures = reset_xz_failures + 1; $display("RESET_XZ %s", "s_axil_awready"); end
        if (s_axil_awready !== 1'd0) begin reset_value_failures = reset_value_failures + 1; $display("RESET_VALUE %s", "s_axil_awready"); end
        if (^s_axil_wready === 1'bx) begin reset_xz_failures = reset_xz_failures + 1; $display("RESET_XZ %s", "s_axil_wready"); end
        if (s_axil_wready !== 1'd0) begin reset_value_failures = reset_value_failures + 1; $display("RESET_VALUE %s", "s_axil_wready"); end
        if (^s_axil_bresp === 1'bx) begin reset_xz_failures = reset_xz_failures + 1; $display("RESET_XZ %s", "s_axil_bresp"); end
        if (s_axil_bresp !== 2'd0) begin reset_value_failures = reset_value_failures + 1; $display("RESET_VALUE %s", "s_axil_bresp"); end
        if (^s_axil_bvalid === 1'bx) begin reset_xz_failures = reset_xz_failures + 1; $display("RESET_XZ %s", "s_axil_bvalid"); end
        if (s_axil_bvalid !== 1'd0) begin reset_value_failures = reset_value_failures + 1; $display("RESET_VALUE %s", "s_axil_bvalid"); end
        if (^s_axil_arready === 1'bx) begin reset_xz_failures = reset_xz_failures + 1; $display("RESET_XZ %s", "s_axil_arready"); end
        if (s_axil_arready !== 1'd0) begin reset_value_failures = reset_value_failures + 1; $display("RESET_VALUE %s", "s_axil_arready"); end
        if (^s_axil_rdata === 1'bx) begin reset_xz_failures = reset_xz_failures + 1; $display("RESET_XZ %s", "s_axil_rdata"); end
        if (s_axil_rdata !== 32'd0) begin reset_value_failures = reset_value_failures + 1; $display("RESET_VALUE %s", "s_axil_rdata"); end
        if (^s_axil_rresp === 1'bx) begin reset_xz_failures = reset_xz_failures + 1; $display("RESET_XZ %s", "s_axil_rresp"); end
        if (s_axil_rresp !== 2'd0) begin reset_value_failures = reset_value_failures + 1; $display("RESET_VALUE %s", "s_axil_rresp"); end
        if (^s_axil_rvalid === 1'bx) begin reset_xz_failures = reset_xz_failures + 1; $display("RESET_XZ %s", "s_axil_rvalid"); end
        if (s_axil_rvalid !== 1'd0) begin reset_value_failures = reset_value_failures + 1; $display("RESET_VALUE %s", "s_axil_rvalid"); end
        if (^s_image_ready === 1'bx) begin reset_xz_failures = reset_xz_failures + 1; $display("RESET_XZ %s", "s_image_ready"); end
        if (s_image_ready !== 1'd0) begin reset_value_failures = reset_value_failures + 1; $display("RESET_VALUE %s", "s_image_ready"); end
        if (^s_weight_ready === 1'bx) begin reset_xz_failures = reset_xz_failures + 1; $display("RESET_XZ %s", "s_weight_ready"); end
        if (s_weight_ready !== 1'd0) begin reset_value_failures = reset_value_failures + 1; $display("RESET_VALUE %s", "s_weight_ready"); end
        if (^s_feature_ready === 1'bx) begin reset_xz_failures = reset_xz_failures + 1; $display("RESET_XZ %s", "s_feature_ready"); end
        if (s_feature_ready !== 1'd0) begin reset_value_failures = reset_value_failures + 1; $display("RESET_VALUE %s", "s_feature_ready"); end
        if (^m_feature_data === 1'bx) begin reset_xz_failures = reset_xz_failures + 1; $display("RESET_XZ %s", "m_feature_data"); end
        if (m_feature_data !== 64'd0) begin reset_value_failures = reset_value_failures + 1; $display("RESET_VALUE %s", "m_feature_data"); end
        if (^m_feature_valid === 1'bx) begin reset_xz_failures = reset_xz_failures + 1; $display("RESET_XZ %s", "m_feature_valid"); end
        if (m_feature_valid !== 1'd0) begin reset_value_failures = reset_value_failures + 1; $display("RESET_VALUE %s", "m_feature_valid"); end
        if (^m_feature_last === 1'bx) begin reset_xz_failures = reset_xz_failures + 1; $display("RESET_XZ %s", "m_feature_last"); end
        if (m_feature_last !== 1'd0) begin reset_value_failures = reset_value_failures + 1; $display("RESET_VALUE %s", "m_feature_last"); end
        if (^m_feature_keep === 1'bx) begin reset_xz_failures = reset_xz_failures + 1; $display("RESET_XZ %s", "m_feature_keep"); end
        if (m_feature_keep !== 8'd0) begin reset_value_failures = reset_value_failures + 1; $display("RESET_VALUE %s", "m_feature_keep"); end
        if (^irq === 1'bx) begin reset_xz_failures = reset_xz_failures + 1; $display("RESET_XZ %s", "irq"); end
        if (irq !== 1'd0) begin reset_value_failures = reset_value_failures + 1; $display("RESET_VALUE %s", "irq"); end
    end
    endtask
    task check_idle;
    begin
        post_reset_idle_cycles = post_reset_idle_cycles + 1;
        if (^m_axil_awaddr === 1'bx) begin post_reset_idle_xz_failures = post_reset_idle_xz_failures + 1; $display("IDLE_XZ %s", "m_axil_awaddr"); end
        if (^m_axil_awvalid === 1'bx) begin post_reset_idle_xz_failures = post_reset_idle_xz_failures + 1; $display("IDLE_XZ %s", "m_axil_awvalid"); end
        if (^m_axil_awprot === 1'bx) begin post_reset_idle_xz_failures = post_reset_idle_xz_failures + 1; $display("IDLE_XZ %s", "m_axil_awprot"); end
        if (^m_axil_wdata === 1'bx) begin post_reset_idle_xz_failures = post_reset_idle_xz_failures + 1; $display("IDLE_XZ %s", "m_axil_wdata"); end
        if (^m_axil_wstrb === 1'bx) begin post_reset_idle_xz_failures = post_reset_idle_xz_failures + 1; $display("IDLE_XZ %s", "m_axil_wstrb"); end
        if (^m_axil_wvalid === 1'bx) begin post_reset_idle_xz_failures = post_reset_idle_xz_failures + 1; $display("IDLE_XZ %s", "m_axil_wvalid"); end
        if (^m_axil_bready === 1'bx) begin post_reset_idle_xz_failures = post_reset_idle_xz_failures + 1; $display("IDLE_XZ %s", "m_axil_bready"); end
        if (^m_axil_araddr === 1'bx) begin post_reset_idle_xz_failures = post_reset_idle_xz_failures + 1; $display("IDLE_XZ %s", "m_axil_araddr"); end
        if (^m_axil_arvalid === 1'bx) begin post_reset_idle_xz_failures = post_reset_idle_xz_failures + 1; $display("IDLE_XZ %s", "m_axil_arvalid"); end
        if (^m_axil_arprot === 1'bx) begin post_reset_idle_xz_failures = post_reset_idle_xz_failures + 1; $display("IDLE_XZ %s", "m_axil_arprot"); end
        if (^m_axil_rready === 1'bx) begin post_reset_idle_xz_failures = post_reset_idle_xz_failures + 1; $display("IDLE_XZ %s", "m_axil_rready"); end
        if (^s_axil_awready === 1'bx) begin post_reset_idle_xz_failures = post_reset_idle_xz_failures + 1; $display("IDLE_XZ %s", "s_axil_awready"); end
        if (^s_axil_wready === 1'bx) begin post_reset_idle_xz_failures = post_reset_idle_xz_failures + 1; $display("IDLE_XZ %s", "s_axil_wready"); end
        if (^s_axil_bresp === 1'bx) begin post_reset_idle_xz_failures = post_reset_idle_xz_failures + 1; $display("IDLE_XZ %s", "s_axil_bresp"); end
        if (^s_axil_bvalid === 1'bx) begin post_reset_idle_xz_failures = post_reset_idle_xz_failures + 1; $display("IDLE_XZ %s", "s_axil_bvalid"); end
        if (^s_axil_arready === 1'bx) begin post_reset_idle_xz_failures = post_reset_idle_xz_failures + 1; $display("IDLE_XZ %s", "s_axil_arready"); end
        if (^s_axil_rdata === 1'bx) begin post_reset_idle_xz_failures = post_reset_idle_xz_failures + 1; $display("IDLE_XZ %s", "s_axil_rdata"); end
        if (^s_axil_rresp === 1'bx) begin post_reset_idle_xz_failures = post_reset_idle_xz_failures + 1; $display("IDLE_XZ %s", "s_axil_rresp"); end
        if (^s_axil_rvalid === 1'bx) begin post_reset_idle_xz_failures = post_reset_idle_xz_failures + 1; $display("IDLE_XZ %s", "s_axil_rvalid"); end
        if (^s_image_ready === 1'bx) begin post_reset_idle_xz_failures = post_reset_idle_xz_failures + 1; $display("IDLE_XZ %s", "s_image_ready"); end
        if (^s_weight_ready === 1'bx) begin post_reset_idle_xz_failures = post_reset_idle_xz_failures + 1; $display("IDLE_XZ %s", "s_weight_ready"); end
        if (^s_feature_ready === 1'bx) begin post_reset_idle_xz_failures = post_reset_idle_xz_failures + 1; $display("IDLE_XZ %s", "s_feature_ready"); end
        if (^m_feature_data === 1'bx) begin post_reset_idle_xz_failures = post_reset_idle_xz_failures + 1; $display("IDLE_XZ %s", "m_feature_data"); end
        if (^m_feature_valid === 1'bx) begin post_reset_idle_xz_failures = post_reset_idle_xz_failures + 1; $display("IDLE_XZ %s", "m_feature_valid"); end
        if (^m_feature_last === 1'bx) begin post_reset_idle_xz_failures = post_reset_idle_xz_failures + 1; $display("IDLE_XZ %s", "m_feature_last"); end
        if (^m_feature_keep === 1'bx) begin post_reset_idle_xz_failures = post_reset_idle_xz_failures + 1; $display("IDLE_XZ %s", "m_feature_keep"); end
        if (^irq === 1'bx) begin post_reset_idle_xz_failures = post_reset_idle_xz_failures + 1; $display("IDLE_XZ %s", "irq"); end
        if (m_feature_valid !== 1'b0) spontaneous_feature_valid = spontaneous_feature_valid + 1;
        if (m_feature_valid === 1'b0 && m_feature_data !== 64'd0) feature_invalid_data_zero_failures = feature_invalid_data_zero_failures + 1;
        if (m_feature_last !== 1'b0 || m_feature_keep !== 8'd0) feature_invalid_data_zero_failures = feature_invalid_data_zero_failures + 1;
        if (dut.done_pending !== 1'b0 || dut.busy !== 1'b0) spontaneous_done = spontaneous_done + 1;
        if (dut.error_pending !== 1'b0) spontaneous_fault = spontaneous_fault + 1;
        if (m_axil_awvalid !== 1'b0 || m_axil_wvalid !== 1'b0 || m_axil_arvalid !== 1'b0) spontaneous_dma_axil_valid = spontaneous_dma_axil_valid + 1;
        if (irq !== 1'b0 || s_axil_bvalid !== 1'b0 || s_axil_rvalid !== 1'b0) spontaneous_result_valid = spontaneous_result_valid + 1;
    end
    endtask
    initial begin
        clk = 1'd0;
        rst_n = 1'd0;
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
        repeat (6) begin
            @(posedge clk); #1; check_reset();
        end
        @(negedge clk); rst_n = 1'b1;
        repeat (40) begin
            @(posedge clk); #1; check_idle();
        end
        $display("PB2R1_4STATE_RESET_SUMMARY");
        $display("RESET_SAMPLED_EDGES=%0d",reset_sampled_edges);
        $display("RESET_XZ_FAILURES=%0d",reset_xz_failures);
        $display("RESET_VALUE_FAILURES=%0d",reset_value_failures);
        $display("POST_RESET_IDLE_CYCLES=%0d",post_reset_idle_cycles);
        $display("POST_RESET_IDLE_XZ_FAILURES=%0d",post_reset_idle_xz_failures);
        $display("FEATURE_INVALID_DATA_ZERO_FAILURES=%0d",feature_invalid_data_zero_failures);
        $display("SPONTANEOUS_DONE=%0d",spontaneous_done);
        $display("SPONTANEOUS_FAULT=%0d",spontaneous_fault);
        $display("SPONTANEOUS_FEATURE_VALID=%0d",spontaneous_feature_valid);
        $display("SPONTANEOUS_DMA_AXIL_VALID=%0d",spontaneous_dma_axil_valid);
        $display("SPONTANEOUS_RESULT_VALID=%0d",spontaneous_result_valid);
        if (reset_sampled_edges == 6 && post_reset_idle_cycles == 40 && reset_xz_failures == 0 && reset_value_failures == 0 && post_reset_idle_xz_failures == 0 && feature_invalid_data_zero_failures == 0 && spontaneous_done == 0 && spontaneous_fault == 0 && spontaneous_feature_valid == 0 && spontaneous_dma_axil_valid == 0 && spontaneous_result_valid == 0) begin
            $display("PB2R1_4STATE_RESET=PASS"); $finish;
        end else begin
            $display("PB2R1_4STATE_RESET=FAIL"); $fatal(1,"Reset/idle contract failure");
        end
    end
endmodule
