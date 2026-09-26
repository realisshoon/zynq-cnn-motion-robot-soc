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

endclass