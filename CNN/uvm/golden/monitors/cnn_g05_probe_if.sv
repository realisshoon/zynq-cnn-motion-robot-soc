interface cnn_g05_probe_if (
    input logic clk,
    input logic rst_n
);

    logic busy;
    logic done_pending;

    logic [31:0] result_seq;
    logic [31:0] result_frame_id;

    logic [543:0] joint_words;
    logic [16:0]  joint_flags;

    logic [31:0] red_word;
    logic [31:0] blue_word;
    logic [31:0] green_word;

    logic published_bank;

    logic [543:0] bank0_joints;
    logic [543:0] bank1_joints;

    logic [16:0] bank0_flags;
    logic [16:0] bank1_flags;

endinterface