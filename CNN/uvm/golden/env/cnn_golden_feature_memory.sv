class cnn_golden_feature_memory extends uvm_subscriber #(cnn_seq_item);

    `uvm_component_utils(cnn_golden_feature_memory)

    string feature_dir;

    int fd;

    int unsigned stage_id;
    int unsigned beat_count;


    function new(string name = "cnn_golden_feature_memory", uvm_component parent = null);

        super.new(name, parent);

        fd         = 0;
        stage_id   = 0;
        beat_count = 0;

    endfunction


    function void build_phase(uvm_phase phase);

        super.build_phase(phase);

        // 실행 옵션:
        // +FEATURE_DIR=/tmp/cnn_feature_stream

        if (!$value$plusargs("FEATURE_DIR=%s", feature_dir)) begin

            feature_dir = "/tmp/cnn_feature_stream";

        end

    endfunction

    function int unsigned expected_feature_beats(int unsigned stage);

        case (stage)

            0: return 49152;
            1: return 98304;

            2: return 49152;
            3: return 49152;

            4: return 24576;
            5: return 24576;

            6:  return 12288;
            7:  return 12288;
            8:  return 12288;
            9:  return 12288;
            10: return 12288;
            11: return 12288;
            12: return 12288;
            13: return 12288;

            default: return 0;

        endcase

    endfunction


    virtual function void write(cnn_seq_item tr);

        string feature_path;


        // =====================================================
        // DUT -> DDR 방향인 m_feature만 저장
        // =====================================================

        if (tr.kind != CNN_FEATURE_OUT_BEAT) return;


        // =====================================================
        // Stage 첫 Feature beat
        // =====================================================

        if (fd == 0) begin

            feature_path = $sformatf("%s/stage_%02d.hex", feature_dir, stage_id);

            fd = $fopen(feature_path, "w");


            if (fd == 0) begin

                `uvm_fatal(get_type_name(), $sformatf("Cannot open feature file: %s", feature_path))

            end


            beat_count = 0;


            `uvm_info(get_type_name(), $sformatf("Capturing Stage%0d feature output: %s", stage_id,
                                                 feature_path), UVM_LOW)

        end


        // =====================================================
        // 실제 AXI-Stream beat 저장
        //
        // data64  keep  last
        // =====================================================

        $fwrite(fd, "%016h %02h %0d\n", tr.data64, tr.keep, tr.last);


        beat_count++;


        // =====================================================
        // 마지막 beat
        // =====================================================

        if (tr.last) begin

            uvm_event stage_done_event;

            // =====================================================
            // Tensor size check
            // =====================================================

            if (beat_count != expected_feature_beats(stage_id)) begin

                `uvm_fatal(get_type_name(),
                           $sformatf("Stage%0d feature size mismatch: expected=%0d actual=%0d",
                                     stage_id, expected_feature_beats(stage_id), beat_count))

            end


            $fclose(fd);
            fd = 0;


            `uvm_info(get_type_name(), $sformatf("Stage%0d feature captured: %0d beats [PASS]",
                                                 stage_id, beat_count), UVM_LOW)


            stage_done_event =
                uvm_event_pool::get_global($sformatf("FEATURE_STAGE_%0d_DONE", stage_id));

            stage_done_event.trigger();

            `uvm_info(get_type_name(), $sformatf("FEATURE_STAGE_%0d_DONE event triggered",
                                                 stage_id), UVM_MEDIUM)

            stage_id++;

        end

    endfunction

endclass
