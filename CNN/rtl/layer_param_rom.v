`timescale 1ns / 1ps

// CNN-v4.0 | Verilog-2001 skeleton | technical contract unchanged
module layer_param_rom (
    input  wire         clk,
    input  wire         rst_n,
    output wire         fault,
    input  wire         req_valid,
    output wire         req_ready,
    input  wire [  4:0] req_op,
    output wire         rsp_valid,
    input  wire         rsp_ready,
    output wire [255:0] rsp_desc
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

    reg [255:0] rsp_desc_reg;
    reg [255:0] rom_data;
    reg         rsp_valid_reg;
    reg         fault_reg;

    assign rsp_desc  = rsp_desc_reg;
    assign rsp_valid = rsp_valid_reg;
    assign fault     = fault_reg;

    assign req_ready = rst_n && !rsp_valid_reg && !fault_reg;

    always @(posedge clk) begin
        if (!rst_n) begin
            rsp_desc_reg  <= 256'd0;
            rsp_valid_reg <= 1'b0;
            fault_reg     <= 1'b0;
        end else begin
            // 응답 전달 완료
            if (rsp_valid && rsp_ready) begin
                rsp_valid_reg <= 1'b0;
            end
            // 새로운 요청 수락
            if (req_valid && req_ready) begin
                if (req_op < 5'd29) begin
                    rsp_desc_reg  <= rom_data;
                    rsp_valid_reg <= 1'b1;
                end else begin
                    fault_reg <= 1'b1;
                end
            end
        end
    end

    always @(*) begin
        case (req_op)
            5'd0:
            rom_data = 256'h00000000000000000000000000000078001800000000022b0600680404020000; // Conv0
            5'd1:
            rom_data = 256'h00000000000000000000000000020040000a0000001e020a8603080402010021; // Conv1 DW
            5'd2:
            rom_data = 256'h000000000000000000000000000200f000300000002e02028c03080402010042; // Conv1 PW
            5'd3:
            rom_data = 256'h0000000000000000000000000004007800120000006a020b0c06040202010023; // Conv2 DW
            5'd4:
            rom_data = 256'h0000000000000000000000000004036000c00000008802029806040201008044; // Conv2 PW
            5'd5:
            rom_data = 256'h000000000000000000000000000600d0001c00000160020a980c040201008025; // Conv3 DW
            5'd6:
            rom_data = 256'h000000000000000000000000000604e00120000001940202980c040201008046; // Conv3 PW
            5'd7:
            rom_data = 256'h000000000000000000000000000800d0001c000002cc020b180c020101008027; // Conv4 DW
            5'd8:
            rom_data = 256'h000000000000000000000000000809c00240000003000202b00c020100804048; // Conv4 PW
            5'd9:
            rom_data = 256'h000000000000000000000000000a0198003600000570020ab018020100804029; // Conv5 DW
            5'd10:
            rom_data = 256'h000000000000000000000000000a12c00480000005d60202b01802010080404a; // Conv5 PW
            5'd11:
            rom_data = 256'h000000000000000000000000000c0198003600000a86020b301801008080402b; // Conv6 DW
            5'd12:
            rom_data = 256'h000000000000000000000000000c2580090000000aec0202e01801008040204c; // Conv6 PW
            5'd13:
            rom_data = 256'h000000000000000000000000000e0330006c0000144c020ae03001008040202d; // Conv7 DW
            5'd14:
            rom_data = 256'h000000000000000000000000000e49801200000015180202e03001008040204e; // Conv7 PW
            5'd15:
            rom_data = 256'h00000000000000000000000000100330006c00002778020ae03001008040202f; // Conv8 DW
            5'd16:
            rom_data = 256'h000000000000000000000000001049801200000028440202e030010080402050; // Conv8 PW
            5'd17:
            rom_data = 256'h00000000000000000000000000120330006c00003aa4020ae030010080402031; // Conv9 DW
            5'd18:
            rom_data = 256'h00000000000000000000000000124980120000003b700202e030010080402052; // Conv9 PW
            5'd19:
            rom_data = 256'h00000000000000000000000000140330006c00004dd0020ae030010080402033; // Conv10 DW
            5'd20:
            rom_data = 256'h00000000000000000000000000144980120000004e9c0202e030010080402054; // Conv10 PW
            5'd21:
            rom_data = 256'h00000000000000000000000000160330006c000060fc020ae030010080402035; // Conv11 DW
            5'd22:
            rom_data = 256'h000000000000000000000000001649801200000061c80202e030010080402056; // Conv11 PW
            5'd23:
            rom_data = 256'h00000000000000000000000000180330006c00007428020ae030010080402037; // Conv12 DW
            5'd24:
            rom_data = 256'h000000000000000000000000001849801200000074f40202e030010080402058; // Conv12 PW
            5'd25:
            rom_data = 256'h000000000000000000000000001a0330006c00008754020ae030010080402039; // Conv13 DW
            5'd26:
            rom_data = 256'h000000000000000000000000001a49801200000088200202e03001008040205a; // Conv13 PW
            5'd27:
            rom_data = 256'h000000000000000000000000001c03d800f000009a80020284700100804020db; // Heatmap
            5'd28:
            rom_data = 256'h000000000000000000000000001e06e801b000009b76020288b001008040215c; // Offset
            default: rom_data = 256'd0;
        endcase
    end

endmodule
