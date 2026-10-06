class cnn_dma_s06_weight_command_test extends cnn_dma_s01_no_stall_test;
    `uvm_component_utils(cnn_dma_s06_weight_command_test)
    function new(string name,uvm_component parent=null); super.new(name,parent); endfunction
    function void build_phase(uvm_phase phase);
        super.build_phase(phase); cfg.scenario_id=DMA_SCENARIO_S06;
        cfg.dma_write_latency=3;
    endfunction
    task run_phase(uvm_phase phase);
        phase.raise_objection(this); super.run_phase(phase);
        if (dma_env.dma_scb.commands.weight_packets!=1 ||
            dma_env.dma_scb.commands.weight_packet_beats!=0 || dma_env.dma_scb.commands.errors!=0)
            `uvm_error("S06_COMMAND", "Conv0 command/packet checker failed")
        `uvm_info("S06 SCOREBOARD SUMMARY",$sformatf(
            "source=0x%08h(%0d) length=0x%08h(%0d bytes) weight=%0d packets=%0d LENGTH_B_commit_time=%0t errors=%0d",
            dma_env.dma_scb.commands.conv0_source,dma_env.dma_scb.commands.conv0_source,
            dma_env.dma_scb.commands.conv0_length,dma_env.dma_scb.commands.conv0_length,
            dma_env.dma_scb.weight_beats,dma_env.dma_scb.commands.weight_packets,
            dma_env.dma_scb.commands.conv0_commit_time,dma_env.dma_scb.commands.errors),UVM_NONE)
        phase.drop_objection(this);
    endtask
endclass
