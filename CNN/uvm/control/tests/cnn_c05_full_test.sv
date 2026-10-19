class cnn_c05_full_test extends cnn_c05_progress_test;
    `uvm_component_utils(cnn_c05_full_test)

    cnn_c05_fsm_monitor c05_mon;
    cnn_c05_fsm_checker c05_checker;
    cnn_c05_fsm_coverage c05_cov;

    function new(string name = "cnn_c05_full_test", uvm_component parent = null);
        super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);
        super.build_phase(phase);
        c05_mon = cnn_c05_fsm_monitor::type_id::create("c05_mon", this);
        c05_checker = cnn_c05_fsm_checker::type_id::create("c05_checker", this);
        c05_cov = cnn_c05_fsm_coverage::type_id::create("c05_cov", this);
    endfunction

    function void connect_phase(uvm_phase phase);
        super.connect_phase(phase);
        // Separate consumers keep correctness and coverage responsibilities
        // independent, even though both observe the same passive event stream.
        c05_mon.ap.connect(c05_checker.imp);
        c05_mon.ap.connect(c05_cov.analysis_export);
    endfunction

    task run_phase(uvm_phase phase);
        cnn_c05_full_seq seq;
        phase.raise_objection(this);
        wait_reset_release();
        seq = cnn_c05_full_seq::type_id::create("seq");
        seq.vif = vif;
        seq.start(env.agt.sqr);
        phase.drop_objection(this);
    endtask
endclass
