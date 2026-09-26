// dma monitor에서는 cnn_dma_event_item을 보내고 common monitor에서는 cnn_seq_item을 보냄
// scoreboard가 받을 item 구분을 위해 아래 코드 사용
`uvm_analysis_imp_decl(_dma)

class cnn_dma_stream_scoreboard extends cnn_scoreboard;
    `uvm_component_utils(cnn_dma_stream_scoreboard)

    // DMA monitor -> scoreboard 
    uvm_analysis_imp_dma #(cnn_dma_event_item, cnn_dma_stream_scoreboard) dma_imp;

    // reference model
    cnn_dma_reference_model ref_model;

    // DMA transaction count
    int unsigned dma_write_count;
    int unsigned dma_read_count;

    function new(string name = "cnn_dma_stream_scoreboard", uvm_component parent = null);
        super.new(name, parent);

        dma_imp = new("dma_imp", this);
    endfunction

    // build phase
    function void build_phase(uvm_phase phase);
        super.build_phase(phase);

        ref_model = cnn_dma_reference_model::type_id::create("ref_model");

        dma_write_count = 0;
        dma_read_count  = 0;
    endfunction

    // DMA monitor에서 transaction 수신
    function void write_dma(cnn_dma_event_item tr);
        case (tr.kind)
            CNN_DMA_AXIL_WRITE: begin
                dma_write_count++;
                `uvm_info(get_type_name(), $sformatf("DMA WRITE: addr = 0x%08h, data = 0x%08h, strb = 0x%1h, resp = 0x%1h", tr.addr, tr.data, tr.strb, tr.resp), UVM_MEDIUM)
            end 
            CNN_DMA_AXIL_READ: begin
                dma_read_count++;
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