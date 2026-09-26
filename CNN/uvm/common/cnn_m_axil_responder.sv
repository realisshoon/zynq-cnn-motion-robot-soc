class cnn_m_axil_responder extends uvm_component;

    `uvm_component_utils(cnn_m_axil_responder)

    virtual cnn_if vif;


    function new(string name, uvm_component parent);
        super.new(name, parent);
    endfunction


    function void build_phase(uvm_phase phase);

        super.build_phase(phase);

        if (!uvm_config_db#(virtual cnn_if)::get(this, "", "vif", vif)) begin

            `uvm_fatal(get_type_name(), "cnn_if was not found in uvm_config_db")

        end

    endfunction


    task run_phase(uvm_phase phase);

        bit aw_seen;
        bit w_seen;


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


        aw_seen = 1'b0;
        w_seen  = 1'b0;


        forever begin

            @(vif.m_axil_rsp_cb);


            // =================================================
            // WRITE CHANNEL
            //
            // DUT -> TB:
            //   AWVALID / WVALID / BREADY
            //   clocking block input으로 sample
            //
            // TB -> DUT:
            //   AWREADY / WREADY / BVALID
            //   clocking block output으로 drive
            //
            // TB가 자신이 drive 중인 output 값을 읽을 때는
            // raw interface signal을 사용한다.
            // =================================================


            // -------------------------------------------------
            // AWREADY / WREADY
            // -------------------------------------------------

            vif.m_axil_rsp_cb.m_axil_awready <= !aw_seen && !vif.m_axil_bvalid;

            vif.m_axil_rsp_cb.m_axil_wready  <= !w_seen && !vif.m_axil_bvalid;


            // -------------------------------------------------
            // AW handshake
            // -------------------------------------------------

            if (vif.m_axil_rsp_cb.m_axil_awvalid && vif.m_axil_awready) begin

                aw_seen = 1'b1;

            end


            // -------------------------------------------------
            // W handshake
            // -------------------------------------------------

            if (vif.m_axil_rsp_cb.m_axil_wvalid && vif.m_axil_wready) begin

                w_seen = 1'b1;

            end


            // -------------------------------------------------
            // AW + W 모두 수신하면 B response 발생
            // -------------------------------------------------

            if (aw_seen && w_seen && !vif.m_axil_bvalid) begin

                vif.m_axil_rsp_cb.m_axil_bresp  <= 2'b00;
                vif.m_axil_rsp_cb.m_axil_bvalid <= 1'b1;

            end


            // -------------------------------------------------
            // B handshake
            // -------------------------------------------------

            if (vif.m_axil_bvalid && vif.m_axil_rsp_cb.m_axil_bready) begin

                vif.m_axil_rsp_cb.m_axil_bvalid <= 1'b0;

                aw_seen = 1'b0;
                w_seen  = 1'b0;

            end


            // =================================================
            // READ CHANNEL
            //
            // Placeholder responder:
            // OKAY + 0x00000000
            // =================================================


            // -------------------------------------------------
            // ARREADY
            // -------------------------------------------------

            vif.m_axil_rsp_cb.m_axil_arready <= !vif.m_axil_rvalid;


            // -------------------------------------------------
            // AR handshake
            // -------------------------------------------------

            if (vif.m_axil_rsp_cb.m_axil_arvalid && vif.m_axil_arready) begin

                vif.m_axil_rsp_cb.m_axil_rdata  <= 32'h0000_0000;

                vif.m_axil_rsp_cb.m_axil_rresp  <= 2'b00;

                vif.m_axil_rsp_cb.m_axil_rvalid <= 1'b1;

            end


            // -------------------------------------------------
            // R handshake
            // -------------------------------------------------

            if (vif.m_axil_rvalid && vif.m_axil_rsp_cb.m_axil_rready) begin

                vif.m_axil_rsp_cb.m_axil_rvalid <= 1'b0;

            end

        end

    endtask

endclass
