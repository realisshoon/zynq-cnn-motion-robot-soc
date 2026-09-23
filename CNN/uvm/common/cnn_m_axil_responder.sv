class cnn_m_axil_responder extends uvm_component;
    `uvm_component_utils(cnn_m_axil_responder)

    virtual cnn_if vif;

    function new(string name, uvm_component parent);
        super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);
        super.build_phase(phase);
        if (!uvm_config_db#(virtual cnn_if)::get(this, "", "vif", vif))
            `uvm_fatal(get_type_name(), "cnn_if was not found in uvm_config_db")
    endfunction

    task run_phase(uvm_phase phase);
        bit aw_seen;
        bit w_seen;

        vif.m_axil_awready = 1'b0;
        vif.m_axil_wready  = 1'b0;
        vif.m_axil_bresp   = 2'b00;
        vif.m_axil_bvalid  = 1'b0;
        vif.m_axil_arready = 1'b0;
        vif.m_axil_rdata   = 32'h0000_0000;
        vif.m_axil_rresp   = 2'b00;
        vif.m_axil_rvalid  = 1'b0;

        wait (vif.rst_n === 1'b1);
        aw_seen = 0;
        w_seen  = 0;

        forever begin
            @(vif.m_axil_rsp_cb);

            // Write request collection. AW and W may handshake independently.
            vif.m_axil_rsp_cb.m_axil_awready <= !aw_seen && !vif.m_axil_rsp_cb.m_axil_bvalid;
            vif.m_axil_rsp_cb.m_axil_wready  <= !w_seen  && !vif.m_axil_rsp_cb.m_axil_bvalid;

            if (vif.m_axil_rsp_cb.m_axil_awvalid &&
                vif.m_axil_rsp_cb.m_axil_awready)
                aw_seen = 1;

            if (vif.m_axil_rsp_cb.m_axil_wvalid &&
                vif.m_axil_rsp_cb.m_axil_wready)
                w_seen = 1;

            if (aw_seen && w_seen && !vif.m_axil_rsp_cb.m_axil_bvalid) begin
                vif.m_axil_rsp_cb.m_axil_bresp  <= 2'b00;
                vif.m_axil_rsp_cb.m_axil_bvalid <= 1'b1;
            end

            if (vif.m_axil_rsp_cb.m_axil_bvalid &&
                vif.m_axil_rsp_cb.m_axil_bready) begin
                vif.m_axil_rsp_cb.m_axil_bvalid <= 1'b0;
                aw_seen = 0;
                w_seen  = 0;
            end

            // Read response placeholder: OKAY with zero data.
            vif.m_axil_rsp_cb.m_axil_arready <= !vif.m_axil_rsp_cb.m_axil_rvalid;

            if (vif.m_axil_rsp_cb.m_axil_arvalid &&
                vif.m_axil_rsp_cb.m_axil_arready) begin
                vif.m_axil_rsp_cb.m_axil_rdata  <= 32'h0000_0000;
                vif.m_axil_rsp_cb.m_axil_rresp  <= 2'b00;
                vif.m_axil_rsp_cb.m_axil_rvalid <= 1'b1;
            end

            if (vif.m_axil_rsp_cb.m_axil_rvalid &&
                vif.m_axil_rsp_cb.m_axil_rready)
                vif.m_axil_rsp_cb.m_axil_rvalid <= 1'b0;
        end
    endtask
endclass
