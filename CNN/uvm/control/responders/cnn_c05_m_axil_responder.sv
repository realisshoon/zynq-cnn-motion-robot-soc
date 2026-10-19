// A small AXI DMA register model for C05. Status bits follow completed AXIS
// transfers; they are not asserted merely to advance the controller FSM.
class cnn_c05_m_axil_responder extends cnn_m_axil_responder;
    `uvm_component_utils(cnn_c05_m_axil_responder)

    bit [3:0] halted, idle, ioc;
    int unsigned image_beats;
    int unsigned weight_beats;
    int unsigned weight_last_offered;

    function new(string name, uvm_component parent);
        super.new(name, parent);
    endfunction

    // DMA channel order: image MM2S, weight MM2S, feature MM2S, feature S2MM.
    function int channel(bit [31:0] addr);
        if ((addr & 32'hffff_ffc0) == 32'h4040_0000) return 0;
        if ((addr & 32'hffff_ffc0) == 32'h4041_0000) return 1;
        if ((addr & 32'hffff_ff00) == 32'h4042_0000)
            return (addr[5:0] >= 6'h30) ? 3 : 2;
        return -1;
    endfunction

    function bit [31:0] status_word(int ch);
        return {17'd0, ioc[ch], 12'd0, idle[ch], halted[ch]};
    endfunction

    // C08 overrides only these fault-injection hooks. Normal C05/C06/C07
    // behavior remains the same when the hooks return their default values.
    virtual function bit [31:0] read_status_data(bit [31:0] addr, int ch);
        return status_word(ch);
    endfunction

    virtual function bit [1:0] read_status_resp(bit [31:0] addr);
        return 2'b00;
    endfunction

    virtual function void read_response_accepted();
    endfunction

    task run_phase(uvm_phase phase);
        bit aw_seen, w_seen;
        bit [31:0] aw_addr, w_data;
        bit [31:0] read_addr;
        int ch;

        vif.m_axil_awready = 0;
        vif.m_axil_wready = 0;
        vif.m_axil_bresp = 0;
        vif.m_axil_bvalid = 0;
        vif.m_axil_arready = 0;
        vif.m_axil_rdata = 0;
        vif.m_axil_rresp = 0;
        vif.m_axil_rvalid = 0;
        aw_seen = 0;
        w_seen = 0;
        halted = 4'hf;
        idle = 4'hf;
        ioc = 0;
        image_beats = 0;
        weight_beats = 0;
        weight_last_offered = 0;
        wait (vif.rst_n === 1'b1);
        `uvm_info("C05_DMA", "C05 DMA responder active", UVM_LOW)

        forever begin
            @(vif.m_axil_rsp_cb);

            // A hard reset may interrupt an outstanding master read. Clear
            // pending response state so the next frame can use the bus again.
            if (!vif.rst_n) begin
                aw_seen = 0;
                w_seen = 0;
                halted = 4'hf;
                idle = 4'hf;
                ioc = 0;
                image_beats = 0;
                weight_beats = 0;
                vif.m_axil_rsp_cb.m_axil_awready <= 0;
                vif.m_axil_rsp_cb.m_axil_wready <= 0;
                vif.m_axil_rsp_cb.m_axil_bvalid <= 0;
                vif.m_axil_rsp_cb.m_axil_arready <= 0;
                vif.m_axil_rsp_cb.m_axil_rvalid <= 0;
                continue;
            end

            // DMA completion is evidence from an accepted stream transfer.
            if (vif.mon_cb.s_weight_valid && vif.mon_cb.s_weight_ready) begin
                weight_beats++;
                if (vif.mon_cb.s_weight_last) begin
                    idle[1] = 1;
                    ioc[1] = 1;
                    `uvm_info("C05_DMA", $sformatf("weight transfer completed: beats=%0d", weight_beats), UVM_LOW)
                    weight_beats = 0;
                end
            end
            if (vif.mon_cb.s_weight_valid && vif.mon_cb.s_weight_last) weight_last_offered++;
            if (vif.mon_cb.s_image_valid && vif.mon_cb.s_image_ready) begin
                image_beats++;
                // The image DMA's SG frame contains 144 lines x 480 beats.
                if (image_beats == 144 * 480) begin
                    idle[0] = 1;
                    ioc[0] = 1;
                    `uvm_info("C05_DMA", "image transfer completed", UVM_LOW)
                end
            end
            if (vif.mon_cb.s_feature_valid && vif.mon_cb.s_feature_ready && vif.mon_cb.s_feature_last) begin
                idle[2] = 1;
                ioc[2] = 1;
            end
            if (vif.mon_cb.m_feature_valid && vif.mon_cb.m_feature_ready && vif.mon_cb.m_feature_last) begin
                idle[3] = 1;
                ioc[3] = 1;
            end

            vif.m_axil_rsp_cb.m_axil_awready <= !aw_seen && !vif.m_axil_bvalid;
            vif.m_axil_rsp_cb.m_axil_wready <= !w_seen && !vif.m_axil_bvalid;
            if (vif.m_axil_rsp_cb.m_axil_awvalid && vif.m_axil_awready) begin
                aw_addr = vif.m_axil_rsp_cb.m_axil_awaddr;
                aw_seen = 1;
            end
            if (vif.m_axil_rsp_cb.m_axil_wvalid && vif.m_axil_wready) begin
                w_data = vif.m_axil_rsp_cb.m_axil_wdata;
                w_seen = 1;
            end
            if (aw_seen && w_seen && !vif.m_axil_bvalid) begin
                ch = channel(aw_addr);
                if (ch < 0) begin
                    vif.m_axil_rsp_cb.m_axil_bresp <= 2'b10;
                    `uvm_error("C05_DMA_ADDR", $sformatf("Unsupported DMA write %08h", aw_addr))
                end else begin
                    vif.m_axil_rsp_cb.m_axil_bresp <= 2'b00;
                    case (aw_addr[5:0])
                        6'h00, 6'h30: begin
                            halted[ch] = !w_data[0];
                            if (w_data[0]) idle[ch] = 0;
                            else idle[ch] = 1;
                        end
                        6'h04, 6'h34: if (w_data[12]) ioc[ch] = 0;
                        6'h10: image_beats = 0; // SG tail descriptor starts a frame.
                        6'h28, 6'h58: idle[ch] = 0; // BTT arms a direct DMA transfer.
                        default: ; // Source/destination/SG address registers.
                    endcase
                end
                vif.m_axil_rsp_cb.m_axil_bvalid <= 1;
            end
            if (vif.m_axil_bvalid && vif.m_axil_rsp_cb.m_axil_bready) begin
                vif.m_axil_rsp_cb.m_axil_bvalid <= 0;
                aw_seen = 0;
                w_seen = 0;
            end

            vif.m_axil_rsp_cb.m_axil_arready <= !vif.m_axil_rvalid;
            if (vif.m_axil_rsp_cb.m_axil_arvalid && vif.m_axil_arready) begin
                read_addr = vif.m_axil_rsp_cb.m_axil_araddr;
                ch = channel(read_addr);
                if (ch >= 0 && (read_addr[5:0] == 6'h04 ||
                                read_addr[5:0] == 6'h34)) begin
                    vif.m_axil_rsp_cb.m_axil_rdata <= read_status_data(read_addr, ch);
                    vif.m_axil_rsp_cb.m_axil_rresp <= read_status_resp(read_addr);
                end else begin
                    vif.m_axil_rsp_cb.m_axil_rdata <= 0;
                    vif.m_axil_rsp_cb.m_axil_rresp <= 2'b10;
                    `uvm_error("C05_DMA_ADDR", $sformatf("Unsupported DMA read %08h", read_addr))
                end
                vif.m_axil_rsp_cb.m_axil_rvalid <= 1;
            end
            if (vif.m_axil_rvalid && vif.m_axil_rsp_cb.m_axil_rready) begin
                read_response_accepted();
                vif.m_axil_rsp_cb.m_axil_rvalid <= 0;
            end
        end
    endtask

    function void report_phase(uvm_phase phase);
        super.report_phase(phase);
        `uvm_info("C05_DMA_SUMMARY", $sformatf("weight_beats_since_last=%0d last_offered=%0d idle=%b halted=%b", weight_beats, weight_last_offered, idle, halted), UVM_LOW)
    endfunction
endclass
