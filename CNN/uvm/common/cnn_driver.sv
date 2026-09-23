class cnn_driver extends uvm_driver #(cnn_seq_item);
    `uvm_component_utils(cnn_driver)

    virtual cnn_if vif;
    int unsigned timeout_cycles = 10000;

    function new(string name, uvm_component parent);
        super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);
        super.build_phase(phase);
        if (!uvm_config_db#(virtual cnn_if)::get(this, "", "vif", vif))
            `uvm_fatal(get_type_name(), "cnn_if was not found in uvm_config_db")
    endfunction

    task run_phase(uvm_phase phase);
        init_inputs();
        wait (vif.rst_n === 1'b1);
        repeat (2) @(posedge vif.clk);

        forever begin
            seq_item_port.get_next_item(req);
            case (req.kind)
                CNN_AXIL_WRITE:        drive_axil_write(req);
                CNN_AXIL_READ:         drive_axil_read(req);
                CNN_IMAGE_BEAT:        drive_image(req);
                CNN_WEIGHT_BEAT:       drive_weight(req);
                CNN_FEATURE_IN_BEAT:   drive_feature_in(req);
                CNN_SET_FEATURE_READY: drive_feature_ready(req);
                default:
                    `uvm_error(get_type_name(),
                               $sformatf("Unsupported driver item kind=%0d", req.kind))
            endcase
            seq_item_port.item_done();
        end
    endtask

    task init_inputs();
        vif.s_axil_awaddr   = '0;
        vif.s_axil_awvalid  = 1'b0;
        vif.s_axil_awprot   = 3'b000;
        vif.s_axil_wdata    = '0;
        vif.s_axil_wstrb    = 4'hf;
        vif.s_axil_wvalid   = 1'b0;
        vif.s_axil_bready   = 1'b0;
        vif.s_axil_araddr   = '0;
        vif.s_axil_arvalid  = 1'b0;
        vif.s_axil_arprot   = 3'b000;
        vif.s_axil_rready   = 1'b0;

        vif.s_image_data    = '0;
        vif.s_image_valid   = 1'b0;
        vif.s_image_last    = 1'b0;
        vif.s_image_keep    = 8'hff;

        vif.s_weight_data   = '0;
        vif.s_weight_valid  = 1'b0;
        vif.s_weight_last   = 1'b0;
        vif.s_weight_keep   = 8'hff;

        vif.s_feature_data  = '0;
        vif.s_feature_valid = 1'b0;
        vif.s_feature_last  = 1'b0;
        vif.s_feature_keep  = 8'hff;

        vif.m_feature_ready = 1'b1;
    endtask

    task drive_axil_write(cnn_seq_item tr);
        bit aw_done, w_done;
        int unsigned count;

        aw_done = 0;
        w_done  = 0;

        @(vif.ctrl_drv_cb);
        vif.ctrl_drv_cb.s_axil_awaddr  <= tr.addr;
        vif.ctrl_drv_cb.s_axil_awprot  <= 3'b000;
        vif.ctrl_drv_cb.s_axil_awvalid <= 1'b1;
        vif.ctrl_drv_cb.s_axil_wdata   <= tr.data32;
        vif.ctrl_drv_cb.s_axil_wstrb   <= tr.strb;
        vif.ctrl_drv_cb.s_axil_wvalid  <= 1'b1;

        count = 0;
        while (!(aw_done && w_done)) begin
            @(vif.ctrl_drv_cb);
            if (!aw_done && vif.ctrl_drv_cb.s_axil_awready) begin
                aw_done = 1;
                vif.ctrl_drv_cb.s_axil_awvalid <= 1'b0;
            end
            if (!w_done && vif.ctrl_drv_cb.s_axil_wready) begin
                w_done = 1;
                vif.ctrl_drv_cb.s_axil_wvalid <= 1'b0;
            end
            count++;
            if (count > timeout_cycles)
                `uvm_fatal(get_type_name(), "AXI-Lite write request timeout")
        end

        vif.ctrl_drv_cb.s_axil_bready <= 1'b1;
        count = 0;
        do begin
            @(vif.ctrl_drv_cb);
            count++;
            if (count > timeout_cycles)
                `uvm_fatal(get_type_name(), "AXI-Lite write response timeout")
        end while (!vif.ctrl_drv_cb.s_axil_bvalid);

        tr.resp = vif.ctrl_drv_cb.s_axil_bresp;
        vif.ctrl_drv_cb.s_axil_bready <= 1'b0;
    endtask

    task drive_axil_read(cnn_seq_item tr);
        int unsigned count;

        @(vif.ctrl_drv_cb);
        vif.ctrl_drv_cb.s_axil_araddr  <= tr.addr;
        vif.ctrl_drv_cb.s_axil_arprot  <= 3'b000;
        vif.ctrl_drv_cb.s_axil_arvalid <= 1'b1;

        count = 0;
        do begin
            @(vif.ctrl_drv_cb);
            count++;
            if (count > timeout_cycles)
                `uvm_fatal(get_type_name(), "AXI-Lite read address timeout")
        end while (!vif.ctrl_drv_cb.s_axil_arready);

        vif.ctrl_drv_cb.s_axil_arvalid <= 1'b0;
        vif.ctrl_drv_cb.s_axil_rready  <= 1'b1;

        count = 0;
        do begin
            @(vif.ctrl_drv_cb);
            count++;
            if (count > timeout_cycles)
                `uvm_fatal(get_type_name(), "AXI-Lite read response timeout")
        end while (!vif.ctrl_drv_cb.s_axil_rvalid);

        tr.rdata = vif.ctrl_drv_cb.s_axil_rdata;
        tr.resp  = vif.ctrl_drv_cb.s_axil_rresp;
        vif.ctrl_drv_cb.s_axil_rready <= 1'b0;
    endtask

    task drive_image(cnn_seq_item tr);
        @(vif.image_drv_cb);
        vif.image_drv_cb.s_image_data  <= tr.data64;
        vif.image_drv_cb.s_image_keep  <= tr.keep;
        vif.image_drv_cb.s_image_last  <= tr.last;
        vif.image_drv_cb.s_image_valid <= 1'b1;
        do @(vif.image_drv_cb); while (!vif.image_drv_cb.s_image_ready);
        vif.image_drv_cb.s_image_valid <= 1'b0;
    endtask

    task drive_weight(cnn_seq_item tr);
        @(vif.weight_drv_cb);
        vif.weight_drv_cb.s_weight_data  <= tr.data64;
        vif.weight_drv_cb.s_weight_keep  <= tr.keep;
        vif.weight_drv_cb.s_weight_last  <= tr.last;
        vif.weight_drv_cb.s_weight_valid <= 1'b1;
        do @(vif.weight_drv_cb); while (!vif.weight_drv_cb.s_weight_ready);
        vif.weight_drv_cb.s_weight_valid <= 1'b0;
    endtask

    task drive_feature_in(cnn_seq_item tr);
        @(vif.feature_in_drv_cb);
        vif.feature_in_drv_cb.s_feature_data  <= tr.data64;
        vif.feature_in_drv_cb.s_feature_keep  <= tr.keep;
        vif.feature_in_drv_cb.s_feature_last  <= tr.last;
        vif.feature_in_drv_cb.s_feature_valid <= 1'b1;
        do @(vif.feature_in_drv_cb); while (!vif.feature_in_drv_cb.s_feature_ready);
        vif.feature_in_drv_cb.s_feature_valid <= 1'b0;
    endtask

    task drive_feature_ready(cnn_seq_item tr);
        vif.m_feature_ready = tr.ready_value;
        repeat (tr.hold_cycles) @(posedge vif.clk);
        if (!tr.ready_value)
            vif.m_feature_ready = 1'b1;
    endtask
endclass
