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
