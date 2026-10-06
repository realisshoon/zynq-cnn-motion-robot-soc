class cnn_dma_stream_base_test extends cnn_base_test;
    `uvm_component_utils(cnn_dma_stream_base_test)

    cnn_dma_stream_cfg cfg;

    cnn_dma_stream_env dma_env;

    function new(string name = "cnn_dma_stream_base_test", uvm_component parent = null);
        super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);
        // 1. DMA / Stream verification configuration 생성
        cfg = cnn_dma_stream_cfg::type_id::create("cfg");
        
        // 2. configuration을 하위 component에게 전달
        uvm_config_db#(cnn_dma_stream_cfg)::set(this, "*", "dma_stream_cfg", cfg);

        // 3. common env -> DMA stream env로 override
        cnn_env::type_id::set_type_override(cnn_dma_stream_env::get_type());

        // 4. common base test build_phase 실행
        super.build_phase(phase);

        // 5. common env handle -> DMA stream env handle로 변환
        if (!$cast(dma_env, env)) begin
            `uvm_fatal(get_type_name(), "Failed to cast env to cnn_dma_stream_env")
        end
    endfunction

    // Runs after scenario-specific checks and child report phases. Never infer
    // PASS from coverage or simulator exit status alone.
    function void report_phase(uvm_phase phase);
        uvm_report_server server;
        int errors,fatals,assert_fails;
        super.report_phase(phase);
        server=uvm_report_server::get_server();
        errors=server.get_severity_count(UVM_ERROR);
        fatals=server.get_severity_count(UVM_FATAL);
        assert_fails=server.get_id_count("DMA_ASSERT");
        if(cfg.scenario_id!=DMA_SCENARIO_UNSET)
            `uvm_info("SCENARIO_RESULT",$sformatf(
                "S%02d SCOREBOARD SUMMARY: weight=%0d image=%0d feature_out=%0d errors=%0d fatals=%0d assert_fail=%0d RESULT=%s",
                int'(cfg.scenario_id),dma_env.dma_scb.weight_beats,dma_env.dma_scb.image_beats,
                dma_env.dma_scb.feature_out_beats,errors,fatals,assert_fails,
                (errors==0 && fatals==0 && assert_fails==0) ? "PASS" : "FAIL"),UVM_NONE)
    endfunction

    function void end_of_elaboration_phase(uvm_phase phase);
    super.end_of_elaboration_phase(phase);

    uvm_top.print_topology();
    endfunction

endclass