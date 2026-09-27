class cnn_dma_responder extends cnn_m_axil_responder;

    `uvm_component_utils(cnn_dma_responder)

    cnn_dma_stream_cfg cfg;

    // DMA BASE ADDRESS
    localparam bit [31:0] IMAGE_BASE = 32'h4040_0000;
    localparam bit [31:0] WEIGHT_BASE = 32'h4041_0000;
    localparam bit [31:0] FEATURE_BASE = 32'h4042_0000;

    // IMAGE DMA register model
    bit [31:0] image_cr;
    bit [31:0] image_sr;

    bit [31:0] image_curdesc;
    bit [31:0] image_taildesc;

    // WEIGHT DMA register model
    bit [31:0] weight_cr;
    bit [31:0] weight_sr;

    bit [31:0] weight_src_addr;
    bit [31:0] weight_length;

    // FEATURE DMA MM2S register model
    bit [31:0] feature_mm2s_cr;
    bit [31:0] feature_mm2s_sr;

    bit [31:0] feature_src_addr;
    bit [31:0] feature_src_length;

    // FEATURE DMA S2MM register model
    bit [31:0] feature_s2mm_cr;
    bit [31:0] feature_s2mm_sr;

    bit [31:0] feature_dst_addr;
    bit [31:0] feature_dst_length;

    // DMA completion state
    bit image_active;
    bit weight_active;
    bit feature_mm2s_active;
    bit feature_s2mm_active;

    int unsigned image_count;
    int unsigned weight_count;
    int unsigned feature_mm2s_count;
    int unsigned feature_s2mm_count;

    function new(string name = "cnn_dma_responder",
                 uvm_component parent = null);
        super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);
        super.build_phase(phase);
        if (!uvm_config_db#(cnn_dma_stream_cfg)::get(
                this, "", "dma_stream_cfg", cfg
            )) begin
            cfg = cnn_dma_stream_cfg::type_id::create("cfg");
            `uvm_warning(get_type_name(), "dma_stream_cfg not found !")
        end
    endfunction

    // DMA register reset
    function void reset_dma_model();
        image_cr            = 32'h0;
        image_sr            = 32'h0000_0001;

        image_curdesc       = 32'h0;
        image_taildesc      = 32'h0;

        weight_cr           = 32'h0;
        weight_sr           = 32'h0000_0001;

        weight_src_addr     = 32'h0;
        weight_length       = 32'h0;

        feature_mm2s_cr     = 32'h0;
        feature_mm2s_sr     = 32'h0000_0001;
        feature_src_addr    = 32'h0;
        feature_src_length  = 32'h0;

        feature_s2mm_cr     = 32'h0;
        feature_s2mm_sr     = 32'h0000_0001;
        feature_dst_addr    = 32'h0;
        feature_dst_length  = 32'h0;

        image_active        = 1'b0;
        weight_active       = 1'b0;
        feature_mm2s_active = 1'b0;
        feature_s2mm_active = 1'b0;

        image_count         = 0;
        weight_count        = 0;
        feature_mm2s_count  = 0;
        feature_s2mm_count  = 0;
    endfunction

    // DMA 시작 helper
    function void start_image_dma();
        image_active = 1'b1;

        image_sr[0] = 1'b0;  // Halted = 0
        image_sr[1] = 1'b0;  // Idle   = 0

        image_count =
            (cfg.dma_complete_latency == 0)
            ? 1
            : cfg.dma_complete_latency;
    endfunction

    function void start_weight_dma();
        weight_active = 1'b1;

        weight_sr[0] = 1'b0;
        weight_sr[1] = 1'b0;

        weight_count =
            (cfg.dma_complete_latency == 0)
            ? 1
            : cfg.dma_complete_latency;
    endfunction

    function void start_feature_mm2s_dma();
        feature_mm2s_active = 1'b1;

        feature_mm2s_sr[0] = 1'b0;
        feature_mm2s_sr[1] = 1'b0;

        feature_mm2s_count =
            (cfg.dma_complete_latency == 0)
            ? 1
            : cfg.dma_complete_latency;
    endfunction

    function void start_feature_s2mm_dma();
        feature_s2mm_active = 1'b1;

        feature_s2mm_sr[0] = 1'b0;
        feature_s2mm_sr[1] = 1'b0;

        feature_s2mm_count =
            (cfg.dma_complete_latency == 0)
            ? 1
            : cfg.dma_complete_latency;
    endfunction

    // 완료 model
    function void update_dma_completion();
        // IMAGE
        if (image_active) begin
            if (image_count <= 1) begin
                image_active = 1'b0;

                image_sr[1]  = 1'b1;  // Idle
                image_sr[12] = 1'b1;  // IOC

                image_count  = 0;
            end else begin
                image_count--;
            end
        end

        // WEIGHT
        if (weight_active) begin
            if (weight_count <= 1) begin
                weight_active = 1'b0;

                weight_sr[1]  = 1'b1;
                weight_sr[12] = 1'b1;

                weight_count  = 0;
            end else begin
                weight_count--;
            end
        end

        // FEATURE MM2S
        if (feature_mm2s_active) begin
            if (feature_mm2s_count <= 1) begin
                feature_mm2s_active = 1'b0;

                feature_mm2s_sr[1]  = 1'b1;
                feature_mm2s_sr[12] = 1'b1;

                feature_mm2s_count  = 0;
            end else begin
                feature_mm2s_count--;
            end
        end

        // FEATURE S2MM
        if (feature_s2mm_active) begin
            if (feature_s2mm_count <= 1) begin
                feature_s2mm_active = 1'b0;

                feature_s2mm_sr[1]  = 1'b1;
                feature_s2mm_sr[12] = 1'b1;

                feature_s2mm_count  = 0;
            end else begin
                feature_s2mm_count--;
            end
        end
    endfunction

    // Register read 함수
    function bit [31:0] dma_reg_read(bit [31:0] addr);
        case (addr)
            // IMAGE
            IMAGE_BASE + 32'h00: return image_cr;

            IMAGE_BASE + 32'h04: return image_sr;

            IMAGE_BASE + 32'h08: return image_curdesc;

            IMAGE_BASE + 32'h10: return image_taildesc;

            // WEIGHT
            WEIGHT_BASE + 32'h00: return weight_cr;

            WEIGHT_BASE + 32'h04: return weight_sr;

            WEIGHT_BASE + 32'h18: return weight_src_addr;

            WEIGHT_BASE + 32'h28: return weight_length;

            // FEATURE MM2S
            FEATURE_BASE + 32'h00: return feature_mm2s_cr;

            FEATURE_BASE + 32'h04: return feature_mm2s_sr;

            FEATURE_BASE + 32'h18: return feature_src_addr;

            FEATURE_BASE + 32'h28: return feature_src_length;

            // FEATURE S2MM
            FEATURE_BASE + 32'h30: return feature_s2mm_cr;

            FEATURE_BASE + 32'h34: return feature_s2mm_sr;

            FEATURE_BASE + 32'h48: return feature_dst_addr;

            FEATURE_BASE + 32'h58: return feature_dst_length;

            default: return 32'h0000_0000;
        endcase
    endfunction

    // Register write 함수
    function void dma_reg_write(bit [31:0] addr, bit [31:0] data);
        case (addr)
            // IMAGE DMA
            IMAGE_BASE + 32'h00: begin
                image_cr = data;
                if (data[0]) begin
                    image_sr[0] = 1'b0;
                end else begin
                    image_sr[0]  = 1'b1;

                    image_active = 1'b0;
                end
            end

            IMAGE_BASE + 32'h04: begin
                // IRQ bits W1C
                image_sr[14:12] = image_sr[14:12] & ~data[14:12];
            end

            IMAGE_BASE + 32'h08: image_curdesc = data;

            IMAGE_BASE + 32'h10: begin
                image_taildesc = data;
                if (image_cr[0]) start_image_dma();
            end

            // WEIGHT DMA
            WEIGHT_BASE + 32'h00: begin
                weight_cr = data;
                if (data[0]) weight_sr[0] = 1'b0;
                else begin
                    weight_sr[0]  = 1'b1;
                    weight_active = 1'b0;
                end
            end

            WEIGHT_BASE + 32'h04: begin
                weight_sr[14:12] = weight_sr[14:12] & ~data[14:12];
            end

            WEIGHT_BASE + 32'h18: weight_src_addr = data;

            WEIGHT_BASE + 32'h28: begin
                weight_length = data;
                if (weight_cr[0] && data != 0) start_weight_dma();
            end

            // FEATURE MM2S
            FEATURE_BASE + 32'h00: begin
                feature_mm2s_cr = data;
                if (data[0]) feature_mm2s_sr[0] = 1'b0;
                else begin
                    feature_mm2s_sr[0]  = 1'b1;
                    feature_mm2s_active = 1'b0;
                end
            end

            FEATURE_BASE + 32'h04: begin
                feature_mm2s_sr[14:12] = feature_mm2s_sr[14:12] & ~data[14:12];
            end

            FEATURE_BASE + 32'h18: feature_src_addr = data;

            FEATURE_BASE + 32'h28: begin
                feature_src_length = data;
                if (feature_mm2s_cr[0] && data != 0) start_feature_mm2s_dma();
            end

            // FEATURE S2MM
            FEATURE_BASE + 32'h30: begin
                feature_s2mm_cr = data;
                if (data[0]) feature_s2mm_sr[0] = 1'b0;
                else begin
                    feature_s2mm_sr[0]  = 1'b1;
                    feature_s2mm_active = 1'b0;
                end
            end

            FEATURE_BASE + 32'h34: begin
                feature_s2mm_sr[14:12] = feature_s2mm_sr[14:12] & ~data[14:12];
            end

            FEATURE_BASE + 32'h48: feature_dst_addr = data;

            FEATURE_BASE + 32'h58: begin
                feature_dst_length = data;
                if (feature_s2mm_cr[0] && data != 0) start_feature_s2mm_dma();
            end

            default: begin
            end
        endcase
    endfunction

    task run_phase(uvm_phase phase);

        bit aw_seen;
        bit w_seen;

        bit [31:0] awaddr_q;
        bit [31:0] wdata_q;

        vif.m_axil_awready = 1'b0;
        vif.m_axil_wready  = 1'b0;

        vif.m_axil_bresp   = 2'b00;
        vif.m_axil_bvalid  = 1'b0;

        vif.m_axil_arready = 1'b0;

        vif.m_axil_rdata   = 32'h0000_0000;
        vif.m_axil_rresp   = 2'b00;
        vif.m_axil_rvalid  = 1'b0;

        reset_dma_model();

        wait (vif.rst_n === 1'b1);

        aw_seen = 1'b0;
        w_seen  = 1'b0;

        forever begin
            @(vif.m_axil_rsp_cb);

            // DMA completion model
            update_dma_completion();

            // WRITE ADDRESS / DATA READY
            vif.m_axil_rsp_cb.m_axil_awready <= !aw_seen && !vif.m_axil_bvalid;

            vif.m_axil_rsp_cb.m_axil_wready  <= !w_seen && !vif.m_axil_bvalid;

            // AW
            if (vif.m_axil_rsp_cb.m_axil_awvalid && vif.m_axil_awready) begin
                aw_seen  = 1'b1;
                awaddr_q = vif.m_axil_rsp_cb.m_axil_awaddr;
            end

            // W
            if (vif.m_axil_rsp_cb.m_axil_wvalid && vif.m_axil_wready) begin
                w_seen  = 1'b1;
                wdata_q = vif.m_axil_rsp_cb.m_axil_wdata;
            end

            // AW + W complete
            if (aw_seen && w_seen && !vif.m_axil_bvalid) begin
                dma_reg_write(awaddr_q, wdata_q);
                vif.m_axil_rsp_cb.m_axil_bresp  <= 2'b00;
                vif.m_axil_rsp_cb.m_axil_bvalid <= 1'b1;
            end

            // B response complete
            if (vif.m_axil_bvalid && vif.m_axil_rsp_cb.m_axil_bready) begin
                vif.m_axil_rsp_cb.m_axil_bvalid <= 1'b0;

                aw_seen = 1'b0;
                w_seen  = 1'b0;
            end

            // READ
            vif.m_axil_rsp_cb.m_axil_arready <= !vif.m_axil_rvalid;
            if (vif.m_axil_rsp_cb.m_axil_arvalid && vif.m_axil_arready) begin
                vif.m_axil_rsp_cb.m_axil_rdata <= dma_reg_read(
                    vif.m_axil_rsp_cb.m_axil_araddr
                );

                vif.m_axil_rsp_cb.m_axil_rresp <= 2'b00;

                vif.m_axil_rsp_cb.m_axil_rvalid <= 1'b1;
            end
            if (vif.m_axil_rvalid && vif.m_axil_rsp_cb.m_axil_rready) begin
                vif.m_axil_rsp_cb.m_axil_rvalid <= 1'b0;
            end
        end
    endtask

endclass
