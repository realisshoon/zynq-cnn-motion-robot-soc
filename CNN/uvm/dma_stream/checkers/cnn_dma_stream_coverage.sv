class cnn_dma_stream_coverage extends cnn_coverage;

    `uvm_component_utils(cnn_dma_stream_coverage)


    cnn_dma_stream_cfg cfg;

    // Coverage sample variables
    cnn_dma_gap_stream_e sampled_stream;

    int unsigned sampled_gap_cycles;

    bit [1:0] sampled_gap_mode;

    // Scenario Coverage
    //
    // 00 : S01 No Gap
    // 11 : S02 Weight + Image Gap
    covergroup scenario_cg;

        option.per_instance = 1;

        cp_gap_mode: coverpoint sampled_gap_mode {

            bins s01_no_gap = {2'b00};

            bins s02_weight_image_gap = {2'b11};

            // 현재 S01/S02에는 없는 mode
            ignore_bins unplanned_modes = {2'b01, 2'b10};

        }

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

        scenario_cg        = new();
        input_gap_cg       = new();

        sampled_gap_mode   = 2'b00;
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

        // S01 / S02 Scenario Coverage
        sampled_gap_mode = {cfg.image_gap_enable, cfg.weight_gap_enable};

        scenario_cg.sample();

        // Actual Gap Event Coverage
        forever begin

            // Sequence가 실제 gap을 발생시킬 때까지 대기
            cfg.gap_cov_mbox.get(ev);

            sampled_stream = ev.stream;

            sampled_gap_cycles = ev.gap_cycles;

            input_gap_cg.sample();

        end

    endtask

    // report_phase
    function void report_phase(uvm_phase phase);

        super.report_phase(phase);

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
