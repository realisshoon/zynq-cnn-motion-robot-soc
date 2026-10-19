cnn_golden_feature_memory feature_mem;

class cnn_golden_env extends cnn_env;

    `uvm_component_utils(cnn_golden_env)

    cnn_g02_checkpoint_monitor g02_mon;
    bit g02_enable;
    cnn_g05_atomic_monitor g05_mon;
    bit g05_enable;

    function new(string name, uvm_component parent);
        super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);

        cnn_scoreboard::type_id::set_type_override(cnn_golden_scoreboard::get_type());

        cnn_m_axil_responder::type_id::set_type_override(cnn_golden_dma_responder::get_type());

        super.build_phase(phase);

        golden_cov = cnn_golden_coverage::type_id::create("golden_cov", this);

        feature_mem = cnn_golden_feature_memory::type_id::create("feature_mem", this);

        g02_enable  = 1'b0;

        void'(uvm_config_db#(bit)::get(this, "", "g02_enable", g02_enable));

        if (g02_enable) begin
            g02_mon = cnn_g02_checkpoint_monitor::type_id::create("g02_mon", this);
        end
        g05_enable = 1'b0;

        void'(uvm_config_db#(bit)::get(this, "", "g05_enable", g05_enable));

        if (g05_enable) begin

            g05_mon = cnn_g05_atomic_monitor::type_id::create("g05_mon", this);

        end

    endfunction

    function void connect_phase(uvm_phase phase);

        super.connect_phase(phase);

        agt.mon.ap.connect(feature_mem.analysis_export);

    endfunction

endclass
