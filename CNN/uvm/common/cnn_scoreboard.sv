class cnn_scoreboard extends uvm_scoreboard;
    `uvm_component_utils(cnn_scoreboard)

    uvm_analysis_imp #(cnn_seq_item, cnn_scoreboard) imp;

    int unsigned image_beats;
    int unsigned weight_beats;
    int unsigned feature_in_beats;
    int unsigned feature_out_beats;
    int unsigned irq_events;

    function new(string name, uvm_component parent);
        super.new(name, parent);
        imp = new("imp", this);
    endfunction

    function void write(cnn_seq_item tr);
        case (tr.kind)
            CNN_IMAGE_BEAT:       image_beats++;
            CNN_WEIGHT_BEAT:      weight_beats++;
            CNN_FEATURE_IN_BEAT:  feature_in_beats++;
            CNN_FEATURE_OUT_BEAT: feature_out_beats++;
            CNN_IRQ_EVENT:        irq_events++;
            default: begin
            end
        endcase
    endfunction

    function void report_phase(uvm_phase phase);
        super.report_phase(phase);
        `uvm_info("CNN_BASE_SCB",
            $sformatf("image=%0d weight=%0d feature_in=%0d feature_out=%0d irq=%0d",
                      image_beats, weight_beats, feature_in_beats,
                      feature_out_beats, irq_events),
            UVM_LOW)
    endfunction
endclass
