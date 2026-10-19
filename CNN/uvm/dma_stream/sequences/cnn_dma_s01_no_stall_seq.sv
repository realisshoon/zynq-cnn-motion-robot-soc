class cnn_dma_s01_no_stall_seq extends cnn_dma_stream_base_seq;

    `uvm_object_utils(cnn_dma_s01_no_stall_seq)

    // Test에서 주입
    virtual cnn_if        vif;

    // Test에서 결과 확인용
    bit            [31:0] final_status;
    bit                   image_read_done_seen;

    localparam int unsigned WEIGHT_BEATS = 120;
    localparam int unsigned IMAGE_ROWS = 144;
    localparam int unsigned IMAGE_BEATS_PER_ROW = 480;
    localparam int unsigned IMAGE_TOTAL_BEATS   =
        IMAGE_ROWS * IMAGE_BEATS_PER_ROW;

    function new(string name = "cnn_dma_s01_no_stall_seq");
        super.new(name);

        final_status         = 32'd0;
        image_read_done_seen = 1'b0;
    endfunction

    task pre_body();

        super.pre_body();

        if (vif == null) begin
            `uvm_fatal(get_type_name(), "vif was not assigned to S01 sequence")
        end

    endtask

    task body();

        int unsigned beat;
        int unsigned row;
        int unsigned col_beat;
        int unsigned poll_count;

        bit [1:0] resp;

        `uvm_info(get_type_name(), "S01 No-Stall Stage0 Baseline START",
                  UVM_LOW)

        // STEP 1
        // DUT START
        axil_write(12'h000, 32'h0000_0001);

        // Weight DMA가 실제로 arm될 때까지 기다림
        //
        // WEIGHT DMA LENGTH
        // 0x4041_0028 = 0x0000_03C0
        //
        // 0x3C0 = 960 bytes
        wait_dma_write_commit(vif, 32'h4041_0028, 32'h0000_03C0);

        `uvm_info(get_type_name(),
                  "Weight DMA control completed. Starting Weight AXIS stream.",
                  UVM_LOW)

        // STEP 2
        // Conv0 Weight
        //
        // 960 bytes
        // 64bit = 8 bytes / beat
        // 120 beats
        //
        // Smoke/baseline 목적이므로 data는 zero 사용.
        // Conv0 parameter 영역의 zero 값도 legal format.
        for (beat = 0; beat < WEIGHT_BEATS; beat++) begin

            send_stream(CNN_WEIGHT_BEAT, 64'h0000_0000_0000_0000, 8'hFF,
                        (beat == WEIGHT_BEATS - 1));

        end

        `uvm_info(get_type_name(),
                  "S01 Weight stream completed: 120 beats / 960 bytes", UVM_LOW)

        // IMAGE DMA SG start까지 기다림
        //
        // SG base = 0x1120_0000
        // taildesc = base + 9152
        //          = 0x1120_23C0
        wait_dma_write_commit(vif, 32'h4040_0010, 32'h1120_23C0);

        `uvm_info(get_type_name(),
                  "Image DMA control completed. Starting Image AXIS stream.",
                  UVM_LOW)

        // STEP 3
        // IMAGE DMA Stream
        //
        // 144 selected rows
        // 3840 bytes / row
        // 480 beats / row
        //
        // 각 row 마지막 beat에서 TLAST = 1
        for (row = 0; row < IMAGE_ROWS; row++) begin

            for (col_beat = 0; col_beat < IMAGE_BEATS_PER_ROW; col_beat++) begin

                send_stream(CNN_IMAGE_BEAT,

                            // baseline에서는 black frame 사용
                            64'h0000_0000_0000_0000, 8'hFF,

                            // 각 row의 마지막 beat
                            (col_beat == IMAGE_BEATS_PER_ROW - 1));
            end
        end

        `uvm_info(get_type_name(), $sformatf(
                                       "S01 Image stream completed: %0d beats",
                                       IMAGE_TOTAL_BEATS), UVM_LOW)

        // STEP 4
        // Stage0가 완전히 끝날 때까지 STATUS polling
        //
        // STATUS 0x004
        //
        // bit3 = IMAGE_READ_DONE
        // bit2 = error_pending
        // bit1 = busy
        // bit0 = done_pending
        poll_count = 0;

        while (!image_read_done_seen && poll_count < 5000) begin

            // 계속 AXI-Lite를 두드리지 않고
            // 1000 cycle마다 상태 확인
            repeat (1000) @(posedge vif.clk);

            axil_read(12'h004, final_status, resp);

            if (resp != 2'b00) begin

                `uvm_fatal(get_type_name(),
                           $sformatf("STATUS read failed: resp=0x%0h", resp))

            end

            // error_pending
            if (final_status[2]) begin

                `uvm_fatal(
                    get_type_name(),
                    $sformatf(
                        "DUT error_pending asserted during S01. STATUS=0x%08h",
                        final_status))

            end

            if (final_status[3]) begin

                image_read_done_seen = 1'b1;

            end

            poll_count++;
        end

        if (!image_read_done_seen) begin

            `uvm_fatal(get_type_name(),
                       "S01 timeout waiting for IMAGE_READ_DONE")

        end

        `uvm_info(get_type_name(),
                  $sformatf("S01 IMAGE_READ_DONE observed. STATUS=0x%08h",
                            final_status), UVM_LOW)

        `uvm_info(get_type_name(),
                  "S01 No-Stall Stage0 Baseline sequence completed", UVM_LOW)
    endtask

endclass
