// Additional S04 variants exercise actual feature MM2S traffic, alongside
// the preserved four stage0 runs with their exact 120/69120/49152 anchors.
class cnn_dma_s04_full_flow_test extends cnn_dma_s08_fm_mapping_test;
    `uvm_component_utils(cnn_dma_s04_full_flow_test)
    function new(string name,uvm_component parent=null); super.new(name,parent); endfunction
    function void build_phase(uvm_phase phase);
        super.build_phase(phase); cfg.scenario_id=DMA_SCENARIO_S04;
        cfg.random_stall_enable=1; cfg.output_stall_enable=1;
        cfg.weight_gap_enable=1; cfg.image_gap_enable=1; cfg.feature_gap_enable=1;
        void'($value$plusargs("DMA_SEED=%d",cfg.random_seed));
        if(cfg.random_seed==0) `uvm_fatal("S04_SEED", "Use a nonzero seed")
    endfunction
    task run_phase(uvm_phase phase);
        phase.raise_objection(this); super.run_phase(phase);
        if(cfg.gap_events[0]==0 || cfg.gap_events[1]==0 || cfg.gap_events[2]==0 ||
            cfg.normal_stalls==0 || cfg.simultaneous_cycles==0 || cfg.output_last_accepts!=14 ||
            cfg.recoveries!=cfg.normal_stalls+cfg.last_stalls)
            `uvm_error("S04_FULL_RANDOM", "Missing observed stream disturbance/completion/recovery")
        `uvm_info("S04 SCOREBOARD SUMMARY",$sformatf(
            "seed=%0d scope=stage0-15 weight_gap=%0d image_gap=%0d feature_gap=%0d output_stalls=%0d simultaneous_cycles=%0d LAST_accepts=%0d checker_errors=%0d",
            cfg.random_seed,cfg.gap_events[0],cfg.gap_events[1],cfg.gap_events[2],cfg.recoveries,
            cfg.simultaneous_cycles,cfg.output_last_accepts,dma_env.dma_scb.commands.errors),UVM_NONE)
        phase.drop_objection(this);
    endtask
endclass
