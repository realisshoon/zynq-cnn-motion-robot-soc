// CNN-v4.0 | Verilog-2001 skeleton | technical contract unchanged
module feature_map_io (
    input wire clk,
    input wire rst_n,
    input wire cfg_valid,
    output wire cfg_ready,
    input wire [255:0] cfg_desc,
    output wire done,
    output wire fault,
    input wire [63:0] s_body_data,
    input wire s_body_valid,
    output wire s_body_ready,
    input wire [3:0] s_body_mask,
    input wire [63:0] s_body_tag,
    output wire [255:0] m_pixel_data,
    output wire m_pixel_valid,
    input wire m_pixel_ready,
    output wire [31:0] m_pixel_mask,
    output wire [63:0] m_pixel_tag,
    input wire [31:0] src_addr,
    input wire [31:0] dst_addr,
    input wire [19:0] src_bytes,
    input wire [19:0] dst_bytes,
    input wire read_en,
    input wire write_en,
    input wire read_dma_done,
    input wire write_dma_done,
    input wire dma_error,
    output wire read_done,
    output wire write_done,
    input wire [63:0] s_dma_read_data,
    input wire s_dma_read_valid,
    output wire s_dma_read_ready,
    input wire s_dma_read_last,
    input wire [7:0] s_dma_read_keep,
    output wire [63:0] m_dma_write_data,
    output wire m_dma_write_valid,
    input wire m_dma_write_ready,
    output wire m_dma_write_last,
    output wire [7:0] m_dma_write_keep
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
