interface cnn_if (
    input logic clk,
    input logic rst_n
);

    // ---------------------------------------------------------------------
    // DUT AXI4-Lite master: DUT drives request, TB provides response.
    // ---------------------------------------------------------------------
    logic [31:0] m_axil_awaddr;
    logic        m_axil_awvalid;
    logic        m_axil_awready;
    logic [2:0]  m_axil_awprot;
    logic [31:0] m_axil_wdata;
    logic [3:0]  m_axil_wstrb;
    logic        m_axil_wvalid;
    logic        m_axil_wready;
    logic [1:0]  m_axil_bresp;
    logic        m_axil_bvalid;
    logic        m_axil_bready;
    logic [31:0] m_axil_araddr;
    logic        m_axil_arvalid;
    logic        m_axil_arready;
    logic [2:0]  m_axil_arprot;
    logic [31:0] m_axil_rdata;
    logic [1:0]  m_axil_rresp;
    logic        m_axil_rvalid;
    logic        m_axil_rready;

    // ---------------------------------------------------------------------
    // DUT AXI4-Lite slave: TB drives control/configuration accesses.
    // ---------------------------------------------------------------------
    logic [11:0] s_axil_awaddr;
    logic        s_axil_awvalid;
    logic        s_axil_awready;
    logic [2:0]  s_axil_awprot;
    logic [31:0] s_axil_wdata;
    logic [3:0]  s_axil_wstrb;
    logic        s_axil_wvalid;
    logic        s_axil_wready;
    logic [1:0]  s_axil_bresp;
    logic        s_axil_bvalid;
    logic        s_axil_bready;
    logic [11:0] s_axil_araddr;
    logic        s_axil_arvalid;
    logic        s_axil_arready;
    logic [2:0]  s_axil_arprot;
    logic [31:0] s_axil_rdata;
    logic [1:0]  s_axil_rresp;
    logic        s_axil_rvalid;
    logic        s_axil_rready;

    // AXI-stream inputs.
    logic [63:0] s_image_data;
    logic        s_image_valid;
    logic        s_image_ready;
    logic        s_image_last;
    logic [7:0]  s_image_keep;

    logic [63:0] s_weight_data;
    logic        s_weight_valid;
    logic        s_weight_ready;
    logic        s_weight_last;
    logic [7:0]  s_weight_keep;

    logic [63:0] s_feature_data;
    logic        s_feature_valid;
    logic        s_feature_ready;
    logic        s_feature_last;
    logic [7:0]  s_feature_keep;

    // AXI-stream output.
    logic [63:0] m_feature_data;
    logic        m_feature_valid;
    logic        m_feature_ready;
    logic        m_feature_last;
    logic [7:0]  m_feature_keep;

    logic irq;

    // Control/config driver.
    clocking ctrl_drv_cb @(posedge clk);
        default input #1step output #0;
        output s_axil_awaddr, s_axil_awvalid, s_axil_awprot;
        input  s_axil_awready;
        output s_axil_wdata, s_axil_wstrb, s_axil_wvalid;
        input  s_axil_wready;
        input  s_axil_bresp, s_axil_bvalid;
        output s_axil_bready;
        output s_axil_araddr, s_axil_arvalid, s_axil_arprot;
        input  s_axil_arready;
        input  s_axil_rdata, s_axil_rresp, s_axil_rvalid;
        output s_axil_rready;
    endclocking

    clocking image_drv_cb @(posedge clk);
        default input #1step output #0;
        output s_image_data, s_image_valid, s_image_last, s_image_keep;
        input  s_image_ready;
    endclocking

    clocking weight_drv_cb @(posedge clk);
        default input #1step output #0;
        output s_weight_data, s_weight_valid, s_weight_last, s_weight_keep;
        input  s_weight_ready;
    endclocking

    clocking feature_in_drv_cb @(posedge clk);
        default input #1step output #0;
        output s_feature_data, s_feature_valid, s_feature_last, s_feature_keep;
        input  s_feature_ready;
    endclocking

    clocking feature_out_sink_cb @(posedge clk);
        default input #1step output #0;
        input  m_feature_data, m_feature_valid, m_feature_last, m_feature_keep;
        output m_feature_ready;
    endclocking

    // Placeholder DMA/register responder for DUT AXI-Lite master.
    clocking m_axil_rsp_cb @(posedge clk);
        default input #1step output #0;
        input  m_axil_awaddr, m_axil_awvalid, m_axil_awprot;
        output m_axil_awready;
        input  m_axil_wdata, m_axil_wstrb, m_axil_wvalid;
        output m_axil_wready;
        output m_axil_bresp, m_axil_bvalid;
        input  m_axil_bready;
        input  m_axil_araddr, m_axil_arvalid, m_axil_arprot;
        output m_axil_arready;
        output m_axil_rdata, m_axil_rresp, m_axil_rvalid;
        input  m_axil_rready;
    endclocking

    // Passive observation of accepted external stream traffic and IRQ.
    clocking mon_cb @(posedge clk);
        default input #1step;
        input s_image_data, s_image_valid, s_image_ready, s_image_last, s_image_keep;
        input s_weight_data, s_weight_valid, s_weight_ready, s_weight_last, s_weight_keep;
        input s_feature_data, s_feature_valid, s_feature_ready, s_feature_last, s_feature_keep;
        input m_feature_data, m_feature_valid, m_feature_ready, m_feature_last, m_feature_keep;
        input irq;
    endclocking

endinterface
