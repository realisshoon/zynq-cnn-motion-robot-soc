class cnn_dma_reference_model extends uvm_object;
    `uvm_object_utils(cnn_dma_reference_model)

    // DDR Base Address
    localparam bit [31:0] WGT_BASE = 32'h1000_0000;
    localparam bit [31:0] FM_A_BASE = 32'h1100_0000;
    localparam bit [31:0] FM_B_BASE = 32'h1110_0000;
    localparam bit [31:0] SG_BASE = 32'h1120_0000;

    // DMA Base Address
    localparam bit [31:0] IMAGE_DMA_BASE = 32'h4040_0000;
    localparam bit [31:0] WEIGHT_DMA_BASE = 32'h4041_0000;
    localparam bit [31:0] FEATURE_DMA_BASE = 32'h4042_0000;

    function new(string name = "cnn_dma_reference_model");
        super.new(name);
    endfunction

    // Derived from checked-in layer_param_rom.v descriptor fields [106:75]
    // and [144:125], operations 0..28. Zero payload meets signed-bias and
    // reserved-bit constraints; numerical CNN inference is outside this plan.
    function bit [31:0] get_expected_weight_src(int op);
        case(op)
            0: return WGT_BASE + 32'h00000000;
            1: return WGT_BASE + 32'h000003c0;
            2: return WGT_BASE + 32'h000005c0;
            3: return WGT_BASE + 32'h00000d40;
            4: return WGT_BASE + 32'h00001100;
            5: return WGT_BASE + 32'h00002c00;
            6: return WGT_BASE + 32'h00003280;
            7: return WGT_BASE + 32'h00005980;
            8: return WGT_BASE + 32'h00006000;
            9: return WGT_BASE + 32'h0000ae00;
            10: return WGT_BASE + 32'h0000bac0;
            11: return WGT_BASE + 32'h000150c0;
            12: return WGT_BASE + 32'h00015d80;
            13: return WGT_BASE + 32'h00028980;
            14: return WGT_BASE + 32'h0002a300;
            15: return WGT_BASE + 32'h0004ef00;
            16: return WGT_BASE + 32'h00050880;
            17: return WGT_BASE + 32'h00075480;
            18: return WGT_BASE + 32'h00076e00;
            19: return WGT_BASE + 32'h0009ba00;
            20: return WGT_BASE + 32'h0009d380;
            21: return WGT_BASE + 32'h000c1f80;
            22: return WGT_BASE + 32'h000c3900;
            23: return WGT_BASE + 32'h000e8500;
            24: return WGT_BASE + 32'h000e9e80;
            25: return WGT_BASE + 32'h0010ea80;
            26: return WGT_BASE + 32'h00110400;
            27: return WGT_BASE + 32'h00135000;
            28: return WGT_BASE + 32'h00136ec0;
            default: return 0;
        endcase
    endfunction
    function int unsigned get_expected_weight_bytes(int op);
        case(op)
            0: return 960;
            1: return 512;
            2: return 1920;
            3: return 960;
            4: return 6912;
            5: return 1664;
            6: return 9984;
            7: return 1664;
            8: return 19968;
            9: return 3264;
            10: return 38400;
            11: return 3264;
            12: return 76800;
            13: return 6528;
            14: return 150528;
            15: return 6528;
            16: return 150528;
            17: return 6528;
            18: return 150528;
            19: return 6528;
            20: return 150528;
            21: return 6528;
            22: return 150528;
            23: return 6528;
            24: return 150528;
            25: return 6528;
            26: return 150528;
            27: return 7872;
            28: return 14144;
            default: return 0;
        endcase
    endfunction

    // expected feature map source address
    function bit [31:0] get_expected_fm_src(int unsigned stage);
        if ((stage >= 1) && (stage <= 13)) begin
            // odd stage (FM_A -> FM_B)
            if (stage[0]) begin
                return FM_A_BASE;
            end else begin
                // even stage (FM_B -> FM_A)
                return FM_B_BASE;
            end
        end else if ((stage == 14) || (stage == 15)) begin
            return FM_B_BASE;
        end else begin
            // stage0
            return 32'h0000_0000;
        end
    endfunction

    // expected feature map destination address
    function bit [31:0] get_expected_fm_dst(int unsigned stage);
        // stage0 conv0 output
        if (stage == 0) begin
            return FM_A_BASE;
        end else if ((stage >= 1) && (stage <= 13)) begin
            // odd stage (FM_A -> FM_B)
            if (stage[0]) return FM_B_BASE;
            else  // even stage (FM_B -> FM_A)\
                return FM_A_BASE;
        end else begin
            return 32'h0000_0000;
        end
    endfunction

endclass
