interface cnn_g07_probe_if(input logic clk, input logic rst_n);
    logic [63:0] data;
    logic [7:0] keep;
    logic valid, ready, last;
endinterface
