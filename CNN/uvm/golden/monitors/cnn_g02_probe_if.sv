interface cnn_g02_probe_if (
    input logic clk,
    input logic rst_n
);

    // OP0
    logic [ 63:0] conv0_data;
    logic         conv0_valid;
    logic         conv0_ready;
    logic [  3:0] conv0_mask;
    logic [ 63:0] conv0_tag;

    // DW OP
    logic [255:0] dw_data;
    logic         dw_valid;
    logic         dw_ready;
    logic [ 31:0] dw_mask;
    logic [ 63:0] dw_tag;

    // PW / Head OP
    logic [ 63:0] pw_data;
    logic         pw_valid;
    logic         pw_ready;
    logic [  3:0] pw_mask;
    logic [ 63:0] pw_tag;

endinterface
