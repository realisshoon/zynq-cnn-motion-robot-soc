`uvm_analysis_imp_decl(_feed)
// Emulates DMA stream delivery, not DDR memory movement. Only B-committed
// commands enter these mailboxes. Each stream has one independent worker.
class cnn_dma_stream_feeder extends uvm_component;
    `uvm_component_utils(cnn_dma_stream_feeder)
    uvm_analysis_imp_feed #(cnn_dma_event_item,cnn_dma_stream_feeder) imp;
    cnn_dma_stream_cfg cfg;
    virtual cnn_if vif;
    int weight_packets,feature_packets,image_packets;
    int unsigned rng[3];
    function new(string name,uvm_component parent);
        super.new(name,parent); imp=new("imp",this);
    endfunction
    function void build_phase(uvm_phase phase);
        super.build_phase(phase);
        if (!uvm_config_db#(virtual cnn_if)::get(this,"","vif",vif) ||
            !uvm_config_db#(cnn_dma_stream_cfg)::get(this,"","dma_stream_cfg",cfg))
            `uvm_fatal("DMA_FEED", "Missing configuration")
    endfunction
    function void write_feed(cnn_dma_event_item tr);
        if (!cfg.full_flow_enable || tr.kind!=CNN_DMA_AXIL_WRITE || tr.resp!=0) return;
        case(tr.addr)
            32'h40410028: if (!cfg.weight_commands.try_put(tr.data)) `uvm_fatal("DMA_FEED", "Weight queue")
            32'h40420028: if (!cfg.feature_commands.try_put(tr.data)) `uvm_fatal("DMA_FEED", "Feature queue")
            32'h40400010: if (!cfg.image_commands.try_put(tr.data)) `uvm_fatal("DMA_FEED", "Image queue")
        endcase
    endfunction
    function int unsigned next_random(int stream);
        rng[stream]=rng[stream] ^ (rng[stream]<<13);
        rng[stream]=rng[stream] ^ (rng[stream]>>17);
        rng[stream]=rng[stream] ^ (rng[stream]<<5);
        return rng[stream];
    endfunction
    // Called at negedge after preceding beat was accepted. Payload held on
    // every nonaccepting edge; only deliberate VALID-low intervals are gaps.
    task random_gap(int stream);
        int length;
        cnn_dma_gap_event_item ev;
        if(cfg.scenario_id!=DMA_SCENARIO_S04 || next_random(stream)%16!=0) return;
        length=1+next_random(stream)%3;
        case(stream)
            0: vif.s_weight_valid=0;
            1: vif.s_image_valid=0;
            2: vif.s_feature_valid=0;
        endcase
        cfg.gap_active[stream]=1;
        repeat(length) @(negedge vif.clk);
        cfg.gap_active[stream]=0;
        cfg.gap_events[stream]++; cfg.gap_cycles[stream]+=length;
        ev=cnn_dma_gap_event_item::type_id::create("full_flow_gap");
        ev.stream=cnn_dma_gap_stream_e'(stream); ev.gap_cycles=length;
        cfg.gap_cov_mbox.put(ev);
    endtask
    task send_packet(bit feature_input, bit [31:0] bytes);
        int beats,valid_bytes,wait_cycles;
        bit [7:0] keep;
        if (bytes==0) `uvm_fatal("DMA_FEED", "Zero LENGTH")
        beats=(bytes+7)/8;
        for(int beat=0;beat<beats;beat++) begin
            valid_bytes=(beat==beats-1 && bytes%8!=0) ? bytes%8 : 8;
            keep=(9'b1<<valid_bytes)-1;
            @(negedge vif.clk);
            random_gap(feature_input ? 2 : 0);
            if (feature_input) begin
                vif.s_feature_valid=1; vif.s_feature_data=0;
                vif.s_feature_keep=keep; vif.s_feature_last=(beat==beats-1);
            end else begin
                vif.s_weight_valid=1; vif.s_weight_data=0;
                vif.s_weight_keep=keep; vif.s_weight_last=(beat==beats-1);
            end
            wait_cycles=0;
            do begin
                @(posedge vif.clk); wait_cycles++;
                if (wait_cycles>2000000) `uvm_fatal("DMA_FEED", "No stream acceptance for 2M cycles")
            end while (feature_input ? !vif.s_feature_ready : !vif.s_weight_ready);
        end
        @(negedge vif.clk);
        if(feature_input) begin vif.s_feature_valid=0; vif.s_feature_last=0; end
        else begin vif.s_weight_valid=0; vif.s_weight_last=0; end
    endtask
    task weight_worker();
        bit [31:0] bytes;
        forever begin cfg.weight_commands.get(bytes); send_packet(0,bytes); weight_packets++; end
    endtask
    task feature_worker();
        bit [31:0] bytes;
        forever begin cfg.feature_commands.get(bytes); send_packet(1,bytes); feature_packets++; end
    endtask
    task image_worker();
        bit [31:0] tail;
        int wait_cycles;
        forever begin
            cfg.image_commands.get(tail);
            if(tail!=32'h112023c0) `uvm_fatal("DMA_FEED", "Unexpected image SG tail")
            for(int row=0;row<144;row++) begin
                for(int beat=0;beat<480;beat++) begin
                    @(negedge vif.clk);
                    random_gap(1);
                    vif.s_image_valid=1; vif.s_image_data=0; vif.s_image_keep=8'hff;
                    vif.s_image_last=(beat==479);
                    wait_cycles=0;
                    do begin
                        @(posedge vif.clk); wait_cycles++;
                        if(wait_cycles>2000000) `uvm_fatal("DMA_FEED", "No image acceptance for 2M cycles")
                    end while(!vif.s_image_ready);
                end
                image_packets++;
            end
            @(negedge vif.clk); vif.s_image_valid=0; vif.s_image_last=0;
        end
    endtask
    task run_phase(uvm_phase phase);
        if(!cfg.full_flow_enable) return;
        foreach(rng[i]) rng[i]=cfg.random_seed ^ (32'h9e3779b9*(i+1));
        wait(vif.rst_n===1);
        fork weight_worker(); feature_worker(); image_worker(); join
    endtask
endclass
