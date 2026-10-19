class cnn_c05_fsm_coverage extends uvm_subscriber #(cnn_c05_event_item);
    `uvm_component_utils(cnn_c05_fsm_coverage)

    bit [4:0] sampled_state, sampled_op;
    bit [3:0] sampled_stage;
    bit sampled_part, sampled_cfg_exit, sampled_run_exit, sampled_publish;
    bit [15:0] stage_hits;
    bit [28:0] op_hits;
    int cfg_exits, run_exits, publish_events;

    covergroup cg;
        option.per_instance = 1;
        // Every stage and ROM operation is a distinct required control step.
        cp_stage: coverpoint sampled_stage iff (sampled_state == 2) {
            bins stages[] = {[0:15]};
        }
        cp_op: coverpoint sampled_op iff (sampled_state == 2) {
            bins operations[] = {[0:28]};
        }
        // The body stages have both depthwise (0) and pointwise (1) loads.
        cp_part: coverpoint sampled_part iff (sampled_state == 2 &&
                                             sampled_stage inside {[1:13]}) {
            bins depthwise = {0};
            bins pointwise = {1};
        }
        // These bins show that config, run completion, and publish occurred.
        cp_cfg_exit: coverpoint sampled_cfg_exit { bins accepted = {1}; }
        cp_run_exit: coverpoint sampled_run_exit { bins completed = {1}; }
        cp_publish: coverpoint sampled_publish { bins published = {1}; }
    endgroup

    function new(string name, uvm_component parent);
        super.new(name, parent);
        cg = new();
    endfunction

    function void write(cnn_c05_event_item t);
        sampled_state = t.state;
        sampled_stage = t.stage;
        sampled_op = t.requested_op;
        sampled_part = t.load_part;
        sampled_cfg_exit = t.previous_state == 9 && t.state == 10;
        sampled_run_exit = t.previous_state == 11 && (t.state == 12 || t.state == 14);
        sampled_publish = t.previous_state == 16 && t.state == 0;
        if (t.state == 2 && t.stage <= 15) stage_hits[t.stage] = 1;
        if (t.state == 2 && t.requested_op <= 28) op_hits[t.requested_op] = 1;
        if (sampled_cfg_exit) cfg_exits++;
        if (sampled_run_exit) run_exits++;
        if (sampled_publish) publish_events++;
        cg.sample();
    endfunction

    function void report_phase(uvm_phase phase);
        super.report_phase(phase);
        `uvm_info("C05_COVERAGE",
                  $sformatf("stage_bins=%04h op_bins=%08h cfg_exits=%0d run_exits=%0d publish=%0d coverage=%0.1f%%",
                            stage_hits, op_hits, cfg_exits, run_exits,
                            publish_events, cg.get_coverage()), UVM_LOW)
    endfunction
endclass
