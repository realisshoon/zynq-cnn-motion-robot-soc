interface gpio_if (
    input logic clk
);
    logic reset;

    logic [3:0]  awaddr;
    logic        awvalid;
    logic        awready;
    logic [31:0] wdata;
    logic [3:0]  wstrb;
    logic        wvalid;
    logic        wready;
    logic [1:0]  bresp;
    logic        bvalid;
    logic        bready;

    logic [3:0]  araddr;
    logic        arvalid;
    logic        arready;
    logic [31:0] rdata;
    logic [1:0]  rresp;
    logic        rvalid;
    logic        rready;

    logic [7:0]  gpio_drive_en = 8'h00;
    logic [7:0]  gpio_drive_data = 8'h00;
    logic [7:0]  io_port_value;

    logic        sample_valid;
    logic        sample_is_read;
    logic [3:0]  sample_addr;
    logic [31:0] sample_wdata;
    logic [31:0] sample_rdata;
    logic [7:0]  sample_ext_data;
    logic [7:0]  sample_io;

    clocking drv_cb @(posedge clk);
        default input #1step output #0;

        output awaddr;
        output awvalid;
        input  awready;
        output wdata;
        output wstrb;
        output wvalid;
        input  wready;
        input  bresp;
        input  bvalid;
        output bready;

        output araddr;
        output arvalid;
        input  arready;
        input  rdata;
        input  rresp;
        input  rvalid;
        output rready;
    endclocking

    clocking mon_cb @(posedge clk);
        default input #1step;

        input sample_valid;
        input sample_is_read;
        input sample_addr;
        input sample_wdata;
        input sample_rdata;
        input sample_ext_data;
        input sample_io;
    endclocking

endinterface
