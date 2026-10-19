// Extends the common driver without changing its source. Gaps occur only
// between accepted items, so VALID payload is never withdrawn under stall.
class cnn_dma_random_driver extends cnn_driver;
    `uvm_component_utils(cnn_dma_random_driver)
    cnn_dma_stream_cfg cfg;
    int unsigned rng;
    function new(string name,uvm_component parent); super.new(name,parent); endfunction
    function void build_phase(uvm_phase phase);
        super.build_phase(phase);
        if (!uvm_config_db#(cnn_dma_stream_cfg)::get(this,"","dma_stream_cfg",cfg))
            `uvm_fatal("DMA_RANDOM", "Missing configuration")
    endfunction
    function int unsigned next_random();
        rng = rng ^ (rng << 13); rng = rng ^ (rng >> 17); rng = rng ^ (rng << 5);
        return rng;
    endfunction
    task input_gap(int stream);
        int length;
        cnn_dma_gap_event_item ev;
        if (next_random()%8 != 0) return;
        length=1+next_random()%3;
        cfg.gap_active[stream]=1;
        repeat(length) @(vif.mon_cb);
        cfg.gap_active[stream]=0;
        cfg.gap_events[stream]++; cfg.gap_cycles[stream]+=length;
        ev=cnn_dma_gap_event_item::type_id::create("random_gap");
        ev.stream = stream==0 ? DMA_GAP_WEIGHT : DMA_GAP_IMAGE;
        ev.gap_cycles=length;
        cfg.gap_cov_mbox.put(ev);
    endtask
    task run_phase(uvm_phase phase);
        rng=cfg.random_seed;
        init_inputs(); wait(vif.rst_n===1); repeat(2) @(posedge vif.clk);
        forever begin
            seq_item_port.get_next_item(req);
            case(req.kind)
                CNN_AXIL_WRITE: drive_axil_write(req);
                CNN_AXIL_READ: drive_axil_read(req);
                CNN_WEIGHT_BEAT: begin input_gap(0); drive_weight(req); end
                CNN_IMAGE_BEAT: begin input_gap(1); drive_image(req); end
                CNN_FEATURE_IN_BEAT: drive_feature_in(req);
                CNN_SET_FEATURE_READY: drive_feature_ready(req);
                default: `uvm_fatal("DMA_RANDOM", "Unsupported item")
            endcase
            seq_item_port.item_done();
        end
    endtask
endclass
