class cnn_g04_multiframe_seq extends cnn_g01_e2e_seq;

    `uvm_object_utils(cnn_g04_multiframe_seq)

    int unsigned start_frame_id;
    int unsigned frame_count;

    string g04_image_dir;
    string g04_golden_dir;


    function new(string name = "cnn_g04_multiframe_seq");
        super.new(name);

        start_frame_id = 121;
        frame_count    = 3;
    endfunction


    task clear_done_pending();

        bit [31:0] status;
        bit [1:0] resp;

        int unsigned poll_count;

        `uvm_info("G04", "Clearing DONE_PENDING before next frame", UVM_LOW)

        // CTRL.DONE_CLEAR
        axil_write(12'h000, 32'h0000_0002);

        poll_count = 0;

        forever begin

            axil_read(12'h004, status, resp);

            if (resp != 2'b00) begin
                `uvm_fatal("G04", $sformatf("STATUS read failed after DONE_CLEAR: resp=%0b", resp))
            end

            if (status[2]) begin
                `uvm_fatal("G04", $sformatf(
                                      "ERROR_PENDING asserted after DONE_CLEAR: status=0x%08h",
                                      status))
            end

            if (!status[0]) begin

                `uvm_info("G04", $sformatf("DONE_PENDING cleared: status=0x%08h", status), UVM_LOW)

                break;

            end

            poll_count++;

            if (poll_count > 100) begin
                `uvm_fatal("G04", "Timeout waiting DONE_PENDING clear")
            end

        end

    endtask


    task run_one_frame(int unsigned frame_id, int unsigned seq_id);

        int stage_id;

        test_frame_id = frame_id;
        expected_result_seq = seq_id;

        image_hex_path = $sformatf("%s/frame_%05d_image.hex", g04_image_dir, frame_id);

        golden_final_hex_path =
            $sformatf("%s/frame_%05d/final_expected.hex", g04_golden_dir, frame_id);


        `uvm_info("G04", "============================================================", UVM_LOW)

        `uvm_info("G04", $sformatf("FRAME START id=%0d expected_seq=%0d", frame_id, seq_id),
                  UVM_LOW)

        `uvm_info("G04", $sformatf("IMAGE=%s", image_hex_path), UVM_LOW)

        `uvm_info("G04", $sformatf("GOLDEN=%s", golden_final_hex_path), UVM_LOW)

        `uvm_info("G04", "============================================================", UVM_LOW)


        load_golden_expected();

        configure_g01();

        start_accelerator();


        // Stage0 Conv0
        send_weight_op(0);

        send_image_frame();

        wait_stage0_complete();


        // Stage1~13 Body
        for (stage_id = 1; stage_id <= 13; stage_id++) begin

            run_body_stage(stage_id, (2 * stage_id) - 1, (2 * stage_id), stage_id - 1);

        end


        // Stage14 Heatmap
        feed_head_stage(14, 27);


        // Stage15 Offset
        feed_head_stage(15, 28);


        // publish 완료
        wait_g01_done();


        // Joint / flags / seq / frame_id 검증
        read_final_results();


        `uvm_info("G04", $sformatf("FRAME PASS id=%0d result_seq=%0d", frame_id, seq_id), UVM_LOW)

    endtask


    task body();

        int unsigned frame_index;
        int unsigned current_frame_id;

        `uvm_info("G04", "G04 multi-frame consistency sequence started", UVM_LOW)


        if (!$value$plusargs("G04_IMAGE_DIR=%s", g04_image_dir)) begin

            g04_image_dir = "image_hex";

        end


        if (!$value$plusargs("G04_GOLDEN_DIR=%s", g04_golden_dir)) begin

            g04_golden_dir = "python_golden";

        end


        void'($value$plusargs("G04_START_FRAME=%d", start_frame_id));


        void'($value$plusargs("G04_FRAME_COUNT=%d", frame_count));


        joint_threshold = 8'hD2;

        void'($value$plusargs("JOINT_THRESHOLD=%h", joint_threshold));


        `uvm_info("G04",
                  $sformatf(
                      "Configuration: start_frame=%0d frame_count=%0d threshold=0x%02h signed=%0d",
                      start_frame_id, frame_count, joint_threshold, $signed(joint_threshold)),
                  UVM_LOW)


        for (frame_index = 0; frame_index < frame_count; frame_index++) begin

            current_frame_id = start_frame_id + frame_index;


            // 첫 frame 이후에는 이전 publish의 DONE_PENDING 해제
            if (frame_index != 0) begin
                clear_done_pending();
            end


            run_one_frame(current_frame_id, frame_index + 1);

        end


        `uvm_info("G04", "============================================================", UVM_LOW)

        `uvm_info("G04", $sformatf("G04 MULTI-FRAME PASS: %0d consecutive frames", frame_count),
                  UVM_LOW)

        `uvm_info("G04", $sformatf("RESULT_SEQ verified: 1 -> %0d", frame_count), UVM_LOW)

        `uvm_info("G04", $sformatf("RESULT_FRAME_ID verified: %0d -> %0d", start_frame_id,
                                   start_frame_id + frame_count - 1), UVM_LOW)

        `uvm_info("G04", "============================================================", UVM_LOW)

    endtask

endclass
