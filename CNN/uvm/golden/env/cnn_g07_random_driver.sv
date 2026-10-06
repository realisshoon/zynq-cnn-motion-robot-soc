class cnn_g07_stall_config extends uvm_object;
    `uvm_object_utils(cnn_g07_stall_config)
    rand int unsigned low_cycles, high_cycles;
    constraint bounded { low_cycles inside {[0:12]}; high_cycles inside {[32:128]}; }
    function new(string name = "stall_config"); super.new(name); endfunction
endclass

// The inherited driver remains the sole owner of m_feature_ready. Its init
// completes before the dedicated sink process starts. Input sequence is unchanged.
class cnn_g07_random_driver extends cnn_driver;
    `uvm_component_utils(cnn_g07_random_driver)
    cnn_g07_stall_config timing;
    int unsigned stall_seed = 1;
    longint unsigned stalls_observed, accepted, stable_checks, schedule_count;
    bit [31:0] schedule_hash = 32'h811c9dc5;
    int fd;
    string stimulus_path;
    function new(string name, uvm_component parent); super.new(name, parent); endfunction
    function void build_phase(uvm_phase phase);
        super.build_phase(phase);
        void'($value$plusargs("STALL_SEED=%d", stall_seed));
        timing = cnn_g07_stall_config::type_id::create("timing");
        timing.srandom(stall_seed);
        if ($value$plusargs("STALL_TRACE=%s", stimulus_path)) begin
            fd = $fopen(stimulus_path, "w");
            if (!fd) `uvm_fatal("G07", "Cannot open stimulus trace")
            $fdisplay(fd, "time_ps,low_cycles,high_cycles");
        end
    endfunction
    task drive_random_sink();
        wait (vif.rst_n === 1'b1);
        repeat (3) @(negedge vif.clk);
        forever begin
            // Random high intervals create both idle/ready and active/ready.
            if (!timing.randomize()) `uvm_fatal("G07", "Stall randomization failed")
            schedule_count++;
            schedule_hash = (schedule_hash ^ timing.low_cycles) * 32'h01000193;
            schedule_hash = (schedule_hash ^ timing.high_cycles) * 32'h01000193;
            if (fd) $fdisplay(fd, "%0t,%0d,%0d", $time, timing.low_cycles, timing.high_cycles);
            repeat (timing.high_cycles) @(negedge vif.clk);
            if (timing.low_cycles != 0) begin
                // Negedge updates avoid races with posedge transfer observation.
                vif.m_feature_ready = 0;
                repeat (timing.low_cycles) @(negedge vif.clk);
                vif.m_feature_ready = 1;
            end
        end
    endtask
    task check_stream();
        bit held;
        logic [63:0] saved_data;
        logic [7:0] saved_keep;
        logic saved_last;
        int length;
        forever begin
            @(vif.mon_cb);
            if (!vif.rst_n) begin held = 0; length = 0; continue; end
            if (held) begin
                stable_checks++;
                if (vif.mon_cb.m_feature_valid !== 1'b1 ||
                    vif.mon_cb.m_feature_data !== saved_data ||
                    vif.mon_cb.m_feature_keep !== saved_keep ||
                    vif.mon_cb.m_feature_last !== saved_last)
                    `uvm_error("G07_STABILITY", "VALID/data/keep/last changed before stalled transfer accepted")
            end
            if (vif.mon_cb.m_feature_valid && !vif.mon_cb.m_feature_ready) begin
                if (!held) begin
                    saved_data = vif.mon_cb.m_feature_data;
                    saved_keep = vif.mon_cb.m_feature_keep;
                    saved_last = vif.mon_cb.m_feature_last;
                    if (stalls_observed < 8)
                        `uvm_info("G07_WINDOW", $sformatf("STALL START time_ps=%0t data=%016h keep=%02h last=%0d", $time, saved_data, saved_keep, saved_last), UVM_LOW)
                end
                held = 1; length++;
            end else if (vif.mon_cb.m_feature_valid && vif.mon_cb.m_feature_ready) begin
                accepted++;
                golden_cov.sample_stall(length);
                if (held) begin
                    if (stalls_observed < 8)
                        `uvm_info("G07_WINDOW", $sformatf("STALL ACCEPT time_ps=%0t length=%0d data=%016h", $time, length, vif.mon_cb.m_feature_data), UVM_LOW)
                    stalls_observed++;
                end
                held = 0; length = 0;
            end
        end
    endtask
    task run_phase(uvm_phase phase);
        fork
            super.run_phase(phase);
            drive_random_sink();
            check_stream();
        join
    endtask
    function void report_phase(uvm_phase phase);
        if (!stalls_observed || !stable_checks || !accepted)
            `uvm_error("G07", "No actual stalled handshakes / stability checks / transfers observed")
        `uvm_info("G07", $sformatf("seed=%0d stalls=%0d accepted=%0d stability_checks=%0d schedules=%0d stimulus_hash=%08h", stall_seed, stalls_observed, accepted, stable_checks, schedule_count, schedule_hash), UVM_LOW)
        if (fd) $fclose(fd);
    endfunction
endclass
