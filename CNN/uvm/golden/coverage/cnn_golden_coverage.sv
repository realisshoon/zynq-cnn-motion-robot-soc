// Scenario bins are sampled only after the existing bit-exact checker succeeds.
class cnn_golden_coverage extends uvm_component;
    `uvm_component_utils(cnn_golden_coverage)
    virtual cnn_if vif;
    virtual cnn_g02_probe_if op_vif;
    string test_name;
    bit op_seen[29];
    int scenario, op_id, threshold_case, frame_id, progression, bank_transition;
    int stall_length, handshake;
    bit stalled;

    covergroup scenarios;
        option.per_instance = 1;
        cp_scenario: coverpoint scenario { bins G01 = {1}; bins G02 = {2};
            bins G03 = {3}; bins G04 = {4}; bins G05 = {5};
            bins G06 = {6}; bins G07 = {7}; }
    endgroup
    covergroup checkpoints;
        option.per_instance = 1;
        cp_op: coverpoint op_id { bins operations[] = {[0:28]}; }
    endgroup
    covergroup thresholds;
        option.per_instance = 1;
        cp_threshold: coverpoint threshold_case { bins equal_valid = {0}; bins plus_one_invalid = {1}; }
    endgroup
    covergroup frames;
        option.per_instance = 1;
        cp_frame: coverpoint frame_id { bins samples[] = {[121:125]}; }
    endgroup
    covergroup sequences;
        option.per_instance = 1;
        cp_progression: coverpoint progression { bins seq_1 = {1}; bins seq_2 = {2}; bins seq_3 = {3}; }
    endgroup
    covergroup publishing;
        option.per_instance = 1;
        cp_bank: coverpoint bank_transition { bins bank_0_to_1 = {1}; bins bank_1_to_0 = {2}; }
    endgroup
    covergroup stalls;
        option.per_instance = 1;
        cp_occurred: coverpoint stalled;
        cp_length: coverpoint stall_length { bins none = {0}; bins short = {[1:3]};
            bins medium_stall = {[4:7]}; bins long_stall = {[8:12]}; }
    endgroup
    covergroup handshakes;
        option.per_instance = 1;
        cp_valid_ready: coverpoint handshake { bins states[] = {[0:3]}; }
    endgroup

    function new(string name, uvm_component parent);
        super.new(name, parent);
        scenarios = new(); checkpoints = new(); thresholds = new(); frames = new();
        sequences = new(); publishing = new(); stalls = new(); handshakes = new();
    endfunction
    function void build_phase(uvm_phase phase);
        super.build_phase(phase);
        if (!uvm_config_db#(virtual cnn_if)::get(this, "", "vif", vif) ||
            !uvm_config_db#(virtual cnn_g02_probe_if)::get(this, "", "g02_vif", op_vif))
            `uvm_fatal("GOLDEN_COVERAGE", "Missing probes")
        void'($value$plusargs("UVM_TESTNAME=%s", test_name));
    endfunction
    function void sample_op(int id);
        if (id >= 0 && id < 29 && !op_seen[id]) begin
            op_seen[id] = 1; op_id = id; checkpoints.sample();
        end
    endfunction
    function void sample_result(int frame, int seq_id, bit [7:0] threshold, bit [31:0] joint0);
        case (test_name)
            "cnn_g01_e2e_test": scenario = 1;
            "cnn_g02_checkpoint_test": scenario = 2;
            "cnn_g03_threshold_test": scenario = 3;
            "cnn_g04_multiframe_test": scenario = 4;
            "cnn_g05_atomic_publish_test": scenario = 5;
            "cnn_g06_multisample_test": scenario = 6;
            "cnn_g07_random_stall_test": scenario = 7;
            default: scenario = 0;
        endcase
        scenarios.sample();
        if (scenario == 6) begin frame_id = frame; frames.sample(); end
        if (scenario == 4 && seq_id <= 3) begin progression = seq_id; sequences.sample(); end
        if (scenario == 3 && frame == 121) begin
            if (threshold == 98 && joint0 == 32'h620fe2bf) begin
                threshold_case = 0; thresholds.sample();
            end
            if (threshold == 99 && joint0 == 32'h62000000) begin
                threshold_case = 1; thresholds.sample();
            end
        end
    endfunction
    function void sample_publish(bit old_bank, bit new_bank);
        if (old_bank != new_bank) begin
            bank_transition = old_bank ? 2 : 1; publishing.sample();
        end
    endfunction
    function void sample_stall(int length);
        stall_length = length; stalled = (length != 0); stalls.sample();
    endfunction
    task run_phase(uvm_phase phase);
        forever begin
            @(vif.mon_cb);
            if (!vif.rst_n) continue;
            if (op_vif.conv0_valid && op_vif.conv0_ready) sample_op(op_vif.conv0_tag[37:33]);
            if (op_vif.dw_valid && op_vif.dw_ready) sample_op(op_vif.dw_tag[37:33]);
            if (op_vif.pw_valid && op_vif.pw_ready) sample_op(op_vif.pw_tag[37:33]);
            handshake = {vif.mon_cb.m_feature_valid, vif.mon_cb.m_feature_ready};
            handshakes.sample();
        end
    endtask
    function void report_phase(uvm_phase phase);
        `uvm_info("GOLDEN_COVERAGE", $sformatf("Scenario=%0.2f OP=%0.2f Threshold=%0.2f Frame=%0.2f Seq=%0.2f Publish=%0.2f Stall=%0.2f Handshake=%0.2f (per-run; use URG merge for final)",
            scenarios.get_inst_coverage(), checkpoints.get_inst_coverage(), thresholds.get_inst_coverage(),
            frames.get_inst_coverage(), sequences.get_inst_coverage(), publishing.get_inst_coverage(),
            stalls.get_inst_coverage(), handshakes.get_inst_coverage()), UVM_LOW)
    endfunction
endclass

cnn_golden_coverage golden_cov;
