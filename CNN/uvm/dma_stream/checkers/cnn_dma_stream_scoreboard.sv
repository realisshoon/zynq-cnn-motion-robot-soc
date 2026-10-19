// dma monitor에서는 cnn_dma_event_item을 보내고 common monitor에서는 cnn_seq_item을 보냄
// scoreboard가 받을 item 구분을 위해 아래 코드 사용
`uvm_analysis_imp_decl(_dma)
`uvm_analysis_imp_decl(_checked_stream)

class cnn_dma_stream_scoreboard extends cnn_scoreboard;
    `uvm_component_utils(cnn_dma_stream_scoreboard)

    // DMA monitor -> scoreboard 
    uvm_analysis_imp_dma #(cnn_dma_event_item, cnn_dma_stream_scoreboard) dma_imp;

    cnn_dma_stream_cfg cfg;
    cnn_dma_command_checker commands;
    uvm_analysis_imp_checked_stream #(cnn_seq_item,cnn_dma_stream_scoreboard) checked_stream_imp;
    int unsigned aw_first_count, w_first_count, same_cycle_count;
    int unsigned bresp_errors, rresp_errors;

    // reference model
    cnn_dma_reference_model ref_model;

    // DMA transaction count
    int unsigned dma_write_count;
    int unsigned dma_read_count;

    function new(string name = "cnn_dma_stream_scoreboard", uvm_component parent = null);
        super.new(name, parent);

        dma_imp = new("dma_imp", this);
        checked_stream_imp=new("checked_stream_imp",this);
        commands=cnn_dma_command_checker::type_id::create("commands");
    endfunction

    // build phase
    function void build_phase(uvm_phase phase);
        super.build_phase(phase);

        if (!uvm_config_db#(cnn_dma_stream_cfg)::get(this,"","dma_stream_cfg",cfg))
            `uvm_fatal("DMA_SCB", "Missing configuration")
        ref_model = cnn_dma_reference_model::type_id::create("ref_model");

        dma_write_count = 0;
        dma_read_count  = 0;
    endfunction

    function void connect_phase(uvm_phase phase);
        uvm_component owner;
        super.connect_phase(phase); owner=get_parent();
        commands.cfg=cfg;
        if (!$cast(commands.cov,owner.get_child("cov"))) `uvm_fatal("DMA_SCB", "Missing coverage")
    endfunction
    function void write_checked_stream(cnn_seq_item tr);
        commands.stream(tr);
    endfunction

    // DMA monitor에서 transaction 수신
    function void write_dma(cnn_dma_event_item tr);
        commands.command(tr);
        if(cfg.scenario_id==DMA_SCENARIO_S05 &&
            tr.response_cycles != 1+(tr.kind==CNN_DMA_AXIL_WRITE ? cfg.dma_write_latency : cfg.dma_read_latency))
            `uvm_error("S05_LATENCY",$sformatf("Observed response latency=%0d does not match configured extra latency",tr.response_cycles))
        case (tr.kind)
            CNN_DMA_AXIL_WRITE: begin
                dma_write_count++;
                case(tr.handshake_order)
                    0: same_cycle_count++;
                    1: aw_first_count++;
                    2: w_first_count++;
                endcase
                if (tr.resp!=0) begin
                    bresp_errors++; `uvm_error("DMA_BRESP", "Expected OKAY")
                end
                `uvm_info(get_type_name(), $sformatf("DMA WRITE: addr = 0x%08h, data = 0x%08h, strb = 0x%1h, resp = 0x%1h", tr.addr, tr.data, tr.strb, tr.resp), UVM_MEDIUM)
            end 
            CNN_DMA_AXIL_READ: begin
                dma_read_count++;
                if (tr.resp!=0) begin
                    rresp_errors++; `uvm_error("DMA_RRESP", "Expected OKAY")
                end
                `uvm_info(get_type_name(), $sformatf("DMA READ: addr = 0x%08h, data = 0x%08h, resp = 0x%1h", tr.addr, tr.data, tr.resp), UVM_MEDIUM)
            end
            default: begin
                `uvm_warning(get_type_name(), "Unknown DMA transaction kind")
            end
        endcase
    endfunction

    // final report
    function void report_phase(uvm_phase phase);
        super.report_phase(phase);
        `uvm_info(get_type_name(), $sformatf("DMA SCOREBOARD SUMMARY: writes = %0d, reads = %0d", dma_write_count, dma_read_count), UVM_LOW)
    endfunction
    
endclass