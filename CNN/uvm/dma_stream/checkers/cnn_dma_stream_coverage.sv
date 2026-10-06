`uvm_analysis_imp_decl(_dma_cov)
class cnn_dma_stream_coverage extends cnn_coverage;

    `uvm_component_utils(cnn_dma_stream_coverage)


    cnn_dma_stream_cfg cfg;
    uvm_analysis_imp_dma_cov #(cnn_dma_event_item,cnn_dma_stream_coverage) dma_imp;

    // Coverage sample variables
    cnn_dma_gap_stream_e sampled_stream;

    int unsigned sampled_gap_cycles;


    // Scenario identity only; correctness comes from independent checks.
    covergroup scenario_cg;
        option.per_instance = 1;
        cp_scenario_id: coverpoint cfg.scenario_id {
            bins s01 = {DMA_SCENARIO_S01}; bins s02 = {DMA_SCENARIO_S02};
            bins s03 = {DMA_SCENARIO_S03}; bins s04 = {DMA_SCENARIO_S04};
            bins s05 = {DMA_SCENARIO_S05}; bins s06 = {DMA_SCENARIO_S06};
            bins s07 = {DMA_SCENARIO_S07}; bins s08 = {DMA_SCENARIO_S08};
        }
    endgroup

    // Samples completed, observed VALID&&!READY episodes, not requests.
    covergroup backpressure_cg with function sample(bit last_beat, int cycles, bit recovery);
        option.per_instance = 1;
        stall_type: coverpoint last_beat {
            bins normal_beat_stall = {0}; bins last_beat_stall = {1};
        }
        stall_length: coverpoint cycles {
            bins stall_1_cycle = {1}; bins stall_2_cycles = {2}; bins stall_3_cycles = {3};
        }
        stall_recovery: coverpoint recovery { bins recovered = {1}; }
        type_length: cross stall_type, stall_length;
    endgroup

    covergroup random_stall_cg with function sample(int stream, int length, bit recovery);
        option.per_instance=1;
        cp_stream: coverpoint stream {
            bins weight_gap_hit={0}; bins image_gap_hit={1}; bins output_stall_hit={2}; bins feature_gap_hit={3};
        }
        cp_length: coverpoint length { bins cycles[]={[1:3]}; }
        cp_recovery: coverpoint recovery { bins recovered={1}; }
        stream_length: cross cp_stream,cp_length;
    endgroup
    covergroup simultaneous_cg with function sample(int cycles);
        option.per_instance=1;
        cp_simultaneous: coverpoint cycles { bins observed={[1:$]}; }
    endgroup

    covergroup axil_protocol_cg with function sample(bit write_access, int order, int latency);
        option.per_instance=1;
        cp_access: coverpoint write_access { bins read_transaction={0}; bins write_transaction={1}; }
        cp_order: coverpoint order iff(write_access) {
            bins same_cycle={0}; bins aw_first={1}; bins w_first={2};
        }
        cp_latency: coverpoint latency { bins short_response={[1:2]}; bins delayed_response={[3:$]}; }
        access_latency: cross cp_access,cp_latency;
    endgroup
    function void write_dma_cov(cnn_dma_event_item tr);
        axil_protocol_cg.sample(tr.kind==CNN_DMA_AXIL_WRITE,tr.handshake_order,tr.response_cycles);
    endfunction

    covergroup weight_dma_cg with function sample(int checkpoint);
        option.per_instance=1;
        cp_checkpoint: coverpoint checkpoint {
            bins run={0}; bins source={1}; bins length={2}; bins command_order={3};
            bins stream_after_commit={4}; bins complete_120_beats={5};
        }
    endgroup

    covergroup completion_order_cg with function sample(int checkpoint);
        option.per_instance=1;
        cp_checkpoint: coverpoint checkpoint {
            bins dma_done_before_stream_done={0}; bins stream_done_after_dma_done={1};
            bins safe_final_completion={2}; bins s2mm_before_source={3};
        }
    endgroup
    // Body arm order is checked only by actual full-flow traffic.
    covergroup body_arm_order_cg with function sample(bit ordered);
        option.per_instance=1;
        cp_order: coverpoint ordered { bins s2mm_before_mm2s={1}; }
    endgroup
    // Every mapping coverpoint samples the address actually committed by DUT.
    covergroup fm_mapping_cg with function sample(int stage, bit dst, bit [31:0] addr);
        option.per_instance=1;
        stage0_dst: coverpoint addr iff(stage==0 && dst) { bins fm_a={32'h11000000}; }
        odd_body_src: coverpoint addr iff(stage>=1 && stage<=13 && stage%2==1 && !dst)
            { bins fm_a={32'h11000000}; }
        odd_body_dst: coverpoint addr iff(stage>=1 && stage<=13 && stage%2==1 && dst)
            { bins fm_b={32'h11100000}; }
        even_body_src: coverpoint addr iff(stage>=2 && stage<=12 && stage%2==0 && !dst)
            { bins fm_b={32'h11100000}; }
        even_body_dst: coverpoint addr iff(stage>=2 && stage<=12 && stage%2==0 && dst)
            { bins fm_a={32'h11000000}; }
        head14_src: coverpoint addr iff(stage==14 && !dst) { bins fm_b={32'h11100000}; }
        head15_src: coverpoint addr iff(stage==15 && !dst) { bins fm_b={32'h11100000}; }
    endgroup
    covergroup fm_stage_cg with function sample(int stage);
        option.per_instance=1;
        cp_stage: coverpoint stage { bins stages[]={[0:15]}; }
    endgroup

    // Actual Input Gap Coverage
    //
    // Sequence에서 실제 삽입 완료된 gap event만 sample
    covergroup input_gap_cg;

        option.per_instance = 1;

        // 어느 Stream인가?
        cp_stream: coverpoint sampled_stream {

            bins weight = {DMA_GAP_WEIGHT}; bins image = {DMA_GAP_IMAGE};

        }

        // 실제 삽입된 intentional gap 길이
        cp_gap_cycles: coverpoint sampled_gap_cycles {

            bins gap_1_cycle = {1};

            bins gap_2_cycles = {2};

            bins gap_3_cycles = {3};

            // 현재 S02 Verification Plan 범위 밖
            ignore_bins outside_s02 = {0, [4 : $]};

        }

        // Stream × Gap Length
        x_stream_gap : cross cp_stream, cp_gap_cycles;

    endgroup

    // Constructor
    function new(string name = "cnn_dma_stream_coverage",
                 uvm_component parent = null);

        super.new(name, parent);

        dma_imp=new("dma_imp",this);
        axil_protocol_cg=new(); weight_dma_cg=new();
        completion_order_cg=new(); body_arm_order_cg=new(); fm_mapping_cg=new(); fm_stage_cg=new();
        scenario_cg        = new();
        backpressure_cg = new();
        random_stall_cg=new(); simultaneous_cg=new();
        input_gap_cg       = new();

        sampled_stream     = DMA_GAP_WEIGHT;
        sampled_gap_cycles = 0;

    endfunction

    // build_phase
    function void build_phase(uvm_phase phase);

        super.build_phase(phase);

        if (!uvm_config_db#(cnn_dma_stream_cfg)::get(
                this, "", "dma_stream_cfg", cfg
            )) begin

            `uvm_fatal(get_type_name(), "dma_stream_cfg was not found")

        end

    endfunction

    // Common Coverage
    //
    // 기존 kind / keep / last coverage 유지
    function void write(cnn_seq_item t);

        super.write(t);

    endfunction

    // run_phase
    //
    // 1. Scenario mode는 test당 한 번 sample
    //
    // 2. 실제 gap은 sequence가 mailbox로 보낸 event만 sample
    task run_phase(uvm_phase phase);

        cnn_dma_gap_event_item ev;

        // Exactly one actual test identity per run.

        scenario_cg.sample();

        // Actual Gap Event Coverage
        forever begin

            // Sequence가 실제 gap을 발생시킬 때까지 대기
            cfg.gap_cov_mbox.get(ev);

            sampled_stream = ev.stream;

            sampled_gap_cycles = ev.gap_cycles;

            if (ev.stream!=DMA_GAP_FEATURE) input_gap_cg.sample();
            if (cfg.scenario_id==DMA_SCENARIO_S04)
                random_stall_cg.sample(ev.stream==DMA_GAP_FEATURE ? 3 : int'(ev.stream),ev.gap_cycles,1);

        end

    endtask

    // report_phase
    function void report_phase(uvm_phase phase);

        super.report_phase(phase);
        if (cfg.scenario_id==DMA_SCENARIO_S04) simultaneous_cg.sample(cfg.simultaneous_cycles);
        `uvm_info("DMA_ADDITIONAL_COVERAGE",$sformatf(
            "weight_dma=%0.2f%% completion_order=%0.2f%% body_arm=%0.2f%% fm_mapping=%0.2f%% fm_stages=%0.2f%%",
            weight_dma_cg.get_inst_coverage(),completion_order_cg.get_inst_coverage(),
            body_arm_order_cg.get_inst_coverage(),fm_mapping_cg.get_inst_coverage(),fm_stage_cg.get_inst_coverage()),UVM_NONE)
        `uvm_info("DMA_SCENARIO_COVERAGE", $sformatf("backpressure=%0.2f%% random_stall=%0.2f%% simultaneous=%0.2f%% axil_protocol=%0.2f%%",
            backpressure_cg.get_inst_coverage(),random_stall_cg.get_inst_coverage(),simultaneous_cg.get_inst_coverage(),axil_protocol_cg.get_inst_coverage()), UVM_NONE)

        `uvm_info(get_type_name(), $sformatf(
                  {
                      "DMA FUNCTIONAL COVERAGE: ",
                      "scenario=%0.2f%% ",
                      "input_gap=%0.2f%%"
                  },
                  scenario_cg.get_inst_coverage(),
                  input_gap_cg.get_inst_coverage()
                  ), UVM_NONE)

    endfunction

endclass
