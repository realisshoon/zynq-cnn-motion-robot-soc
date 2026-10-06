class cnn_dma_s08_fm_mapping_test extends cnn_dma_stream_base_test;
    `uvm_component_utils(cnn_dma_s08_fm_mapping_test)
    function new(string name,uvm_component parent=null); super.new(name,parent); endfunction
    function void build_phase(uvm_phase phase);
        super.build_phase(phase); cfg.scenario_id=DMA_SCENARIO_S08; cfg.full_flow_enable=1; cfg.dma_complete_latency=2;
    endfunction
    task run_phase(uvm_phase phase);
        cnn_dma_s08_full_flow_seq seq;
        cnn_dma_command_checker c;
        phase.raise_objection(this);
        seq=cnn_dma_s08_full_flow_seq::type_id::create("seq"); seq.set_cfg(cfg);
        seq.vif=dma_env.feeder.vif; seq.start(dma_env.agt.sqr);
        c=dma_env.dma_scb.commands;
        for(int stage=0;stage<16;stage++) begin
            if(stage>0 && (!c.src_addr_seen[stage] || c.actual_src[stage]!=c.ref_model.get_expected_fm_src(stage)))
                `uvm_error("S08_MAPPING",$sformatf("Missing/mismatched Stage%0d source",stage))
            if(stage<14 && (!c.dst_addr_seen[stage] || c.actual_dst[stage]!=c.ref_model.get_expected_fm_dst(stage)))
                `uvm_error("S08_MAPPING",$sformatf("Missing/mismatched Stage%0d destination",stage))
            if(stage>=14 && c.dst_addr_seen[stage]) `uvm_error("S08_MAPPING", "Head has body destination")
            `uvm_info("S08_MAPPING_TABLE",$sformatf(
                "stage=%02d actual_src=0x%08h expected_src=0x%08h actual_dst=0x%08h expected_dst=0x%08h",
                stage,c.actual_src[stage],c.ref_model.get_expected_fm_src(stage),
                c.actual_dst[stage],c.ref_model.get_expected_fm_dst(stage)),UVM_NONE)
        end
        if(!seq.done_seen || !seq.final_status[3] || seq.final_status[2] || c.weight_packets!=29 || c.feature_in_packets!=15 ||
            c.output_packets!=14 || c.image_packets!=144 || c.image_packet_beats!=0 ||
            c.weight_packet_beats!=0 || c.feature_in_packet_beats!=0 || c.output_packet_beats!=0 ||
            c.mapping_mismatches!=0 || c.errors!=0)
            `uvm_error("S08_FULL_FLOW", "Full-flow completion/packet/mapping check failed")
        `uvm_info("S08 SCOREBOARD SUMMARY",$sformatf(
            "stages=16 weight_packets=%0d feature_in_packets=%0d feature_out_packets=%0d image_rows=%0d FM_mapping_mismatches=%0d errors=%0d STATUS=0x%08h",
            c.weight_packets,c.feature_in_packets,c.output_packets,c.image_packets,c.mapping_mismatches,c.errors,seq.final_status),UVM_NONE)
        phase.drop_objection(this);
    endtask
endclass
