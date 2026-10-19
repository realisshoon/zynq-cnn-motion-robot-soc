class cnn_dma_s07_completion_race_test extends cnn_dma_s01_no_stall_test;
    `uvm_component_utils(cnn_dma_s07_completion_race_test)
    function new(string name,uvm_component parent=null); super.new(name,parent); endfunction
    function void build_phase(uvm_phase phase);
        super.build_phase(phase); cfg.scenario_id=DMA_SCENARIO_S07;
        cfg.dma_complete_latency=1;
    endfunction
    task run_phase(uvm_phase phase);
        cnn_dma_command_checker c;
        phase.raise_objection(this); super.run_phase(phase); c=dma_env.dma_scb.commands;
        if ((c.dma_done_time>0 && c.final_output_time>c.dma_done_time &&
            c.dst_arm_time[0]>0 && c.source_start_time[0]>c.dst_arm_time[0] &&
            c.output_packets==1 && cfg.output_last_accepts==1 && c.errors==0) !== 1'b1)
            `uvm_error("S07_RACE", "Missing observed early DMA completion, arm order or final drain")
        `uvm_info("S07 SCOREBOARD SUMMARY",$sformatf(
            "S2MM_arm=%0t image_start=%0t DMA_Idle_observed=%0t final_stream_accept=%0t feature_out=%0d LAST_accept=%0d errors=%0d scope=stage0",
            c.dst_arm_time[0],c.source_start_time[0],c.dma_done_time,c.final_output_time,
            dma_env.dma_scb.feature_out_beats,cfg.output_last_accepts,c.errors),UVM_NONE)
        phase.drop_objection(this);
    endtask
endclass
