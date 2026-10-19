class cnn_dma_s04_random_stall_test extends cnn_dma_s01_no_stall_test;
    `uvm_component_utils(cnn_dma_s04_random_stall_test)
    function new(string name,uvm_component parent=null); super.new(name,parent); endfunction
    function void build_phase(uvm_phase phase);
        cnn_driver::type_id::set_type_override(cnn_dma_random_driver::get_type());
        super.build_phase(phase);
        cfg.scenario_id=DMA_SCENARIO_S04;
        cfg.weight_gap_enable=1; cfg.image_gap_enable=1;
        cfg.output_stall_enable=1; cfg.random_stall_enable=1;
        void'($value$plusargs("DMA_SEED=%d",cfg.random_seed));
        if (cfg.random_seed==0) `uvm_fatal("S04_SEED", "Use a nonzero seed")
    endfunction
    task run_phase(uvm_phase phase);
        phase.raise_objection(this);
        super.run_phase(phase);
        if (cfg.gap_events[0]==0 || cfg.gap_events[1]==0 ||
            cfg.normal_stalls==0 || cfg.simultaneous_cycles==0 ||
            cfg.recoveries != cfg.normal_stalls+cfg.last_stalls || cfg.output_last_accepts!=1)
            `uvm_error("S04_OBSERVED", "Missing random disturbance, overlap, recovery or final LAST")
        `uvm_info("S04 SCOREBOARD SUMMARY",$sformatf(
            "seed=%0d weight=%0d image=%0d feature_out=%0d weight_gap=%0d image_gap=%0d random_stall_events=%0d simultaneous_cycles=%0d recovery=%0d streams=weight,image,output(stage0)",
            cfg.random_seed,dma_env.dma_scb.weight_beats,dma_env.dma_scb.image_beats,
            dma_env.dma_scb.feature_out_beats,cfg.gap_events[0],cfg.gap_events[1],
            cfg.normal_stalls+cfg.last_stalls,cfg.simultaneous_cycles,cfg.recoveries),UVM_NONE)
        phase.drop_objection(this);
    endtask
endclass
