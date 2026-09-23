class cnn_monitor extends uvm_monitor;
    `uvm_component_utils(cnn_monitor)

    virtual cnn_if vif;
    uvm_analysis_port #(cnn_seq_item) ap;

    function new(string name, uvm_component parent);
        super.new(name, parent);
        ap = new("ap", this);
    endfunction

    function void build_phase(uvm_phase phase);
        super.build_phase(phase);
        if (!uvm_config_db#(virtual cnn_if)::get(this, "", "vif", vif))
            `uvm_fatal(get_type_name(), "cnn_if was not found in uvm_config_db")
    endfunction

    task run_phase(uvm_phase phase);
        cnn_seq_item tr;
        bit irq_d;

        irq_d = 1'b0;
        forever begin
            @(vif.mon_cb);

            if (vif.mon_cb.s_image_valid && vif.mon_cb.s_image_ready) begin
                tr = cnn_seq_item::type_id::create("image_tr");
                tr.kind   = CNN_IMAGE_BEAT;
                tr.data64 = vif.mon_cb.s_image_data;
                tr.keep   = vif.mon_cb.s_image_keep;
                tr.last   = vif.mon_cb.s_image_last;
                ap.write(tr);
            end

            if (vif.mon_cb.s_weight_valid && vif.mon_cb.s_weight_ready) begin
                tr = cnn_seq_item::type_id::create("weight_tr");
                tr.kind   = CNN_WEIGHT_BEAT;
                tr.data64 = vif.mon_cb.s_weight_data;
                tr.keep   = vif.mon_cb.s_weight_keep;
                tr.last   = vif.mon_cb.s_weight_last;
                ap.write(tr);
            end

            if (vif.mon_cb.s_feature_valid && vif.mon_cb.s_feature_ready) begin
                tr = cnn_seq_item::type_id::create("feature_in_tr");
                tr.kind   = CNN_FEATURE_IN_BEAT;
                tr.data64 = vif.mon_cb.s_feature_data;
                tr.keep   = vif.mon_cb.s_feature_keep;
                tr.last   = vif.mon_cb.s_feature_last;
                ap.write(tr);
            end

            if (vif.mon_cb.m_feature_valid && vif.mon_cb.m_feature_ready) begin
                tr = cnn_seq_item::type_id::create("feature_out_tr");
                tr.kind   = CNN_FEATURE_OUT_BEAT;
                tr.data64 = vif.mon_cb.m_feature_data;
                tr.keep   = vif.mon_cb.m_feature_keep;
                tr.last   = vif.mon_cb.m_feature_last;
                ap.write(tr);
            end

            if (vif.mon_cb.irq && !irq_d) begin
                tr = cnn_seq_item::type_id::create("irq_tr");
                tr.kind = CNN_IRQ_EVENT;
                ap.write(tr);
            end
            irq_d = vif.mon_cb.irq;
        end
    endtask
endclass
