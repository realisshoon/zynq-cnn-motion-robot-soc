class cnn_dma_s03_backpressure_seq extends cnn_dma_stream_base_seq;

    `uvm_object_utils(cnn_dma_s03_backpressure_seq)

    virtual cnn_if vif;

    bit [31:0] final_status;
    bit image_read_done_seen;

    int unsigned stall_events;
    int unsigned stall_cycles_total;

    localparam int unsigned WEIGHT_BEATS = 120;
    localparam int unsigned IMAGE_ROWS = 144;
    localparam int unsigned IMAGE_BEATS_PER_ROW = 480;
    localparam int unsigned IMAGE_TOTAL_BEATS =
        IMAGE_ROWS * IMAGE_BEATS_PER_ROW;

    function new(string name = "cnn_dma_s03_backpressure_seq");

        super.new(name);

        final_status = 32'd0;
        image_read_done_seen = 1'b0;

        stall_events = 0;
        stall_cycles_total = 0;

    endfunction

    // Directed stall length
    //
    // cfg = 1 ~ 3이면
    //
    // event 0 -> 1 cycle
    // event 1 -> 2 cycle
    // event 2 -> 3 cycle
    function int unsigned get_stall_cycles(int unsigned index);

        int unsigned span;

        if (cfg.max_stall_cycles <= cfg.min_stall_cycles) begin

            return cfg.min_stall_cycles;

        end

        span = cfg.max_stall_cycles - cfg.min_stall_cycles + 1;

        return cfg.min_stall_cycles + (index % span);

    endfunction

    task pre_body();

        super.pre_body();

        if (vif == null) begin

            `uvm_fatal(get_type_name(), "vif was not assigned to S03 sequence")

        end

    endtask

    task body();

        int unsigned beat;
        int unsigned row;
        int unsigned col_beat;
        int unsigned global_image_beat;

        int unsigned stall_cycles;
        int unsigned poll_count;
        int unsigned stall_wait_count;

        bit [1:0] resp;

        `uvm_info(get_type_name(), "S03 Output Backpressure scenario START",
                  UVM_LOW)

        // STEP 1
        // DUT START
        axil_write(12'h000, 32'h0000_0001);

        // STEP 2
        // Weight DMA arm 이후 Weight 전송
        wait_dma_write_commit(vif, 32'h4041_0028, 32'h0000_03C0);

        for (beat = 0; beat < WEIGHT_BEATS; beat++) begin

            send_stream(CNN_WEIGHT_BEAT, 64'h0000_0000_0000_0000, 8'hFF,
                        (beat == WEIGHT_BEATS - 1));

        end

        `uvm_info(get_type_name(), "S03 Weight stream completed", UVM_LOW)

        // STEP 3
        // IMAGE DMA arm
        wait_dma_write_commit(vif, 32'h4040_0010, 32'h1120_23C0);

        global_image_beat = 0;

        // IMAGE 입력을 계속 공급하면서
        //
        // m_feature_valid이 실제로 올라온 순간을 발견하면
        // 1/2/3 cycle READY stall을 순서대로 발생
        for (row = 0; row < IMAGE_ROWS; row++) begin

            for (col_beat = 0; col_beat < IMAGE_BEATS_PER_ROW; col_beat++) begin

                send_stream(CNN_IMAGE_BEAT, 64'h0000_0000_0000_0000, 8'hFF,

                            (col_beat == IMAGE_BEATS_PER_ROW - 1));

                global_image_beat++;

                // S03-A
                //
                // 일반 output beat에서만 backpressure
                //
                // TLAST boundary stall은 다음 단계에서 추가
                if (
                    cfg.output_stall_enable &&
                    stall_events < 3 &&
                    vif.m_feature_valid === 1'b1 &&
                    vif.m_feature_ready === 1'b1 &&
                    vif.m_feature_last !== 1'b1
                ) begin

                    stall_cycles = get_stall_cycles(stall_events);

                    `uvm_info(get_type_name(),
                              $sformatf({"S03 applying output stall: ",
                                         "event=%0d cycles=%0d"},
                                            stall_events + 1, stall_cycles),
                              UVM_LOW)

                    set_feature_ready(1'b0, stall_cycles);

                    stall_events++;
                    stall_cycles_total += stall_cycles;

                end

            end

        end

        // 혹시 IMAGE 입력 중 3개의 stall을 모두 만들지 못했다면
        // 남아 있는 Feature output 동안 추가 시도
        stall_wait_count = 0;

        while (stall_events < 3 && stall_wait_count < 100000) begin

            @(posedge vif.clk);

            if (
                vif.m_feature_valid === 1'b1 &&
                vif.m_feature_ready === 1'b1 &&
                vif.m_feature_last !== 1'b1
            ) begin

                stall_cycles = get_stall_cycles(stall_events);

                set_feature_ready(1'b0, stall_cycles);

                stall_events++;
                stall_cycles_total += stall_cycles;

            end

            stall_wait_count++;

        end

        if (stall_events != 3) begin

            `uvm_fatal(get_type_name(), $sformatf(
                                            {"S03 could not generate ",
                                             "all directed stalls. events=%0d"
                                                }, stall_events))

        end

        `uvm_info(get_type_name(), $sformatf({"S03 stalls completed: ",
                                              "events=%0d total_cycles=%0d"},
                                                 stall_events,
                                                 stall_cycles_total), UVM_LOW)

        // STEP 4
        // Stage0 완료 / IMAGE_READ_DONE 확인
        poll_count = 0;

        while (!image_read_done_seen && poll_count < 5000) begin

            repeat (1000) @(posedge vif.clk);

            axil_read(12'h004, final_status, resp);

            if (resp != 2'b00) begin

                `uvm_fatal(get_type_name(),
                           $sformatf("STATUS read failed resp=0x%0h", resp))

            end

            if (final_status[2]) begin

                `uvm_fatal(get_type_name(),
                           $sformatf({"DUT error during S03. ", "STATUS=0x%08h"
                                         }, final_status))

            end

            if (final_status[3]) begin

                image_read_done_seen = 1'b1;

            end

            poll_count++;

        end

        if (!image_read_done_seen) begin

            `uvm_fatal(get_type_name(),
                       "S03 timeout waiting for IMAGE_READ_DONE")

        end

        `uvm_info(get_type_name(),
                  "S03 Output Backpressure sequence completed", UVM_LOW)

    endtask

endclass
