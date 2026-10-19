class cnn_dma_s05_axil_protocol_test extends cnn_dma_s01_no_stall_test;
    `uvm_component_utils(cnn_dma_s05_axil_protocol_test)
    function new(string name,uvm_component parent=null); super.new(name,parent); endfunction
    function void build_phase(uvm_phase phase);
        super.build_phase(phase); cfg.scenario_id=DMA_SCENARIO_S05;
        cfg.dma_ar_delay=2; cfg.axil_order_cycle=1; cfg.dma_read_latency=3; cfg.dma_write_latency=3;
        void'($value$plusargs("DMA_READ_LATENCY=%d",cfg.dma_read_latency));
        void'($value$plusargs("DMA_WRITE_LATENCY=%d",cfg.dma_write_latency));
    endfunction
    task run_phase(uvm_phase phase);
        phase.raise_objection(this); super.run_phase(phase);
        if (dma_env.dma_mon.aw_stall_cycles==0 || dma_env.dma_mon.w_stall_cycles==0 ||
            dma_env.dma_mon.ar_stall_cycles==0)
            `uvm_error("S05_STALL", "AW/W/AR hold assertions were not exercised")
        if (dma_env.dma_scb.aw_first_count==0 || dma_env.dma_scb.w_first_count==0 ||
            dma_env.dma_scb.same_cycle_count==0 || dma_env.dma_scb.dma_read_count==0 ||
            dma_env.dma_scb.bresp_errors!=0 || dma_env.dma_scb.rresp_errors!=0)
            `uvm_error("S05_PROTOCOL", "Missing handshake order/access or response error")
        `uvm_info("S05 SCOREBOARD SUMMARY",$sformatf(
            "writes=%0d reads=%0d AW-first=%0d W-first=%0d same-cycle=%0d BRESP_error=%0d RRESP_error=%0d AW_stall=%0d W_stall=%0d AR_stall=%0d",
            dma_env.dma_scb.dma_write_count,dma_env.dma_scb.dma_read_count,
            dma_env.dma_scb.aw_first_count,dma_env.dma_scb.w_first_count,
            dma_env.dma_scb.same_cycle_count,dma_env.dma_scb.bresp_errors,dma_env.dma_scb.rresp_errors,
            dma_env.dma_mon.aw_stall_cycles,dma_env.dma_mon.w_stall_cycles,dma_env.dma_mon.ar_stall_cycles),UVM_NONE)
        phase.drop_objection(this);
    endtask
endclass
