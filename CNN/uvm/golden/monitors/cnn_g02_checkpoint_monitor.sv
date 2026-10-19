class cnn_g02_checkpoint_monitor extends uvm_component;

    `uvm_component_utils(cnn_g02_checkpoint_monitor)

    virtual cnn_g02_probe_if vif;

    longint unsigned tx_count[0:28];
    longint unsigned elem_count[0:28];
    integer dump_fd[0:28];
    string dump_dir;

    function new(string name = "cnn_g02_checkpoint_monitor", uvm_component parent = null);
        super.new(name, parent);
    endfunction


    function void build_phase(uvm_phase phase);

        super.build_phase(phase);

        if (!uvm_config_db#(virtual cnn_g02_probe_if)::get(this, "", "g02_vif", vif)) begin
            `uvm_fatal(get_type_name(), "g02_vif was not found")
        end

        foreach (tx_count[i]) begin
            tx_count[i]   = 0;
            elem_count[i] = 0;
        end

        if (!$value$plusargs("G02_DUMP_DIR=%s", dump_dir)) begin
            dump_dir = "rtl_checkpoints";
        end

        foreach (dump_fd[i]) begin
            dump_fd[i] = 0;
        end

    endfunction


    function automatic int unsigned popcount4(bit [3:0] mask);

        int unsigned count;

        count = 0;

        for (int i = 0; i < 4; i++) begin
            if (mask[i]) count++;
        end

        return count;

    endfunction


    function automatic int unsigned popcount32(bit [31:0] mask);

        int unsigned count;

        count = 0;

        for (int i = 0; i < 32; i++) begin
            if (mask[i]) count++;
        end

        return count;

    endfunction


    function automatic longint unsigned expected_elements(int op_id);

        case (op_id)

            0:  return 393216;
            1:  return 393216;
            2:  return 786432;
            3:  return 196608;
            4:  return 393216;
            5:  return 393216;
            6:  return 393216;
            7:  return 98304;
            8:  return 196608;
            9:  return 196608;
            10: return 196608;

            11: return 49152;

            12: return 98304;
            13: return 98304;
            14: return 98304;
            15: return 98304;
            16: return 98304;
            17: return 98304;
            18: return 98304;
            19: return 98304;
            20: return 98304;
            21: return 98304;
            22: return 98304;
            23: return 98304;
            24: return 98304;
            25: return 98304;
            26: return 98304;

            27: return 4352;
            28: return 8704;

            default: return 0;

        endcase

    endfunction


    task run_phase(uvm_phase phase);

        int unsigned op_id;

        forever begin

            @(posedge vif.clk);

            if (!vif.rst_n) continue;


            // OP0
            if (vif.conv0_valid && vif.conv0_ready) begin

                op_id = vif.conv0_tag[37:33];

                if (op_id != 0) begin

                    `uvm_error("G02_MON", $sformatf("Unexpected Conv0 OP ID: %0d", op_id))

                end else begin

                    tx_count[op_id]++;
                    elem_count[op_id] += popcount4(vif.conv0_mask);
                    $fdisplay(get_dump_fd(op_id), "%016h %01h %016h", vif.conv0_tag,
                                  vif.conv0_mask, vif.conv0_data);
                end

            end


            // OP1,3,...25
            if (vif.dw_valid && vif.dw_ready) begin

                op_id = vif.dw_tag[37:33];

                if ((op_id < 1) || (op_id > 25) || !op_id[0]) begin

                    `uvm_error("G02_MON", $sformatf("Unexpected DW OP ID: %0d", op_id))

                end else begin

                    tx_count[op_id]++;
                    elem_count[op_id] += popcount32(vif.dw_mask);
                    $fdisplay(get_dump_fd(op_id), "%016h %08h %064h", vif.dw_tag, vif.dw_mask,
                                  vif.dw_data);
                end

            end


            // OP2,4,...26 + OP27/28
            if (vif.pw_valid && vif.pw_ready) begin

                op_id = vif.pw_tag[37:33];

                if (!(
                    ((op_id >= 2) &&
                     (op_id <= 26) &&
                     !op_id[0]) ||
                    (op_id == 27) ||
                    (op_id == 28)
                )) begin

                    `uvm_error("G02_MON", $sformatf("Unexpected PW OP ID: %0d", op_id))

                end else begin

                    tx_count[op_id]++;
                    elem_count[op_id] += popcount4(vif.pw_mask);
                    $fdisplay(get_dump_fd(op_id), "%016h %01h %016h", vif.pw_tag, vif.pw_mask,
                                  vif.pw_data);
                end

            end

        end

    endtask

    function automatic integer get_dump_fd(int unsigned op_id);

        string path;

        if (dump_fd[op_id] == 0) begin

            path = $sformatf("%s/op_%02d.hex", dump_dir, op_id);

            dump_fd[op_id] = $fopen(path, "w");

            if (dump_fd[op_id] == 0) begin
                `uvm_fatal("G02_DUMP", $sformatf("Cannot open checkpoint dump: %s", path))
            end

        end

        return dump_fd[op_id];

    endfunction
    function void report_phase(uvm_phase phase);

        longint unsigned expected;
        int unsigned pass_count;

        super.report_phase(phase);

        pass_count = 0;

        for (int op_id = 0; op_id < 29; op_id++) begin

            expected = expected_elements(op_id);

            if (elem_count[op_id] == expected) begin

                pass_count++;

                `uvm_info("G02_COUNT", $sformatf("OP%02d PASS tx=%0d elements=%0d expected=%0d",
                                                 op_id, tx_count[op_id], elem_count[op_id],
                                                 expected), UVM_LOW)

            end else begin

                `uvm_error("G02_COUNT", $sformatf(
                           "OP%02d FAIL tx=%0d elements=%0d expected=%0d",
                           op_id,
                           tx_count[op_id],
                           elem_count[op_id],
                           expected
                           ))

            end

        end

        `uvm_info("G02_COUNT", $sformatf("G02 checkpoint count summary: %0d/29 OP PASS", pass_count
                  ), UVM_NONE)

    endfunction
    function void final_phase(uvm_phase phase);

        super.final_phase(phase);

        foreach (dump_fd[i]) begin

            if (dump_fd[i] != 0) begin
                $fclose(dump_fd[i]);
                dump_fd[i] = 0;
            end

        end

    endfunction
endclass
