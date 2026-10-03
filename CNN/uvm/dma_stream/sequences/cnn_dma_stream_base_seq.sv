class cnn_dma_stream_base_seq extends cnn_base_sequence;
    `uvm_object_utils(cnn_dma_stream_base_seq)

    // DMA / Stream Verification configuration
    cnn_dma_stream_cfg cfg;

    function new(string name = "cnn_dma_stream_base_seq");
        super.new(name);
    endfunction

    function void set_cfg(cnn_dma_stream_cfg cfg_i);
        cfg = cfg_i;
    endfunction

    // cfg 안넣고 sequence 실행한 경우 표시 (디버깅을 위한 body)
    task pre_body();
        if (cfg == null) begin
            `uvm_fatal(get_type_name(),
                       "cnn_dma_stream_cfg was not assigned to sequence")
        end
    endtask

    task wait_dma_write_commit(virtual cnn_if vif_i, bit [31:0] exp_addr,
                               bit [31:0] exp_data,
                               int unsigned timeout_cycles = 10000);

        bit                 aw_seen;
        bit                 w_seen;

        bit          [31:0] got_addr;
        bit          [31:0] got_data;

        int unsigned        count;

        aw_seen = 1'b0;
        w_seen = 1'b0;

        got_addr = 32'd0;
        got_data = 32'd0;

        count = 0;

        while (count < timeout_cycles) begin

            @(vif_i.m_axil_rsp_cb);

            // AW handshake
            if (
            !aw_seen &&
            vif_i.m_axil_rsp_cb.m_axil_awvalid &&
            vif_i.m_axil_awready
        ) begin

                aw_seen  = 1'b1;
                got_addr = vif_i.m_axil_rsp_cb.m_axil_awaddr;

            end

            // W handshake
            if (
            !w_seen &&
            vif_i.m_axil_rsp_cb.m_axil_wvalid &&
            vif_i.m_axil_wready
        ) begin

                w_seen   = 1'b1;
                got_data = vif_i.m_axil_rsp_cb.m_axil_wdata;

            end

            // Write response handshake
            //
            // 이 시점에서 한 AXI-Lite write transaction 완료.
            if (
            aw_seen &&
            w_seen &&
            vif_i.m_axil_bvalid &&
            vif_i.m_axil_rsp_cb.m_axil_bready
        ) begin

                if (vif_i.m_axil_bresp != 2'b00) begin

                    `uvm_fatal(
                        get_type_name(),
                        $sformatf(
                            "DMA write BRESP error addr=0x%08h resp=0x%0h",
                            got_addr, vif_i.m_axil_bresp))

                end

                if (got_addr == exp_addr) begin

                    if (got_data != exp_data) begin

                        `uvm_fatal(
                            get_type_name(),
                            $sformatf(
                                {"DMA write data mismatch ",
                                 "addr=0x%08h expected=0x%08h actual=0x%08h"},
                                    exp_addr, exp_data, got_data))

                    end

                    `uvm_info(
                        get_type_name(),
                        $sformatf(
                            "DMA write committed: addr=0x%08h data=0x%08h",
                            got_addr, got_data), UVM_LOW)

                    return;

                end

                // 다른 DMA write였다면 다음 transaction 관찰
                aw_seen = 1'b0;
                w_seen  = 1'b0;

            end

            count++;

        end

        `uvm_fatal(get_type_name(),
                   $sformatf(
                       "Timeout waiting DMA write addr=0x%08h data=0x%08h",
                       exp_addr, exp_data))

    endtask

    task body();
        // intentionally empty
        // S01 ~ S08 scenario sequences derive from this class
    endtask

endclass
