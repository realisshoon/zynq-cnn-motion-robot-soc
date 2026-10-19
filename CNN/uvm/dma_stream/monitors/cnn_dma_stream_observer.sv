// Passive stall accounting and a separate pre-accept TLAST sink helper.
class cnn_dma_stream_observer extends uvm_component;
    `uvm_component_utils(cnn_dma_stream_observer)
    virtual cnn_if vif;
    cnn_dma_stream_cfg cfg;
    cnn_dma_stream_coverage cov;
    function new(string name, uvm_component parent); super.new(name,parent); endfunction
    function void build_phase(uvm_phase phase);
        super.build_phase(phase);
        if (!uvm_config_db#(virtual cnn_if)::get(this,"","vif",vif) ||
            !uvm_config_db#(cnn_dma_stream_cfg)::get(this,"","dma_stream_cfg",cfg))
            `uvm_fatal("DMA_OBSERVER", "Missing configuration")
    endfunction
    function void connect_phase(uvm_phase phase);
        uvm_component owner;
        owner = get_parent();
        if (!$cast(cov, owner.get_child("cov")))
            `uvm_fatal("DMA_OBSERVER", "Missing coverage component")
    endfunction
    task observe();
        int cycles;
        bit last_beat;
        forever begin
            @(vif.mon_cb);
            if (!vif.rst_n) continue;
            if (vif.mon_cb.m_feature_valid && !vif.mon_cb.m_feature_ready) begin
                if (cycles == 0) last_beat = vif.mon_cb.m_feature_last;
                cycles++;
                cfg.observed_stall_cycles++;
                if (cfg.gap_active[0] || cfg.gap_active[1] || cfg.gap_active[2]) cfg.simultaneous_cycles++;
            end
            if (vif.mon_cb.m_feature_valid && vif.mon_cb.m_feature_ready) begin
                cfg.output_accepts++;
                if (vif.mon_cb.m_feature_last) cfg.output_last_accepts++;
                if (cycles != 0) begin
                    if (last_beat) cfg.last_stalls++; else cfg.normal_stalls++;
                    cfg.recoveries++;
                    cov.backpressure_cg.sample(last_beat,cycles,1);
                    if (cfg.scenario_id==DMA_SCENARIO_S04)
                        cov.random_stall_cg.sample(2,cycles,1);
                    cycles=0;
                end
            end
        end
    endtask
    task boundary_sink();
        int cycles;
        if (cfg.scenario_id != DMA_SCENARIO_S03) return;
        void'($value$plusargs("BOUNDARY_STALL=%d",cfg.boundary_stall_cycles));
        if (!(cfg.boundary_stall_cycles inside {[1:3]}))
            `uvm_fatal("DMA_BOUNDARY", "BOUNDARY_STALL must be 1..3")
        // READY is changed at negedge after the penultimate acceptance.
        do @(negedge vif.clk); while (cfg.output_accepts != 49151);
        vif.m_feature_ready=0;
        // Count actual LAST stall cycles. No prior acceptance of this beat.
        do begin
            @(vif.mon_cb);
            if (vif.mon_cb.m_feature_valid && vif.mon_cb.m_feature_last &&
                !vif.mon_cb.m_feature_ready) cycles++;
        end while (cycles < cfg.boundary_stall_cycles);
        @(negedge vif.clk);
        vif.m_feature_ready=1;
    endtask
    task random_sink();
        int unsigned rng;
        int length;
        if (!cfg.random_stall_enable) return;
        rng=cfg.random_seed ^ 32'h9e3779b9;
        wait(vif.rst_n===1);
        forever begin
            @(negedge vif.clk);
            rng=rng ^ (rng << 13); rng=rng ^ (rng >> 17); rng=rng ^ (rng << 5);
            if (vif.m_feature_valid && rng%16==0) begin
                length=1+(rng/16)%3;
                vif.m_feature_ready=0;
                repeat(length) @(negedge vif.clk);
                vif.m_feature_ready=1;
                // At least one accepting edge before another disturbance.
                @(negedge vif.clk);
            end
        end
    endtask
    task run_phase(uvm_phase phase);
        fork observe(); boundary_sink(); random_sink(); join
    endtask
endclass
