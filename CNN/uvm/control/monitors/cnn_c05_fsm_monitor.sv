class cnn_c05_fsm_monitor extends uvm_monitor;
    `uvm_component_utils(cnn_c05_fsm_monitor)

    virtual cnn_if vif;
    // One producer may feed both correctness and coverage independently.
    uvm_analysis_port #(cnn_c05_event_item) ap;

    localparam string FSM_PATH = "tb_top.dut.u_top_level_fsm.";

    function new(string name, uvm_component parent);
        super.new(name, parent);
        ap = new("ap", this);
    endfunction

    function void build_phase(uvm_phase phase);
        super.build_phase(phase);
        if (!uvm_config_db#(virtual cnn_if)::get(this, "", "vif", vif))
            `uvm_fatal("C05_MON_VIF", "cnn_if not found")
    endfunction

    function bit read_signal(string name, output uvm_hdl_data_t value);
        if (!uvm_hdl_read({FSM_PATH, name}, value)) begin
            `uvm_error("C05_MON_PATH", $sformatf("Cannot observe %s%s", FSM_PATH, name))
            return 0;
        end
        return 1;
    endfunction

    task run_phase(uvm_phase phase);
        uvm_hdl_data_t value;
        bit [4:0] state_now, state_before;
        bit [3:0] stage_now, stage_before;
        bit first_sample;
        cnn_c05_event_item event_item;

        first_sample = 1;
        wait (vif.rst_n === 1'b1);
        forever begin
            @(posedge vif.clk);
            if (!vif.rst_n) begin
                first_sample = 1;
                continue;
            end
            // uvm_hdl_read observes existing DUT state only. The monitor
            // never forces or drives a signal. Other fields are fetched only
            // on an event to keep a long frame simulation inexpensive.
            if (!read_signal("state", value)) continue;
            state_now = value[4:0];
            if (!read_signal("stage", value)) continue;
            stage_now = value[3:0];
            if (!first_sample && state_now == state_before &&
                stage_now == stage_before) continue;

            event_item = cnn_c05_event_item::type_id::create("event_item");
            event_item.state = state_now;
            event_item.stage = stage_now;
            event_item.previous_state = first_sample ? state_now : state_before;
            event_item.previous_stage = first_sample ? stage_now : stage_before;
            if (read_signal("requested_op", value)) event_item.requested_op = value[4:0];
            if (read_signal("load_part", value)) event_item.load_part = value[0];
            if (read_signal("cfg_seen", value)) event_item.cfg_seen = value[6:0];
            if (read_signal("cfg_required", value)) event_item.cfg_required = value[6:0];
            if (read_signal("stage_complete", value)) event_item.stage_complete = value[0];
            if (read_signal("busy_r", value)) event_item.busy = value[0];
            if (read_signal("done_pending_r", value)) event_item.done_pending = value[0];
            if (read_signal("error_pending_r", value)) event_item.error_pending = value[0];
            ap.write(event_item);

            state_before = state_now;
            stage_before = stage_now;
            first_sample = 0;
        end
    endtask
endclass
