class cnn_dma_s02_input_gap_seq extends cnn_dma_stream_base_seq;

    `uvm_object_utils(cnn_dma_s02_input_gap_seq)

    virtual cnn_if        vif;

    bit            [31:0] final_status;
    bit                   image_read_done_seen;

    int unsigned          weight_gap_events;
    int unsigned          image_gap_events;

    int unsigned          weight_gap_cycles_total;
    int unsigned          image_gap_cycles_total;

    localparam int unsigned WEIGHT_BEATS = 120;
    localparam int unsigned IMAGE_ROWS = 144;
    localparam int unsigned IMAGE_BEATS_PER_ROW = 480;
    localparam int unsigned IMAGE_TOTAL_BEATS =
        IMAGE_ROWS * IMAGE_BEATS_PER_ROW;

    function new(string name = "cnn_dma_s02_input_gap_seq");

        super.new(name);

        final_status = 32'd0;

        image_read_done_seen = 1'b0;

        weight_gap_events = 0;
        image_gap_events = 0;

        weight_gap_cycles_total = 0;
        image_gap_cycles_total = 0;

    endfunction

    // deterministic gap length
    //
    // min_gap_cycles ~ max_gap_cycles 사이의 값을
    // index에 따라 반복적으로 선택
    //
    // random을 쓰지 않아도 1,2,3 cycle 등의 다양한 gap 발생
    function int unsigned get_gap_cycles(int unsigned index);

        int unsigned span;

        if (cfg.max_gap_cycles <= cfg.min_gap_cycles) begin

            return cfg.min_gap_cycles;

        end

        span = cfg.max_gap_cycles - cfg.min_gap_cycles + 1;

        return cfg.min_gap_cycles + (index % span);

    endfunction

    task pre_body();

        super.pre_body();

        if (vif == null) begin

            `uvm_fatal(get_type_name(), "vif was not assigned to S02 sequence")

        end

    endtask

    task body();

        int unsigned beat;

        int unsigned row;
        int unsigned col_beat;
        int unsigned global_image_beat;

        int unsigned gap_cycles;

        int unsigned poll_count;

        bit [1:0] resp;

        `uvm_info(get_type_name(), "S02 Input VALID Gap scenario START",
                  UVM_LOW)

        // STEP 1
        // DUT START
        axil_write(12'h000, 32'h0000_0001);

        wait_dma_write_commit(vif, 32'h4041_0028, 32'h0000_03C0);

        // STEP 2
        // WEIGHT Stream
        //
        // 120 beats는 S01과 동일.
        //
        // 단,
        // 매 8번째 beat 앞에서 intentional gap 삽입.
        for (beat = 0; beat < WEIGHT_BEATS; beat++) begin

            if (cfg.weight_gap_enable && beat != 0 && (beat % 8) == 0) begin

                gap_cycles = get_gap_cycles(beat);

                repeat (gap_cycles) @(posedge vif.clk);

                weight_gap_events++;

                weight_gap_cycles_total += gap_cycles;

            end

            send_stream(CNN_WEIGHT_BEAT, 64'h0000_0000_0000_0000, 8'hFF,

                        (beat == WEIGHT_BEATS - 1));

        end

        `uvm_info(get_type_name(),
                  $sformatf({"S02 Weight complete: ",
                             "beats=%0d gap_events=%0d gap_cycles=%0d"},
                                WEIGHT_BEATS, weight_gap_events,
                                weight_gap_cycles_total), UVM_LOW)

        wait_dma_write_commit(vif, 32'h4040_0010, 32'h1120_23C0);

        // STEP 3
        // IMAGE Stream
        //
        // S01과 동일:
        //
        // 144 rows
        // 480 beats / row
        // total = 69120 beats
        //
        // 차이:
        // 매 64번째 beat 앞에 intentional gap 삽입.
        global_image_beat = 0;

        for (row = 0; row < IMAGE_ROWS; row++) begin

            for (col_beat = 0; col_beat < IMAGE_BEATS_PER_ROW; col_beat++) begin

                if (
                    cfg.image_gap_enable &&
                    global_image_beat != 0 &&
                    (global_image_beat % 64) == 0
                ) begin

                    gap_cycles = get_gap_cycles(global_image_beat);

                    repeat (gap_cycles) @(posedge vif.clk);


                    image_gap_events++;

                    image_gap_cycles_total += gap_cycles;

                end

                send_stream(CNN_IMAGE_BEAT, 64'h0000_0000_0000_0000, 8'hFF,

                            // row마다 TLAST
                            (col_beat == IMAGE_BEATS_PER_ROW - 1));

                global_image_beat++;

            end
        end

        `uvm_info(get_type_name(),
                  $sformatf({"S02 Image complete: ",
                             "beats=%0d gap_events=%0d gap_cycles=%0d"},
                                IMAGE_TOTAL_BEATS, image_gap_events,
                                image_gap_cycles_total), UVM_LOW)

        // STEP 4
        // IMAGE_READ_DONE 기다리기
        poll_count = 0;

        while (!image_read_done_seen && poll_count < 5000) begin

            repeat (1000) @(posedge vif.clk);

            axil_read(12'h004, final_status, resp);

            if (resp != 2'b00) begin

                `uvm_fatal(get_type_name(),
                           $sformatf("STATUS read failed resp=0x%0h", resp))

            end

            // ERROR_PENDING
            if (final_status[2]) begin

                `uvm_fatal(get_type_name(),
                           $sformatf({"DUT error during S02. ", "STATUS=0x%08h"
                                         }, final_status))

            end

            // IMAGE_READ_DONE
            if (final_status[3]) begin

                image_read_done_seen = 1'b1;

            end

            poll_count++;

        end

        if (!image_read_done_seen) begin

            `uvm_fatal(get_type_name(),
                       "S02 timeout waiting for IMAGE_READ_DONE")

        end

        `uvm_info(get_type_name(), $sformatf({"S02 IMAGE_READ_DONE observed. ",
                                              "STATUS=0x%08h"}, final_status),
                  UVM_LOW)

        `uvm_info(get_type_name(), "S02 Input VALID Gap sequence completed",
                  UVM_LOW)

    endtask

endclass
