class cnn_c02_driver extends cnn_driver;

    `uvm_component_utils(cnn_c02_driver)


    function new(string name = "cnn_c02_driver", uvm_component parent = null);

        super.new(name, parent);

    endfunction


    task run_phase(uvm_phase phase);

        cnn_seq_item      tr;
        cnn_c02_axil_item c02_tr;

        init_inputs();

        wait (vif.rst_n === 1'b1);
        repeat (2) @(posedge vif.clk);


        forever begin

            seq_item_port.get_next_item(tr);


            // C02 special AW-first transaction
            if ($cast(
                    c02_tr, tr
                ) && c02_tr.kind == CNN_AXIL_WRITE && c02_tr.aw_first) begin

                drive_axil_write_aw_first(c02_tr);

            end else if ($cast(
                    c02_tr, tr
                ) && c02_tr.kind == CNN_AXIL_WRITE && c02_tr.w_first) begin

                drive_axil_write_w_first(c02_tr);
            end  // Normal transactions use existing common driver behavior
            else if ($cast(
                    c02_tr, tr
                ) && c02_tr.kind == CNN_AXIL_WRITE && c02_tr.delay_bready) begin

                drive_axil_write_delayed_bready(c02_tr);

            end else if ($cast(
                    c02_tr, tr
                ) && c02_tr.kind == CNN_AXIL_READ && c02_tr.delay_rready) begin

                drive_axil_read_delayed_rready(c02_tr);

            end else begin

                case (tr.kind)

                    CNN_AXIL_WRITE: drive_axil_write(tr);

                    CNN_AXIL_READ: drive_axil_read(tr);

                    CNN_IMAGE_BEAT: drive_image(tr);

                    CNN_WEIGHT_BEAT: drive_weight(tr);

                    CNN_FEATURE_IN_BEAT: drive_feature_in(tr);

                    CNN_SET_FEATURE_READY: drive_feature_ready(tr);

                    default:
                    `uvm_error(get_type_name(), $sformatf(
                               "Unsupported driver item kind=%0d", tr.kind))

                endcase

            end


            seq_item_port.item_done();

        end

    endtask


    task drive_axil_write_aw_first(cnn_c02_axil_item tr);

        int unsigned count;


        // -----------------------------------------------------
        // 1. AW Channel first
        // -----------------------------------------------------

        @(vif.ctrl_drv_cb);

        vif.ctrl_drv_cb.s_axil_awaddr  <= tr.addr;
        vif.ctrl_drv_cb.s_axil_awprot  <= 3'b000;
        vif.ctrl_drv_cb.s_axil_awvalid <= 1'b1;


        count = 0;

        do begin

            @(vif.ctrl_drv_cb);

            count++;

            if (count > timeout_cycles)
                `uvm_fatal(get_type_name(), "AXI-Lite AW-first address timeout")

        end while (!vif.ctrl_drv_cb.s_axil_awready);


        vif.ctrl_drv_cb.s_axil_awvalid <= 1'b0;


        `uvm_info("C02_AW_FIRST", "AW handshake completed", UVM_LOW)


        // -----------------------------------------------------
        // 2. Intentionally delay W Channel
        // -----------------------------------------------------

        repeat (tr.gap_cycles) @(vif.ctrl_drv_cb);


        // -----------------------------------------------------
        // 3. W Channel
        // -----------------------------------------------------

        vif.ctrl_drv_cb.s_axil_wdata  <= tr.data32;
        vif.ctrl_drv_cb.s_axil_wstrb  <= tr.strb;
        vif.ctrl_drv_cb.s_axil_wvalid <= 1'b1;


        count = 0;

        do begin

            @(vif.ctrl_drv_cb);

            count++;

            if (count > timeout_cycles)
                `uvm_fatal(get_type_name(), "AXI-Lite AW-first data timeout")

        end while (!vif.ctrl_drv_cb.s_axil_wready);


        vif.ctrl_drv_cb.s_axil_wvalid <= 1'b0;


        `uvm_info("C02_AW_FIRST",
                  $sformatf("W handshake completed after %0d cycle gap",
                            tr.gap_cycles), UVM_LOW)


        // -----------------------------------------------------
        // 4. Write Response
        // -----------------------------------------------------

        vif.ctrl_drv_cb.s_axil_bready <= 1'b1;


        count = 0;

        do begin

            @(vif.ctrl_drv_cb);

            count++;

            if (count > timeout_cycles)
                `uvm_fatal(get_type_name(),
                           "AXI-Lite AW-first response timeout")

        end while (!vif.ctrl_drv_cb.s_axil_bvalid);


        tr.resp = vif.ctrl_drv_cb.s_axil_bresp;

        vif.ctrl_drv_cb.s_axil_bready <= 1'b0;

    endtask

    task drive_axil_write_w_first(cnn_c02_axil_item tr);

        int unsigned count;


        // -----------------------------------------------------
        // 1. W Channel first
        // -----------------------------------------------------

        @(vif.ctrl_drv_cb);

        vif.ctrl_drv_cb.s_axil_wdata  <= tr.data32;
        vif.ctrl_drv_cb.s_axil_wstrb  <= tr.strb;
        vif.ctrl_drv_cb.s_axil_wvalid <= 1'b1;


        count = 0;

        do begin

            @(vif.ctrl_drv_cb);

            count++;

            if (count > timeout_cycles)
                `uvm_fatal(get_type_name(), "AXI-Lite W-first data timeout")

        end while (!vif.ctrl_drv_cb.s_axil_wready);


        vif.ctrl_drv_cb.s_axil_wvalid <= 1'b0;


        `uvm_info("C02_W_FIRST", "W handshake completed", UVM_LOW)


        // -----------------------------------------------------
        // 2. Intentionally delay AW Channel
        // -----------------------------------------------------

        repeat (tr.gap_cycles) @(vif.ctrl_drv_cb);


        // -----------------------------------------------------
        // 3. AW Channel
        // -----------------------------------------------------

        vif.ctrl_drv_cb.s_axil_awaddr  <= tr.addr;
        vif.ctrl_drv_cb.s_axil_awprot  <= 3'b000;
        vif.ctrl_drv_cb.s_axil_awvalid <= 1'b1;


        count = 0;

        do begin

            @(vif.ctrl_drv_cb);

            count++;

            if (count > timeout_cycles)
                `uvm_fatal(get_type_name(), "AXI-Lite W-first address timeout")

        end while (!vif.ctrl_drv_cb.s_axil_awready);


        vif.ctrl_drv_cb.s_axil_awvalid <= 1'b0;


        `uvm_info("C02_W_FIRST",
                  $sformatf("AW handshake completed after %0d cycle gap",
                            tr.gap_cycles), UVM_LOW)


        // -----------------------------------------------------
        // 4. Write Response
        // -----------------------------------------------------

        vif.ctrl_drv_cb.s_axil_bready <= 1'b1;


        count = 0;

        do begin

            @(vif.ctrl_drv_cb);

            count++;

            if (count > timeout_cycles)
                `uvm_fatal(get_type_name(), "AXI-Lite W-first response timeout")

        end while (!vif.ctrl_drv_cb.s_axil_bvalid);


        tr.resp = vif.ctrl_drv_cb.s_axil_bresp;

        vif.ctrl_drv_cb.s_axil_bready <= 1'b0;

    endtask

    task drive_axil_write_delayed_bready(cnn_c02_axil_item tr);

        bit aw_done;
        bit w_done;
        bit [1:0] held_bresp;

        int unsigned count;
        int unsigned i;


        aw_done = 1'b0;
        w_done  = 1'b0;


        // -----------------------------------------------------
        // 1. Normal AW / W request
        // -----------------------------------------------------

        @(vif.ctrl_drv_cb);

        vif.ctrl_drv_cb.s_axil_awaddr  <= tr.addr;
        vif.ctrl_drv_cb.s_axil_awprot  <= 3'b000;
        vif.ctrl_drv_cb.s_axil_awvalid <= 1'b1;

        vif.ctrl_drv_cb.s_axil_wdata   <= tr.data32;
        vif.ctrl_drv_cb.s_axil_wstrb   <= tr.strb;
        vif.ctrl_drv_cb.s_axil_wvalid  <= 1'b1;

        // Important:
        // BREADY stays LOW.
        vif.ctrl_drv_cb.s_axil_bready  <= 1'b0;


        count = 0;

        while (!(aw_done && w_done)) begin

            @(vif.ctrl_drv_cb);

            if (!aw_done && vif.ctrl_drv_cb.s_axil_awready) begin

                aw_done = 1'b1;

                vif.ctrl_drv_cb.s_axil_awvalid <= 1'b0;

            end


            if (!w_done && vif.ctrl_drv_cb.s_axil_wready) begin

                w_done = 1'b1;

                vif.ctrl_drv_cb.s_axil_wvalid <= 1'b0;

            end


            count++;

            if (count > timeout_cycles)
                `uvm_fatal(get_type_name(),
                           "AXI-Lite delayed-BREADY request timeout")

        end


        // -----------------------------------------------------
        // 2. Wait for DUT to assert BVALID
        //
        // BREADY is still 0.
        // Slave must assert BVALID independently of BREADY.
        // -----------------------------------------------------

        count = 0;

        do begin

            @(vif.ctrl_drv_cb);

            count++;

            if (count > timeout_cycles)
                `uvm_fatal(get_type_name(),
                           "AXI-Lite delayed-BREADY BVALID timeout")

        end while (!vif.ctrl_drv_cb.s_axil_bvalid);


        held_bresp = vif.ctrl_drv_cb.s_axil_bresp;


        `uvm_info("C02_DELAY_BREADY",
                  $sformatf("BVALID observed while BREADY=0, BRESP=%02b",
                            held_bresp), UVM_LOW)


        // -----------------------------------------------------
        // 3. Keep BREADY low intentionally
        //
        // While BREADY=0:
        //   BVALID must remain asserted.
        //   BRESP must remain stable.
        // -----------------------------------------------------

        for (i = 0; i < tr.bready_delay_cycles; i++) begin

            @(vif.ctrl_drv_cb);


            if (!vif.ctrl_drv_cb.s_axil_bvalid) begin

                `uvm_error(
                    "C02_BVALID_HOLD",
                    $sformatf(
                        "BVALID dropped while BREADY=0 at delay cycle %0d",
                        i + 1))

            end


            if (vif.ctrl_drv_cb.s_axil_bresp != held_bresp) begin

                `uvm_error(
                    "C02_BRESP_STABLE",
                    $sformatf(
                        "BRESP changed while BREADY=0: expected=%02b actual=%02b",
                        held_bresp, vif.ctrl_drv_cb.s_axil_bresp))

            end

        end


        `uvm_info("C02_DELAY_BREADY",
                  $sformatf("BVALID/BRESP held for %0d cycles while BREADY=0",
                            tr.bready_delay_cycles), UVM_LOW)


        // -----------------------------------------------------
        // 4. Finally assert BREADY
        // -----------------------------------------------------

        vif.ctrl_drv_cb.s_axil_bready <= 1'b1;


        @(vif.ctrl_drv_cb);


        if (!vif.ctrl_drv_cb.s_axil_bvalid) begin

            `uvm_error(
                "C02_BVALID_HANDSHAKE",
                "BVALID was not asserted when BREADY completed the response handshake")

        end


        tr.resp = vif.ctrl_drv_cb.s_axil_bresp;


        // -----------------------------------------------------
        // 5. Finish response
        // -----------------------------------------------------

        vif.ctrl_drv_cb.s_axil_bready <= 1'b0;

    endtask

        task drive_axil_read_delayed_rready(
        cnn_c02_axil_item tr
    );

        bit [31:0] held_rdata;
        bit [1:0]  held_rresp;

        int unsigned count;
        int unsigned i;


        // -----------------------------------------------------
        // 1. Read Address Channel
        // -----------------------------------------------------

        @(vif.ctrl_drv_cb);

        vif.ctrl_drv_cb.s_axil_araddr  <= tr.addr;
        vif.ctrl_drv_cb.s_axil_arprot  <= 3'b000;
        vif.ctrl_drv_cb.s_axil_arvalid <= 1'b1;

        // RREADY intentionally stays LOW.
        vif.ctrl_drv_cb.s_axil_rready  <= 1'b0;


        count = 0;

        do begin

            @(vif.ctrl_drv_cb);

            count++;

            if (count > timeout_cycles)
                `uvm_fatal(
                    get_type_name(),
                    "AXI-Lite delayed-RREADY address timeout"
                )

        end while (!vif.ctrl_drv_cb.s_axil_arready);


        vif.ctrl_drv_cb.s_axil_arvalid <= 1'b0;


        // -----------------------------------------------------
        // 2. Wait for DUT to assert RVALID
        //
        // RREADY is still 0.
        // -----------------------------------------------------

        count = 0;

        do begin

            @(vif.ctrl_drv_cb);

            count++;

            if (count > timeout_cycles)
                `uvm_fatal(
                    get_type_name(),
                    "AXI-Lite delayed-RREADY RVALID timeout"
                )

        end while (!vif.ctrl_drv_cb.s_axil_rvalid);


        held_rdata = vif.ctrl_drv_cb.s_axil_rdata;
        held_rresp = vif.ctrl_drv_cb.s_axil_rresp;


        `uvm_info(
            "C02_DELAY_RREADY",
            $sformatf(
                "RVALID observed while RREADY=0, RDATA=0x%08h RRESP=%02b",
                held_rdata,
                held_rresp
            ),
            UVM_LOW
        )


        // -----------------------------------------------------
        // 3. Keep RREADY low intentionally
        //
        // While RREADY=0:
        //   RVALID must remain asserted.
        //   RDATA must remain stable.
        //   RRESP must remain stable.
        // -----------------------------------------------------

        for (i = 0; i < tr.rready_delay_cycles; i++) begin

            @(vif.ctrl_drv_cb);


            if (!vif.ctrl_drv_cb.s_axil_rvalid) begin

                `uvm_error(
                    "C02_RVALID_HOLD",
                    $sformatf(
                        "RVALID dropped while RREADY=0 at delay cycle %0d",
                        i + 1
                    )
                )

            end


            if (vif.ctrl_drv_cb.s_axil_rdata != held_rdata) begin

                `uvm_error(
                    "C02_RDATA_STABLE",
                    $sformatf(
                        "RDATA changed while RREADY=0: expected=0x%08h actual=0x%08h",
                        held_rdata,
                        vif.ctrl_drv_cb.s_axil_rdata
                    )
                )

            end


            if (vif.ctrl_drv_cb.s_axil_rresp != held_rresp) begin

                `uvm_error(
                    "C02_RRESP_STABLE",
                    $sformatf(
                        "RRESP changed while RREADY=0: expected=%02b actual=%02b",
                        held_rresp,
                        vif.ctrl_drv_cb.s_axil_rresp
                    )
                )

            end

        end


        `uvm_info(
            "C02_DELAY_RREADY",
            $sformatf(
                "RVALID/RDATA/RRESP held for %0d cycles while RREADY=0",
                tr.rready_delay_cycles
            ),
            UVM_LOW
        )


        // -----------------------------------------------------
        // 4. Finally assert RREADY
        // -----------------------------------------------------

        vif.ctrl_drv_cb.s_axil_rready <= 1'b1;


        @(vif.ctrl_drv_cb);


        if (!vif.ctrl_drv_cb.s_axil_rvalid) begin

            `uvm_error(
                "C02_RVALID_HANDSHAKE",
                "RVALID was not asserted when RREADY completed the read handshake"
            )

        end


        // Response was captured while it was held stable.
        tr.rdata = held_rdata;
        tr.resp  = held_rresp;


        // -----------------------------------------------------
        // 5. Finish response
        // -----------------------------------------------------

        vif.ctrl_drv_cb.s_axil_rready <= 1'b0;

    endtask

endclass
