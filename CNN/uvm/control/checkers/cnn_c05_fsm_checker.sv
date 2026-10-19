class cnn_c05_fsm_checker extends uvm_component;
    `uvm_component_utils(cnn_c05_fsm_checker)

    // The analysis_imp calls write() for every monitor event. Only control
    // order and handshakes are checked; CNN numerical data is out of scope.
    uvm_analysis_imp #(cnn_c05_event_item, cnn_c05_fsm_checker) imp;
    bit started, published, run_complete;
    bit [15:0] stage_hits;
    bit [28:0] op_hits;
    int descriptor_count;
    int transition_count;

    function new(string name, uvm_component parent);
        super.new(name, parent);
        imp = new("imp", this);
    endfunction

    function int expected_op(int stage_index, bit part);
        if (stage_index == 0) return 0;
        if (stage_index == 14) return 27;
        if (stage_index == 15) return 28;
        return 2 * stage_index - 1 + int'(part);
    endfunction

    function void write(cnn_c05_event_item t);
        if (t.state == 1 && t.previous_state == 0) begin
            started = 1;
            descriptor_count = 0;
            stage_hits = '0;
            op_hits = '0;
            run_complete = 0;
        end
        if (!started) return;

        if (t.stage != t.previous_stage) begin
            if (t.stage != t.previous_stage + 1 ||
                t.previous_state != 14 || !run_complete)
                `uvm_error("C05_STAGE_ORDER",
                           $sformatf("Unexpected stage %0d -> %0d, prior state=%0d run_complete=%0b",
                                     t.previous_stage, t.stage, t.previous_state, run_complete))
            if (descriptor_count != ((t.previous_stage >= 1 && t.previous_stage <= 13) ? 2 : 1))
                `uvm_error("C05_DESC_COUNT", $sformatf("Stage %0d had %0d descriptor requests",
                                                        t.previous_stage, descriptor_count))
            descriptor_count = 0;
            run_complete = 0;
            transition_count++;
        end

        if (t.state == 2 && t.previous_state != 2) begin
            int expected_part;
            expected_part = descriptor_count;
            if (t.stage > 15 || expected_part >= ((t.stage >= 1 && t.stage <= 13) ? 2 : 1))
                `uvm_error("C05_DESC_ORDER", $sformatf("Extra descriptor request stage=%0d part=%0d",
                                                      t.stage, expected_part))
            else if (t.load_part != bit'(expected_part) ||
                     t.requested_op != expected_op(t.stage, bit'(expected_part)))
                `uvm_error("C05_OP_ORDER", $sformatf("Stage=%0d part=%0d expected op=%0d actual op=%0d load_part=%0b",
                                                    t.stage, expected_part,
                                                    expected_op(t.stage, bit'(expected_part)),
                                                    t.requested_op, t.load_part))
            descriptor_count++;
            stage_hits[t.stage] = 1;
            op_hits[t.requested_op] = 1;
        end

        // cfg_seen is the registered accumulation including the handshake
        // on the edge that left MODULE_CFG.
        if (t.previous_state == 9 && t.state == 10 &&
            (t.cfg_seen & t.cfg_required) != t.cfg_required)
            `uvm_error("C05_CFG_BARRIER", $sformatf("Stage=%0d seen=%02h required=%02h",
                                                   t.stage, t.cfg_seen, t.cfg_required))

        if (t.previous_state == 11 && t.state != 11) begin
            if (!(t.state == 12 || t.state == 14) || !t.stage_complete)
                `uvm_error("C05_RUN_EXIT", $sformatf("Stage=%0d RUN exit=%0d complete=%0b",
                                               t.stage, t.state, t.stage_complete))
            else run_complete = 1;
        end
        if (t.previous_state == 14 && t.stage == 15 && t.state == 15 &&
            descriptor_count != 1)
            `uvm_error("C05_FINAL_DESC", "Stage 15 did not fetch exactly one descriptor")
        if (t.previous_state == 16 && t.state == 0) begin
            if (t.stage != 15 || !t.done_pending || t.busy || t.error_pending)
                `uvm_error("C05_PUBLISH", $sformatf("Bad publish state: stage=%0d done=%0b busy=%0b error=%0b",
                                                   t.stage, t.done_pending, t.busy, t.error_pending))
            published = 1;
        end
    endfunction

    function void report_phase(uvm_phase phase);
        super.report_phase(phase);
        if (!published || stage_hits != 16'hffff || op_hits != 29'h1fff_ffff ||
            transition_count != 15)
            `uvm_error("C05_INCOMPLETE", $sformatf("published=%0b stages=%04h ops=%08h transitions=%0d",
                                                  published, stage_hits, op_hits, transition_count))
        `uvm_info("C05_CHECKER", $sformatf("published=%0b stages=%04h ops=%08h transitions=%0d",
                                         published, stage_hits, op_hits, transition_count), UVM_LOW)
    endfunction
endclass
