// Small fault-priority reference model. Source flags are supplied from the
// injector's chosen inputs, while code/status/lock come from DUT observation.
class cnn_c08_fault_checker extends uvm_component;
    `uvm_component_utils(cnn_c08_fault_checker)

    int sampled_fault_type, sampled_scenario, sampled_recovery;
    bit [5:0] fault_hits;
    bit [2:0] scenario_hits, recovery_hits;
    bit [31:0] locked_code;
    bit have_locked_code;

    covergroup fault_cg;
        option.per_instance = 1;
        cp_type: coverpoint sampled_fault_type {
            bins axi_response = {0}; bins dma_status = {1};
            bins descriptor = {2}; bins module_input = {3};
            bins watchdog = {4}; bins swap_stream = {5};
        }
        cp_scenario: coverpoint sampled_scenario {
            bins standalone = {0}; bins simultaneous = {1};
            bins secondary_after_lock = {2};
        }
    endgroup
    covergroup recovery_cg;
        option.per_instance = 1;
        cp_recovery: coverpoint sampled_recovery {
            bins clear_error = {0}; bins soft_reset = {1}; bins hard_reset = {2};
        }
    endgroup

    function new(string name, uvm_component parent);
        super.new(name, parent);
        fault_cg = new();
        recovery_cg = new();
    endfunction

    function bit [31:0] priority_code(bit axi_error, bit status_error,
                                      bit descriptor_or_swap, bit module_error,
                                      bit watchdog_error);
        if (axi_error) return 32'h2;
        if (status_error) return 32'h4;
        if (descriptor_or_swap) return 32'h10;
        if (module_error) return 32'h1;
        if (watchdog_error) return 32'h8;
        return 0;
    endfunction

    function void check_fault(string label, int fault_type, int scenario,
                              bit axi_error, bit status_error,
                              bit descriptor_or_swap, bit module_error,
                              bit watchdog_error, bit [31:0] code,
                              bit [31:0] status, bit lock_value);
        bit [31:0] expected;
        expected = priority_code(axi_error, status_error,
                                 descriptor_or_swap, module_error,
                                 watchdog_error);
        if (expected == 0 || code != expected || status[2:0] != 3'b100 ||
            !lock_value)
            `uvm_error("C08_FAULT_CHECK", $sformatf("%s: expected=%08h actual=%08h status=%08h lock=%0b",
                                                  label, expected, code, status, lock_value))
        else
            `uvm_info("C08_FAULT_CHECK", $sformatf("%s: code=%08h status=%08h lock=%0b",
                                                 label, code, status, lock_value), UVM_LOW)
        if (!have_locked_code) begin
            locked_code = code;
            have_locked_code = 1;
        end
        sampled_fault_type = fault_type;
        sampled_scenario = scenario;
        fault_hits[fault_type] = 1;
        scenario_hits[scenario] = 1;
        fault_cg.sample();
    endfunction

    function void check_retained(string label, bit [31:0] code);
        if (!have_locked_code || code != locked_code)
            `uvm_error("C08_RETENTION", $sformatf("%s: original=%08h actual=%08h",
                                              label, locked_code, code))
        sampled_scenario = 2;
        scenario_hits[2] = 1;
        fault_cg.sample();
    endfunction

    function void record_recovery(int recovery_kind);
        sampled_recovery = recovery_kind;
        recovery_hits[recovery_kind] = 1;
        recovery_cg.sample();
        if (recovery_kind != 0) begin
            have_locked_code = 0;
            locked_code = 0;
        end
    endfunction

    function void report_phase(uvm_phase phase);
        super.report_phase(phase);
        `uvm_info("C08_COVERAGE",
                  $sformatf("fault_types=%06b scenarios=%03b recoveries=%03b fault_coverage=%0.1f%% recovery_coverage=%0.1f%%",
                            fault_hits, scenario_hits, recovery_hits,
                            fault_cg.get_coverage(), recovery_cg.get_coverage()), UVM_LOW)
    endfunction
endclass
