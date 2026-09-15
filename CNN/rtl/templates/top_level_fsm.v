// CNN-v4.0 | Verilog-2001 skeleton | technical contract unchanged
module top_level_fsm (
    input wire clk,
    input wire rst_n,
    output wire input_cfg_valid,
    input wire input_cfg_ready,
    output wire [255:0] input_cfg_desc,
    input wire input_done,
    input wire input_fault,
    output wire line_cfg_valid,
    input wire line_cfg_ready,
    output wire [255:0] line_cfg_desc,
    input wire line_done,
    input wire line_fault,
    output wire dw_cfg_valid,
    input wire dw_cfg_ready,
    output wire [255:0] dw_cfg_desc,
    input wire dw_done,
    input wire dw_fault,
    output wire pw_cfg_valid,
    input wire pw_cfg_ready,
    output wire [255:0] pw_cfg_desc,
    input wire pw_done,
    input wire pw_fault,
    output wire fm_cfg_valid,
    input wire fm_cfg_ready,
    output wire [255:0] fm_cfg_desc,
    input wire fm_done,
    input wire fm_fault,
    output wire [31:0] fm_src_addr,
    output wire [31:0] fm_dst_addr,
    output wire [19:0] fm_src_bytes,
    output wire [19:0] fm_dst_bytes,
    output wire fm_read_en,
    output wire fm_write_en,
    output wire fm_read_dma_done,
    output wire fm_write_dma_done,
    output wire fm_dma_error,
    input wire fm_read_done,
    input wire fm_write_done,
    output wire down_cfg_valid,
    input wire down_cfg_ready,
    output wire [255:0] down_cfg_desc,
    input wire down_done,
    input wire down_fault,
    output wire arg_cfg_valid,
    input wire arg_cfg_ready,
    output wire [255:0] arg_cfg_desc,
    input wire arg_done,
    input wire arg_fault,
    output wire coord_cfg_valid,
    input wire coord_cfg_ready,
    output wire [255:0] coord_cfg_desc,
    input wire coord_done,
    input wire coord_fault,
    input wire [31:0] coord_m_joint_data,
    input wire coord_m_joint_valid,
    output wire coord_m_joint_ready,
    input wire coord_m_joint_last,
    input wire [4:0] coord_m_joint_index,
    input wire coord_m_joint_good,
    output wire [7:0] coord_threshold,
    input wire swap_fault,
    output wire swap_load_valid,
    input wire swap_load_ready,
    output wire [255:0] swap_load_desc,
    input wire swap_load_done,
    input wire color_fault,
    output wire color_frame_start,
    output wire [23:0] color_red_cfg,
    output wire [23:0] color_blue_cfg,
    output wire [17:0] color_min_count,
    input wire [31:0] color_red_word,
    input wire [31:0] color_blue_word,
    input wire color_results_valid,
    input wire rom_fault,
    output wire rom_req_valid,
    input wire rom_req_ready,
    output wire [4:0] rom_req_op,
    input wire rom_rsp_valid,
    output wire rom_rsp_ready,
    input wire [255:0] rom_rsp_desc,
    input wire [7:0] threshold_cfg,
    input wire [23:0] red_thresh_cfg,
    input wire [23:0] blue_thresh_cfg,
    input wire [17:0] min_count_cfg,
    input wire datapath_progress,
    input wire start,
    input wire clear_done,
    input wire clear_error,
    input wire [31:0] frame_id,
    input wire [31:0] sg_desc_base,
    input wire [31:0] wgt_base,
    input wire [31:0] fm_a_base,
    input wire [31:0] fm_b_base,
    input wire [31:0] timeout_cycles,
    output wire busy,
    output wire done_pending,
    output wire error_pending,
    output wire [31:0] error_code,
    output wire [31:0] result_seq,
    output wire [31:0] result_frame_id,
    output wire [543:0] joint_words,
    output wire [16:0] joint_flags,
    output wire [31:0] red_word,
    output wire [31:0] blue_word,
    output wire [31:0] cycle_count,
    output wire image_read_done,
    output wire [31:0] m_axil_awaddr,
    output wire m_axil_awvalid,
    input wire m_axil_awready,
    output wire [2:0] m_axil_awprot,
    output wire [31:0] m_axil_wdata,
    output wire [3:0] m_axil_wstrb,
    output wire m_axil_wvalid,
    input wire m_axil_wready,
    input wire [1:0] m_axil_bresp,
    input wire m_axil_bvalid,
    output wire m_axil_bready,
    output wire [31:0] m_axil_araddr,
    output wire m_axil_arvalid,
    input wire m_axil_arready,
    output wire [2:0] m_axil_arprot,
    input wire [31:0] m_axil_rdata,
    input wire [1:0] m_axil_rresp,
    input wire m_axil_rvalid,
    output wire m_axil_rready
);

// Fixed common parameters; do not change the external port widths.
localparam integer CNN_W_IN = 32;
localparam integer CNN_W_OUT = 4;
localparam integer CNN_ACCUM_WIDTH = 24;
localparam integer CNN_REQUANT_IN_WIDTH = 25;
localparam integer CNN_M_WIDTH = 18;
localparam integer CNN_SHIFT = 16;
localparam integer CNN_OPS = 29;
localparam integer CNN_STAGES = 16;
localparam [31:0] CNN_VERSION = 32'h00040000;

localparam integer CFG_OP_ID_LSB = 0, CFG_OP_ID_WIDTH = 5;
localparam integer CFG_KIND_LSB = 5, CFG_KIND_WIDTH = 2;
localparam integer CFG_MODE_LSB = 7, CFG_MODE_WIDTH = 2;
localparam integer CFG_HIN_LSB = 9, CFG_HIN_WIDTH = 9;
localparam integer CFG_WIN_LSB = 18, CFG_WIN_WIDTH = 9;
localparam integer CFG_HOUT_LSB = 27, CFG_HOUT_WIDTH = 9;
localparam integer CFG_WOUT_LSB = 36, CFG_WOUT_WIDTH = 9;
localparam integer CFG_CIN_LSB = 45, CFG_CIN_WIDTH = 9;
localparam integer CFG_COUT_LSB = 54, CFG_COUT_WIDTH = 9;
localparam integer CFG_STRIDE_LSB = 63, CFG_STRIDE_WIDTH = 2;
localparam integer CFG_DILATION_LSB = 65, CFG_DILATION_WIDTH = 2;
localparam integer CFG_PAD_LSB = 67, CFG_PAD_WIDTH = 2;
localparam integer CFG_SHIFT_LSB = 69, CFG_SHIFT_WIDTH = 6;
localparam integer CFG_WEIGHT_OFFSET_LSB = 75, CFG_WEIGHT_OFFSET_WIDTH = 32;
localparam integer CFG_PARAM_OFFSET_LSB = 107, CFG_PARAM_OFFSET_WIDTH = 18;
localparam integer CFG_DMA_BYTES_LSB = 125, CFG_DMA_BYTES_WIDTH = 20;
localparam integer CFG_STAGE_ID_LSB = 145, CFG_STAGE_ID_WIDTH = 4;
localparam integer CFG_RESERVED_LSB = 149, CFG_RESERVED_WIDTH = 107;

// TODO: Use synchronous active-low rst_n; preserve RAM data, reset control/valid state.
// TODO: Latch configuration, implement module-specific FSM and datapath from the prompt.
// TODO: Keep data/mask/tag/last stable while valid && !ready; count accepted work only.
// TODO: Implement the specified rounding, saturation and signedness only where applicable.
// TODO: Implement sticky fault and mode-specific completion; escalate unresolved contracts.
// TODO: Add self-checking Verilog-2001 testbench and the listed waveform scenarios.
// TODO: Meet the existing resource and latency budgets; report synthesis separately.

endmodule
