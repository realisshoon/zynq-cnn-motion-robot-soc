class cnn_golden_dma_responder extends cnn_m_axil_responder;

    `uvm_component_utils(cnn_golden_dma_responder)

    // ---------------------------------------------------------
    // DMA register base addresses
    // ---------------------------------------------------------
    localparam bit [31:0] IMAGE_BASE = 32'h4040_0000;
    localparam bit [31:0] WEIGHT_BASE = 32'h4041_0000;
    localparam bit [31:0] FEATURE_BASE = 32'h4042_0000;

    localparam int IMAGE_TOTAL_BEATS = 69120;


    // ---------------------------------------------------------
    // Simplified DMA status model
    // ---------------------------------------------------------
    bit weight_idle;

    bit image_halted;
    bit image_idle;

    bit feature_mm2s_idle;
    bit feature_s2mm_idle;


    // Stream progress
    bit weight_in_packet;
    bit weight_stream_done;

    bit image_in_frame;
    int unsigned image_beat_count;

    bit feature_read_done;
    bit feature_write_done;


    function new(string name = "cnn_golden_dma_responder", uvm_component parent = null);
        super.new(name, parent);
    endfunction


    // =========================================================
    // DMA status read
    // =========================================================
    function automatic bit [31:0] get_read_data(bit [31:0] addr);

        bit [31:0] data;

        data = 32'h0000_0000;

        case (addr)

            // -------------------------------------------------
            // IMAGE MM2S DMASR
            //
            // bit[0] = Halted
            // bit[1] = Idle
            // -------------------------------------------------
            IMAGE_BASE + 32'h04: begin
                data[0] = image_halted;
                data[1] = image_idle;
            end


            // -------------------------------------------------
            // WEIGHT MM2S DMASR
            // -------------------------------------------------
            WEIGHT_BASE + 32'h04: begin
                data[1] = weight_idle;
            end


            // -------------------------------------------------
            // FEATURE MM2S DMASR
            // DDR -> CNN
            // -------------------------------------------------
            FEATURE_BASE + 32'h04: begin
                data[1] = feature_mm2s_idle;
            end


            // -------------------------------------------------
            // FEATURE S2MM DMASR
            // CNN -> DDR
            // -------------------------------------------------
            FEATURE_BASE + 32'h34: begin
                data[1] = feature_s2mm_idle;
            end


            default: begin
                data = 32'h0000_0000;
            end

        endcase

        return data;

    endfunction


    // =========================================================
    // Handle DMA register writes
    // =========================================================
    task automatic handle_write(bit [31:0] addr, bit [31:0] data);

        case (addr)

            // -------------------------------------------------
            // IMAGE MM2S control
            // -------------------------------------------------
            IMAGE_BASE + 32'h00: begin

                if (data[0]) begin

                    image_halted = 1'b0;
                    image_idle   = 1'b0;

                    `uvm_info(get_type_name(), "IMAGE DMA started", UVM_MEDIUM)

                end else begin

                    image_halted = 1'b1;

                    `uvm_info(get_type_name(), "IMAGE DMA halted", UVM_MEDIUM)

                end

            end


            // -------------------------------------------------
            // WEIGHT MM2S control
            // -------------------------------------------------
            WEIGHT_BASE + 32'h00: begin

                if (data[0]) begin

                    // Weight stream may already have started because
                    // s_weight_ready belongs to the RTL load FSM.
                    if (!weight_stream_done) weight_idle = 1'b0;

                    `uvm_info(get_type_name(), "WEIGHT DMA started", UVM_MEDIUM)

                end

            end


            // -------------------------------------------------
            // FEATURE MM2S control
            // DDR -> CNN
            // -------------------------------------------------
            FEATURE_BASE + 32'h00: begin

                if (data[0]) begin
                    feature_mm2s_idle = 1'b0;
                    feature_read_done = 1'b0;
                end

            end


            // -------------------------------------------------
            // FEATURE S2MM control
            // CNN -> DDR
            // -------------------------------------------------
            FEATURE_BASE + 32'h30: begin

                if (data[0]) begin
                    feature_s2mm_idle  = 1'b0;
                    feature_write_done = 1'b0;
                end

            end

            default: begin
            end

        endcase

    endtask


    // =========================================================
    // AXI-Lite responder
    // =========================================================
    task run_axil();

        bit aw_seen;
        bit w_seen;

        bit [31:0] awaddr_hold;
        bit [31:0] wdata_hold;

        aw_seen = 0;
        w_seen  = 0;

        forever begin

            @(vif.m_axil_rsp_cb);


            // =====================================================
            // WRITE ADDRESS / WRITE DATA READY
            //
            // m_axil_*ready / bvalid는 clocking block OUTPUT이므로
            // RHS에서 cb를 통해 sample하지 않는다.
            // 실제 interface signal을 직접 참조한다.
            // =====================================================

            vif.m_axil_rsp_cb.m_axil_awready <= !aw_seen && !vif.m_axil_bvalid;

            vif.m_axil_rsp_cb.m_axil_wready  <= !w_seen && !vif.m_axil_bvalid;


            // -----------------------------------------------------
            // AW handshake
            // -----------------------------------------------------
            if (vif.m_axil_rsp_cb.m_axil_awvalid && vif.m_axil_awready) begin

                aw_seen = 1;

                awaddr_hold = vif.m_axil_rsp_cb.m_axil_awaddr;

            end


            // -----------------------------------------------------
            // W handshake
            // -----------------------------------------------------
            if (vif.m_axil_rsp_cb.m_axil_wvalid && vif.m_axil_wready) begin

                w_seen = 1;

                wdata_hold = vif.m_axil_rsp_cb.m_axil_wdata;

            end


            // -----------------------------------------------------
            // AW + W 모두 수신
            // -----------------------------------------------------
            if (aw_seen && w_seen && !vif.m_axil_bvalid) begin

                handle_write(awaddr_hold, wdata_hold);

                vif.m_axil_rsp_cb.m_axil_bresp  <= 2'b00;

                vif.m_axil_rsp_cb.m_axil_bvalid <= 1'b1;

            end


            // -----------------------------------------------------
            // B handshake
            // -----------------------------------------------------
            if (vif.m_axil_bvalid && vif.m_axil_rsp_cb.m_axil_bready) begin

                vif.m_axil_rsp_cb.m_axil_bvalid <= 1'b0;

                aw_seen = 0;
                w_seen  = 0;

            end


            // =====================================================
            // READ
            // =====================================================

            vif.m_axil_rsp_cb.m_axil_arready <= !vif.m_axil_rvalid;


            // -----------------------------------------------------
            // AR handshake
            // -----------------------------------------------------
            if (vif.m_axil_rsp_cb.m_axil_arvalid && vif.m_axil_arready) begin

                vif.m_axil_rsp_cb.m_axil_rdata  <= get_read_data(vif.m_axil_rsp_cb.m_axil_araddr);

                vif.m_axil_rsp_cb.m_axil_rresp  <= 2'b00;

                vif.m_axil_rsp_cb.m_axil_rvalid <= 1'b1;

            end


            // -----------------------------------------------------
            // R handshake
            // -----------------------------------------------------
            if (vif.m_axil_rvalid && vif.m_axil_rsp_cb.m_axil_rready) begin

                vif.m_axil_rsp_cb.m_axil_rvalid <= 1'b0;

            end

        end

    endtask
    // =========================================================
    // Watch real stream handshakes
    //
    // 여기서 실제 Stream이 끝났는지 확인해서
    // DMA STATUS를 갱신한다.
    // =========================================================
    task watch_streams();

        forever begin

            @(vif.mon_cb);


            // -------------------------------------------------
            // WEIGHT
            // -------------------------------------------------
            if (vif.mon_cb.s_weight_valid && vif.mon_cb.s_weight_ready) begin

                if (!weight_in_packet) begin

                    weight_in_packet   = 1'b1;
                    weight_stream_done = 1'b0;
                    weight_idle        = 1'b0;

                end


                if (vif.mon_cb.s_weight_last) begin

                    weight_in_packet   = 1'b0;
                    weight_stream_done = 1'b1;
                    weight_idle        = 1'b1;

                    `uvm_info(get_type_name(), "WEIGHT stream completed", UVM_MEDIUM)

                end

            end


            // -------------------------------------------------
            // IMAGE
            // -------------------------------------------------
            if (vif.mon_cb.s_image_valid && vif.mon_cb.s_image_ready) begin

                if (!image_in_frame) begin

                    image_in_frame   = 1'b1;
                    image_beat_count = 0;
                    image_idle       = 1'b0;

                end


                image_beat_count++;


                if (image_beat_count == IMAGE_TOTAL_BEATS) begin

                    image_in_frame = 1'b0;
                    image_idle     = 1'b1;

                    `uvm_info(get_type_name(), $sformatf("IMAGE stream completed: %0d beats",
                                                         image_beat_count), UVM_MEDIUM)

                end

            end


            // -------------------------------------------------
            // FEATURE MM2S
            // TB -> DUT
            // -------------------------------------------------
            if (
                vif.mon_cb.s_feature_valid &&
                vif.mon_cb.s_feature_ready &&
                vif.mon_cb.s_feature_last
            ) begin

                feature_read_done = 1'b1;
                feature_mm2s_idle = 1'b1;

            end


            // -------------------------------------------------
            // FEATURE S2MM
            // DUT -> TB
            // -------------------------------------------------
            if (
                vif.mon_cb.m_feature_valid &&
                vif.mon_cb.m_feature_ready &&
                vif.mon_cb.m_feature_last
            ) begin

                feature_write_done = 1'b1;
                feature_s2mm_idle  = 1'b1;

                `uvm_info(get_type_name(), "FEATURE S2MM stream completed", UVM_MEDIUM)

            end

        end

    endtask


    // =========================================================
    // Main
    // =========================================================
    task run_phase(uvm_phase phase);

        // Interface outputs
        vif.m_axil_awready = 1'b0;
        vif.m_axil_wready = 1'b0;
        vif.m_axil_bresp = 2'b00;
        vif.m_axil_bvalid = 1'b0;

        vif.m_axil_arready = 1'b0;
        vif.m_axil_rdata = 32'h0000_0000;
        vif.m_axil_rresp = 2'b00;
        vif.m_axil_rvalid = 1'b0;


        // DMA reset state
        weight_idle = 1'b1;

        image_halted = 1'b1;
        image_idle = 1'b1;

        feature_mm2s_idle = 1'b1;
        feature_s2mm_idle = 1'b1;

        weight_in_packet = 1'b0;
        weight_stream_done = 1'b0;

        image_in_frame = 1'b0;
        image_beat_count = 0;

        feature_read_done = 1'b0;
        feature_write_done = 1'b0;


        wait (vif.rst_n === 1'b1);


        fork

            run_axil();

            watch_streams();

        join

    endtask

endclass
