class cnn_dma_master_monitor extends uvm_monitor;
    `uvm_component_utils(cnn_dma_master_monitor)

    virtual cnn_if vif;
    int unsigned aw_stall_cycles=0,w_stall_cycles=0,ar_stall_cycles=0;

    uvm_analysis_port #(cnn_dma_event_item) ap;

    function new(string name = "cnn_dma_master_monitor",
                 uvm_component parent = null);
        super.new(name, parent);
        ap = new("ap", this);
    endfunction

    function void build_phase(uvm_phase phase);
        super.build_phase(phase);

        if (!uvm_config_db#(virtual cnn_if)::get(this, "", "vif", vif)) begin
            `uvm_fatal(get_type_name(), "cnn_if was not found")
        end
    endfunction

    task run_phase(uvm_phase phase);

        bit aw_seen;  // write 주소를 받았는가
        bit [31:0] awaddr_q;  // 받은 write 주소 저장

        bit w_seen;  // write 데이터를 이미 받았는가
        bit [31:0] wdata_q;  // 받은 write 데이터 저장

        bit [3:0] wstrb_q;  // write strobe 저장

        bit ar_seen;  // read 주소를 받았는가
        bit [31:0] araddr_q;  // 읽으려는 dma register 주소

        cnn_dma_event_item tr;
        longint unsigned cycle, aw_cycle, w_cycle, ar_cycle;

        cycle=0; aw_cycle=0; w_cycle=0; ar_cycle=0;

        // 초기 상태
        aw_seen = 0;
        w_seen = 0;
        ar_seen = 0;

        awaddr_q = 0;
        wdata_q = 0;
        wstrb_q = 0;
        araddr_q = 0;

        forever begin
            @(posedge vif.clk);
            cycle++;

            if (!vif.rst_n) begin
                aw_seen = 0;
                w_seen = 0;
                ar_seen = 0;

                continue;
            end

            if(vif.m_axil_awvalid && !vif.m_axil_awready) aw_stall_cycles++;
            if(vif.m_axil_wvalid && !vif.m_axil_wready) w_stall_cycles++;
            if(vif.m_axil_arvalid && !vif.m_axil_arready) ar_stall_cycles++;

            // AXI4-Lite WRITE ADDRESS Handshake
            if (vif.m_axil_awvalid && vif.m_axil_awready) begin
                if (aw_seen) `uvm_error("DMA_PROTOCOL", "Duplicate AW before B")
                aw_cycle=cycle;
                awaddr_q = vif.m_axil_awaddr;
                aw_seen = 1;
            end

            // AXI4-Lite WRITE DATA Handshake
            if (vif.m_axil_wvalid && vif.m_axil_wready) begin
                if (w_seen) `uvm_error("DMA_PROTOCOL", "Duplicate W before B")
                w_cycle=cycle;
                wdata_q = vif.m_axil_wdata;
                wstrb_q = vif.m_axil_wstrb;
                w_seen = 1;
            end

            // AXI4-Lite WRITE RESPONSE Handshake
            if (vif.m_axil_bvalid && vif.m_axil_bready) begin
                if (aw_seen && w_seen) begin
                    tr = cnn_dma_event_item::type_id::create("write_tr");

                    tr.kind = CNN_DMA_AXIL_WRITE;
                    tr.stage=observed_stage;
                    tr.observed_time=$time; tr.commit_cycle=cycle;
                    tr.aw_cycle=aw_cycle; tr.w_cycle=w_cycle;
                    tr.handshake_order=(aw_cycle==w_cycle) ? 0 : (aw_cycle<w_cycle ? 1 : 2);
                    tr.response_cycles=cycle-((aw_cycle>w_cycle) ? aw_cycle : w_cycle);

                    tr.addr = awaddr_q;
                    tr.data = wdata_q;
                    tr.strb = wstrb_q;
                    tr.resp = vif.m_axil_bresp;

                    ap.write(tr);

                end else begin
                    `uvm_error("DMA_PROTOCOL", "B response observed before AW/W transaction")
                end
                
                // 한 write transaction 종료
                aw_seen = 0;
                w_seen = 0;
            end

            // AXI4-Lite READ ADDRESS Handshake
            if (vif.m_axil_arvalid && vif.m_axil_arready) begin
                if (ar_seen) `uvm_error("DMA_PROTOCOL", "Duplicate AR before R")
                ar_cycle=cycle;
                araddr_q = vif.m_axil_araddr;
                ar_seen = 1;
            end

            // AXI4-Lite READ DATA Handshake
            if (vif.m_axil_rvalid && vif.m_axil_rready) begin
                if (ar_seen) begin
                    tr = cnn_dma_event_item::type_id::create("read_tr");

                    tr.kind = CNN_DMA_AXIL_READ;
                    tr.stage=observed_stage;
                    tr.observed_time=$time; tr.commit_cycle=cycle; tr.ar_cycle=ar_cycle;
                    tr.response_cycles=cycle-ar_cycle;

                    tr.addr = araddr_q;
                    tr.data = vif.m_axil_rdata;
                    tr.strb = 4'h0;
                    tr.resp = vif.m_axil_rresp;

                    ap.write(tr);
                end else begin
                    `uvm_error("DMA_PROTOCOL", "R response observed without a captured AR transaction")
                end

                // 한 read transaction 종료
                ar_seen = 0;
            end
        end

    endtask

endclass
