class cnn_m_axil_responder extends uvm_component;

    `uvm_component_utils(cnn_m_axil_responder)

    virtual cnn_if vif;


    function new(
        string name,
        uvm_component parent
    );
        super.new(name, parent);
    endfunction


    function void build_phase(uvm_phase phase);

        super.build_phase(phase);

        if (!uvm_config_db#(virtual cnn_if)::get(
                this,
                "",
                "vif",
                vif
            )) begin

            `uvm_fatal(
                get_type_name(),
                "cnn_if was not found in uvm_config_db"
            )

        end

    endfunction


    task run_phase(uvm_phase phase);

        bit aw_seen;
        bit w_seen;

        bit bvalid_state;
        bit rvalid_state;


        // -----------------------------------------------------
        // Initial values
        // -----------------------------------------------------

        vif.m_axil_awready = 1'b0;
        vif.m_axil_wready  = 1'b0;

        vif.m_axil_bresp   = 2'b00;
        vif.m_axil_bvalid  = 1'b0;

        vif.m_axil_arready = 1'b0;

        vif.m_axil_rdata   = 32'h0000_0000;
        vif.m_axil_rresp   = 2'b00;
        vif.m_axil_rvalid  = 1'b0;


        wait (vif.rst_n === 1'b1);


        aw_seen      = 1'b0;
        w_seen       = 1'b0;
        bvalid_state = 1'b0;
        rvalid_state = 1'b0;


        forever begin

            @(vif.m_axil_rsp_cb);


            // -------------------------------------------------
            // AXI-Lite Write Response
            // -------------------------------------------------

            if (bvalid_state) begin

                vif.m_axil_rsp_cb.m_axil_awready <= 1'b0;
                vif.m_axil_rsp_cb.m_axil_wready  <= 1'b0;
                vif.m_axil_rsp_cb.m_axil_bvalid  <= 1'b1;

                if (vif.m_axil_rsp_cb.m_axil_bready) begin

                    vif.m_axil_rsp_cb.m_axil_bvalid <= 1'b0;

                    bvalid_state = 1'b0;
                    aw_seen      = 1'b0;
                    w_seen       = 1'b0;

                end

            end
            else begin

                vif.m_axil_rsp_cb.m_axil_awready <= !aw_seen;
                vif.m_axil_rsp_cb.m_axil_wready  <= !w_seen;

                if (vif.m_axil_rsp_cb.m_axil_awvalid &&
                    !aw_seen) begin

                    aw_seen = 1'b1;

                end

                if (vif.m_axil_rsp_cb.m_axil_wvalid &&
                    !w_seen) begin

                    w_seen = 1'b1;

                end


                if (aw_seen && w_seen) begin

                    vif.m_axil_rsp_cb.m_axil_awready <= 1'b0;
                    vif.m_axil_rsp_cb.m_axil_wready  <= 1'b0;

                    vif.m_axil_rsp_cb.m_axil_bresp   <= 2'b00;
                    vif.m_axil_rsp_cb.m_axil_bvalid  <= 1'b1;

                    bvalid_state = 1'b1;

                end

            end


            // -------------------------------------------------
            // AXI-Lite Read Response
            // Placeholder: OKAY + zero data
            // -------------------------------------------------

            if (rvalid_state) begin

                vif.m_axil_rsp_cb.m_axil_arready <= 1'b0;
                vif.m_axil_rsp_cb.m_axil_rvalid  <= 1'b1;

                if (vif.m_axil_rsp_cb.m_axil_rready) begin

                    vif.m_axil_rsp_cb.m_axil_rvalid <= 1'b0;
                    rvalid_state = 1'b0;

                end

            end
            else begin

                vif.m_axil_rsp_cb.m_axil_arready <= 1'b1;

                if (vif.m_axil_rsp_cb.m_axil_arvalid) begin

                    vif.m_axil_rsp_cb.m_axil_arready <= 1'b0;

                    vif.m_axil_rsp_cb.m_axil_rdata  <=
                        32'h0000_0000;

                    vif.m_axil_rsp_cb.m_axil_rresp  <=
                        2'b00;

                    vif.m_axil_rsp_cb.m_axil_rvalid <=
                        1'b1;

                    rvalid_state = 1'b1;

                end

            end

        end

    endtask

endclass