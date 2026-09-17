// cnn_common_params.vh
// CNN-v4.0 common constants shared by the CNN accelerator RTL.
// Keep this file synchronized with the Notion "공용 설계 규칙" page,
// the module port-spec XLSX, layer_param_rom contents, and the Python model.
//
// Verilog-2001 include file only. Do not use SystemVerilog syntax here.

`ifndef CNN_COMMON_PARAMS_VH
`define CNN_COMMON_PARAMS_VH

// ---------------------------------------------------------------------------
// Version / topology
// ---------------------------------------------------------------------------
`define CNN_VERSION              32'h00040000
`define CNN_OPS                  29
`define CNN_STAGES               16
`define CNN_NUM_LAYERS           16
`define CNN_NUM_JOINTS           17
`define CNN_INPUT_SIZE           256
`define CNN_INPUT_W              256
`define CNN_INPUT_H              256
`define CNN_OUTPUT_GRID          32
`define CNN_FRAME_W              1280
`define CNN_FRAME_H              720

// ---------------------------------------------------------------------------
// Datapath widths
// ---------------------------------------------------------------------------
`define CNN_DATA_WIDTH           8
`define CNN_WEIGHT_WIDTH         8
`define CNN_BODY_WIDTH           8
`define CNN_HEAT_WIDTH           8
`define CNN_OFFSET_WIDTH         16
`define CNN_ACCUM_WIDTH          24
`define CNN_CONV0_ACCUM_WIDTH    25
`define CNN_REQUANT_IN_WIDTH     25
`define CNN_M_WIDTH              18
`define CNN_SHIFT                16
`define CNN_SHIFT_BITS           16
`define CNN_PRODUCT_WIDTH        43
`define CNN_BIAS_ACCUM_WIDTH     24

// Parallelism.
`define CNN_W_IN                 32
`define CNN_W_OUT                4
`define CNN_TAG_WIDTH            64
`define CNN_CFG_DESC_WIDTH       256

// Saturation ranges.
`define CNN_BODY_MIN             0
`define CNN_BODY_MAX             127
`define CNN_HEAT_MIN             -128
`define CNN_HEAT_MAX             127
`define CNN_OFFSET_MIN           -32768
`define CNN_OFFSET_MAX           32767
`define CNN_M_MAX                131071

// Operation kind.
`define CNN_KIND_CONV0           2'd0
`define CNN_KIND_DEPTHWISE       2'd1
`define CNN_KIND_POINTWISE       2'd2

// Tensor mode.
`define CNN_MODE_BODY            2'd0
`define CNN_MODE_HEAT            2'd1
`define CNN_MODE_OFFSET          2'd2

// ---------------------------------------------------------------------------
// cfg_desc256 bit layout
// ---------------------------------------------------------------------------
`define CFG_OP_ID_LSB            0
`define CFG_OP_ID_WIDTH          5
`define CFG_KIND_LSB             5
`define CFG_KIND_WIDTH           2
`define CFG_MODE_LSB             7
`define CFG_MODE_WIDTH           2
`define CFG_HIN_LSB              9
`define CFG_HIN_WIDTH            9
`define CFG_WIN_LSB              18
`define CFG_WIN_WIDTH            9
`define CFG_HOUT_LSB             27
`define CFG_HOUT_WIDTH           9
`define CFG_WOUT_LSB             36
`define CFG_WOUT_WIDTH           9
`define CFG_CIN_LSB              45
`define CFG_CIN_WIDTH            9
`define CFG_COUT_LSB             54
`define CFG_COUT_WIDTH           9
`define CFG_STRIDE_LSB           63
`define CFG_STRIDE_WIDTH         2
`define CFG_DILATION_LSB         65
`define CFG_DILATION_WIDTH       2
`define CFG_PAD_LSB              67
`define CFG_PAD_WIDTH            2
`define CFG_SHIFT_LSB            69
`define CFG_SHIFT_WIDTH          6
`define CFG_WEIGHT_OFFSET_LSB    75
`define CFG_WEIGHT_OFFSET_WIDTH  32
`define CFG_PARAM_OFFSET_LSB     107
`define CFG_PARAM_OFFSET_WIDTH   18
`define CFG_DMA_BYTES_LSB        125
`define CFG_DMA_BYTES_WIDTH      20
`define CFG_STAGE_ID_LSB         145
`define CFG_STAGE_ID_WIDTH       4
`define CFG_RESERVED_LSB         149
`define CFG_RESERVED_WIDTH       107

`define CFG_OP_ID_MSB            4
`define CFG_KIND_MSB             6
`define CFG_MODE_MSB             8
`define CFG_HIN_MSB              17
`define CFG_WIN_MSB              26
`define CFG_HOUT_MSB             35
`define CFG_WOUT_MSB             44
`define CFG_CIN_MSB              53
`define CFG_COUT_MSB             62
`define CFG_STRIDE_MSB           64
`define CFG_DILATION_MSB         66
`define CFG_PAD_MSB              68
`define CFG_SHIFT_MSB            74
`define CFG_WEIGHT_OFFSET_MSB    106
`define CFG_PARAM_OFFSET_MSB     124
`define CFG_DMA_BYTES_MSB        144
`define CFG_STAGE_ID_MSB         148
`define CFG_RESERVED_MSB         255

// ---------------------------------------------------------------------------
// TAG64 bit layout
// ---------------------------------------------------------------------------
`define TAG_COL_LSB              0
`define TAG_COL_WIDTH            8
`define TAG_ROW_LSB              8
`define TAG_ROW_WIDTH            8
`define TAG_BATCH_LSB            16
`define TAG_BATCH_WIDTH          4
`define TAG_GROUP_LSB            20
`define TAG_GROUP_WIDTH          7
`define TAG_TAP_LSB              27
`define TAG_TAP_WIDTH            4
`define TAG_BATCH_LAST_BIT       31
`define TAG_PIXEL_LAST_BIT       32
`define TAG_OP_ID_LSB            33
`define TAG_OP_ID_WIDTH          5
`define TAG_FRAME_END_BIT        38
`define TAG_GROUP_LAST_BIT       39
`define TAG_RESERVED_LSB         40
`define TAG_RESERVED_WIDTH       24

`define TAG_COL_MSB              7
`define TAG_ROW_MSB              15
`define TAG_BATCH_MSB            19
`define TAG_GROUP_MSB            26
`define TAG_TAP_MSB              30
`define TAG_OP_ID_MSB            37
`define TAG_RESERVED_MSB         63

// ---------------------------------------------------------------------------
// Geometry / post-processing constants
// ---------------------------------------------------------------------------
`define CNN_GRID_STRIDE_PIX      5
`define CNN_PAD_TOP_PIX          280
`define CNN_PAD_LEFT_PIX         0
`define CNN_OFFSET_SCALE_M       3096
`define CNN_COORD_SCALE_M        15480
`define CNN_COORD_SHIFT          16
`define CNN_DEFAULT_THRESHOLD    -46

// Offset head channel layout: y offset channels 0..16, x offset channels 17..33.
`define CNN_OFFSET_Y_BASE_CH     0
`define CNN_OFFSET_X_BASE_CH     17

// ---------------------------------------------------------------------------
// Memory map / DMA constants
// ---------------------------------------------------------------------------
`define CNN_WGT_BASE_DEFAULT     32'h10000000
`define CNN_FM_A_BASE_DEFAULT    32'h11000000
`define CNN_FM_B_BASE_DEFAULT    32'h11100000
`define CNN_SG_BASE_DEFAULT      32'h11200000
`define CNN_FRAME_BASE_DEFAULT   32'h0A000000
`define CNN_FM_REGION_BYTES      21'd1048576
`define CNN_FM_PAYLOAD_MAX       20'd786432
`define CNN_IMAGE_SG_ROWS        144
`define CNN_IMAGE_ROW_BYTES      3840
`define CNN_IMAGE_SG_BYTES       552960
`define CNN_AXI_ADDR_ALIGN       64
`define CNN_DMA_DATA_WIDTH       64
`define CNN_DMA_KEEP_WIDTH       8

// ---------------------------------------------------------------------------
// Resource-budget contract for Zybo Z7-20.
// These are design caps and must be checked with synthesis reports.
// ---------------------------------------------------------------------------
`define CNN_DSP_CONV0            28
`define CNN_DSP_DEPTHWISE        33
`define CNN_DSP_POINTWISE        132
`define CNN_DSP_ARITH_TOTAL      193
`define CNN_DSP_HARD_CAP         196
`define CNN_DSP_EXTERNAL_BUDGET  16
`define CNN_DSP_RESERVE          8
`define CNN_DSP_DEVICE_CAP       220
`define CNN_BRAM36_ESTIMATE      79
`define CNN_BRAM36_HARD_CAP      90
`define CNN_BRAM36_EXTERNAL      40
`define CNN_BRAM36_DEVICE_CAP    140
`define CNN_LUT_CNN_BUDGET       32000
`define CNN_LUT_EXTERNAL_BUDGET  16000
`define CNN_LUT_RESERVE          5200
`define CNN_LUT_DEVICE_BUDGET    53200

// ---------------------------------------------------------------------------
// Layer operation IDs.
// Stride=2 operations are OP_CONV0, OP_CONV2_DW, OP_CONV4_DW, OP_CONV6_DW.
// All other depthwise/pointwise ops use stride=1.
// ---------------------------------------------------------------------------
`define CNN_OP_CONV0             5'd0
`define CNN_OP_CONV1_DW          5'd1
`define CNN_OP_CONV1_PW          5'd2
`define CNN_OP_CONV2_DW          5'd3
`define CNN_OP_CONV2_PW          5'd4
`define CNN_OP_CONV3_DW          5'd5
`define CNN_OP_CONV3_PW          5'd6
`define CNN_OP_CONV4_DW          5'd7
`define CNN_OP_CONV4_PW          5'd8
`define CNN_OP_CONV5_DW          5'd9
`define CNN_OP_CONV5_PW          5'd10
`define CNN_OP_CONV6_DW          5'd11
`define CNN_OP_CONV6_PW          5'd12
`define CNN_OP_CONV7_DW          5'd13
`define CNN_OP_CONV7_PW          5'd14
`define CNN_OP_CONV8_DW          5'd15
`define CNN_OP_CONV8_PW          5'd16
`define CNN_OP_CONV9_DW          5'd17
`define CNN_OP_CONV9_PW          5'd18
`define CNN_OP_CONV10_DW         5'd19
`define CNN_OP_CONV10_PW         5'd20
`define CNN_OP_CONV11_DW         5'd21
`define CNN_OP_CONV11_PW         5'd22
`define CNN_OP_CONV12_DW         5'd23
`define CNN_OP_CONV12_PW         5'd24
`define CNN_OP_CONV13_DW         5'd25
`define CNN_OP_CONV13_PW         5'd26
`define CNN_OP_HEATMAP           5'd27
`define CNN_OP_OFFSET            5'd28

// ---------------------------------------------------------------------------
// Register map offsets.
// ---------------------------------------------------------------------------
`define CNN_REG_CTRL             8'h00
`define CNN_REG_STATUS           8'h04
`define CNN_REG_STRIDE           8'h0C
`define CNN_REG_PAD_TOP          8'h10
`define CNN_REG_PAD_LEFT         8'h14
`define CNN_REG_FRAME_ID         8'h70
`define CNN_REG_RESULT_SEQ       8'h74
`define CNN_REG_ERROR_STATUS     8'h78
`define CNN_REG_IRQ_ENABLE       8'h7C
`define CNN_REG_CYCLE_COUNT      8'h80
`define CNN_REG_RESULT_FRAME_ID  8'h84
`define CNN_REG_MIN_COUNT        8'h88
`define CNN_REG_WGT_BASE         8'h8C
`define CNN_REG_FM_A_BASE        8'h90
`define CNN_REG_FM_B_BASE        8'h94
`define CNN_REG_SG_DESC_BASE     8'h98
`define CNN_REG_FRAME_BASE       8'h9C
`define CNN_REG_TIMEOUT_CYCLES   8'hA0
`define CNN_REG_VERSION          8'hA4
`define CNN_REG_CAPS             8'hA8
`define CNN_REG_MODEL_TAG        8'hAC

`endif
