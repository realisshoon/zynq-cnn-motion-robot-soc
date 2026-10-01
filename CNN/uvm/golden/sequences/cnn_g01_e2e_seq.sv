class cnn_g01_e2e_seq extends cnn_golden_base_seq;

    `uvm_object_utils(cnn_g01_e2e_seq)

    // 1280 RGB888 한 줄 = 3840 byte = 480 beat
    // 5줄 간격으로 144줄 사용
    localparam int IMAGE_BEATS_PER_ROW = 480;
    localparam int IMAGE_DMA_ROWS = 144;
    localparam int IMAGE_TOTAL_BEATS = 69120;

    bit [31:0] golden_joint[0:16];
    bit [31:0] golden_joint_flags;

    int unsigned test_frame_id;
    bit [7:0] joint_threshold;
    string golden_final_hex_path;
    string image_hex_path;


    function new(string name = "cnn_g01_e2e_seq");
        super.new(name);
    endfunction

    task load_test_config();

        if (!$value$plusargs("FRAME_ID=%d", test_frame_id)) begin
            `uvm_fatal(get_type_name(), "Missing +FRAME_ID=<id>")
        end

        joint_threshold = 8'hD2;

        void'($value$plusargs("JOINT_THRESHOLD=%h", joint_threshold));

        `uvm_info(get_type_name(),
                  $sformatf("G01 test frame=%0d threshold=0x%02h signed=%0d", test_frame_id,
                            joint_threshold, $signed(joint_threshold)), UVM_LOW)

    endtask

    task configure_g01();

        bit [31:0] status;
        bit [ 1:0] resp;

        `uvm_info(get_type_name(), "G01 configuration start", UVM_LOW)

        // frame_00121 테스트용 설정값
        // axil_write(12'h008, 32'h0000_00D2);  // joint threshold
        axil_write(12'h008, {24'd0, joint_threshold});
        axil_write(12'h060, 32'h0064_64A0);  // red threshold
        axil_write(12'h064, 32'h00A0_6464);  // blue threshold
        // frame_00121 테스트용 설정값
        // axil_write(12'h070, 32'd121);  // frame id

        // G01 테스트 설정값
        axil_write(12'h070, test_frame_id);
        axil_write(12'h088, 32'd8);  // marker min count

        // DMA memory base
        axil_write(12'h08C, 32'h1000_0000);  // weight
        axil_write(12'h090, 32'h1100_0000);  // feature A
        axil_write(12'h094, 32'h1110_0000);  // feature B
        axil_write(12'h098, 32'h1120_0000);  // SG descriptor
        axil_write(12'h09C, 32'h0A00_0000);  // frame

        // watchdog timeout
        axil_write(12'h0A0, 32'h05F5_E100);

        // START 전 idle 상태 확인
        axil_read(12'h004, status, resp);

        if (resp != 2'b00) begin
            `uvm_fatal(get_type_name(), $sformatf("STATUS read failed before START: resp=%0b",
                                                  resp))
        end

        if (status[2:0] != 3'b000) begin
            `uvm_fatal(get_type_name(), $sformatf("DUT not idle before START: status=0x%08h",
                                                  status))
        end

        `uvm_info(get_type_name(), $sformatf("G01 configuration complete: status=0x%08h", status),
                  UVM_LOW)

    endtask


    task start_accelerator();

        bit [31:0] status;
        bit [ 1:0] resp;

        `uvm_info(get_type_name(), "Issuing CNN START", UVM_LOW)

        // CNN start
        axil_write(12'h000, 32'h0000_0001);

        // START는 pulse라 STATUS.busy로 확인
        axil_read(12'h004, status, resp);

        if (resp != 2'b00) begin
            `uvm_fatal(get_type_name(), $sformatf("STATUS read failed after START: resp=%0b", resp))
        end

        if (!status[1]) begin

            `uvm_error(get_type_name(),
                       $sformatf("BUSY was not asserted after START: status=0x%08h", status))

        end else begin

            `uvm_info(get_type_name(), $sformatf("START accepted: status=0x%08h BUSY=1", status),
                      UVM_LOW)

        end

    endtask


    task send_image_frame();

        int        fd;
        int        scan_result;
        int        beat_idx;

        bit [63:0] image_data;
        bit        image_last;

        // frame_00121을 AXI stream용 HEX로 변환한 파일
        if (!$value$plusargs("IMAGE_HEX=%s", image_hex_path)) begin
            `uvm_fatal(get_type_name(), "Missing +IMAGE_HEX=<path>")
        end

        fd = $fopen(image_hex_path, "r");

        if (fd == 0) begin
            `uvm_fatal(get_type_name(), $sformatf("Cannot open IMAGE hex file: %s", image_hex_path))
        end

        `uvm_info(get_type_name(), $sformatf("Loading IMAGE frame: %s", image_hex_path), UVM_LOW)

        for (beat_idx = 0; beat_idx < IMAGE_TOTAL_BEATS; beat_idx++) begin

            scan_result = $fscanf(fd, "%h", image_data);

            if (scan_result != 1) begin

                $fclose(fd);

                `uvm_fatal(get_type_name(), $sformatf("IMAGE hex read failed at beat %0d",
                                                      beat_idx))

            end

            // 한 row의 마지막 beat에서 TLAST
            image_last = ((beat_idx % IMAGE_BEATS_PER_ROW) == (IMAGE_BEATS_PER_ROW - 1));

            send_stream(CNN_IMAGE_BEAT, image_data, 8'hFF, image_last);

        end

        $fclose(fd);

        `uvm_info(get_type_name(), $sformatf("IMAGE frame sent: %0d beats", IMAGE_TOTAL_BEATS),
                  UVM_LOW)

    endtask


    task send_weight_op(int op_id);

        int           fd;
        int           scan_result;
        int           beat_count;
        int           last_value;

        bit    [63:0] weight_data;
        bit           weight_last;

        string        weight_dir;
        string        weight_hex_path;

        if (!$value$plusargs("WEIGHT_DIR=%s", weight_dir)) begin
            `uvm_fatal(get_type_name(), "Missing +WEIGHT_DIR=<path>")
        end

        // OP별 weight stream 파일
        weight_hex_path = $sformatf("%s/op_%02d.hex", weight_dir, op_id);

        fd = $fopen(weight_hex_path, "r");

        if (fd == 0) begin
            `uvm_fatal(get_type_name(), $sformatf("Cannot open weight file: %s", weight_hex_path))
        end

        `uvm_info(get_type_name(), $sformatf("Loading weight OP %0d: %s", op_id, weight_hex_path),
                  UVM_LOW)

        beat_count = 0;

        while (!$feof(
            fd
        )) begin

            scan_result = $fscanf(fd, "%h %d", weight_data, last_value);

            if (scan_result == 2) begin

                if ((last_value != 0) && (last_value != 1)) begin

                    $fclose(fd);

                    `uvm_fatal(get_type_name(), $sformatf("Invalid LAST value at OP %0d beat %0d",
                                                          op_id, beat_count))

                end

                weight_last = last_value[0];

                send_stream(CNN_WEIGHT_BEAT, weight_data, 8'hFF, weight_last);

                beat_count++;

            end else if (!$feof(fd)) begin

                $fclose(fd);

                `uvm_fatal(get_type_name(), $sformatf("Weight hex parse failed: OP=%0d beat=%0d",
                                                      op_id, beat_count))

            end

        end

        $fclose(fd);

        `uvm_info(get_type_name(), $sformatf("Weight OP %0d sent: %0d beats", op_id, beat_count),
                  UVM_LOW)

    endtask


    task wait_stage0_complete();

        bit [31:0] status;
        bit [1:0] resp;

        int unsigned poll_count;

        poll_count = 0;

        `uvm_info(get_type_name(), "Waiting for Stage0 completion", UVM_LOW)

        forever begin

            axil_read(12'h004, status, resp);

            if (resp != 2'b00) begin

                `uvm_fatal(get_type_name(),
                           $sformatf("STATUS read failed while waiting Stage0: resp=%0b", resp))

            end

            // 중간에 error가 발생하면 바로 종료
            if (status[2]) begin

                `uvm_fatal(get_type_name(), $sformatf("DUT error during Stage0: status=0x%08h",
                                                      status))

            end

            // Stage0 image 처리가 끝날 때까지 대기
            if (status[3]) begin

                `uvm_info(get_type_name(), $sformatf("Stage0 complete: status=0x%08h polls=%0d",
                                                     status, poll_count), UVM_LOW)

                break;

            end

            poll_count++;

            if (poll_count > 200000) begin
                `uvm_fatal(get_type_name(), "Timeout waiting for Stage0 completion")
            end

        end

    endtask


    task send_feature_stage(int src_stage);

        int           fd;
        int           scan_result;
        int           beat_count;
        int           last_value;

        bit    [63:0] feature_data;
        bit    [ 7:0] feature_keep;
        bit           feature_last;

        string        feature_dir;
        string        feature_path;

        if (!$value$plusargs("FEATURE_DIR=%s", feature_dir)) begin
            `uvm_fatal(get_type_name(), "Missing +FEATURE_DIR=<path>")
        end

        // 이전 stage에서 저장한 feature를 다시 입력
        feature_path = $sformatf("%s/stage_%02d.hex", feature_dir, src_stage);

        fd = $fopen(feature_path, "r");

        if (fd == 0) begin
            `uvm_fatal(get_type_name(), $sformatf("Cannot open feature file: %s", feature_path))
        end

        `uvm_info(get_type_name(), $sformatf("Replaying Stage%0d feature: %s", src_stage,
                                             feature_path), UVM_LOW)

        beat_count = 0;

        while (!$feof(
            fd
        )) begin

            scan_result = $fscanf(fd, "%h %h %d", feature_data, feature_keep, last_value);

            if (scan_result == 3) begin

                if ((last_value != 0) && (last_value != 1)) begin

                    $fclose(fd);

                    `uvm_fatal(get_type_name(), $sformatf(
                                                    "Invalid feature LAST: stage=%0d beat=%0d",
                                                    src_stage, beat_count))

                end

                feature_last = last_value[0];

                send_stream(CNN_FEATURE_IN_BEAT, feature_data, feature_keep, feature_last);

                beat_count++;

            end else if (!$feof(fd)) begin

                $fclose(fd);

                `uvm_fatal(get_type_name(), $sformatf(
                                                "Feature hex parse failed: stage=%0d beat=%0d",
                                                src_stage, beat_count))

            end

        end

        $fclose(fd);

        `uvm_info(get_type_name(), $sformatf("Stage%0d feature replay complete: %0d beats",
                                             src_stage, beat_count), UVM_LOW)

    endtask


    task wait_feature_stage_done(int stage_id);

        uvm_event stage_done_event;
        string    event_name;

        // feature output이 끝나면 feature memory에서 발생하는 event
        event_name = $sformatf("FEATURE_STAGE_%0d_DONE", stage_id);

        stage_done_event = uvm_event_pool::get_global(event_name);

        `uvm_info(get_type_name(), $sformatf("Waiting for %s", event_name), UVM_LOW)

        stage_done_event.wait_on();

        `uvm_info(get_type_name(), $sformatf("%s observed", event_name), UVM_LOW)

        // 다음 frame에서 다시 사용할 수 있게 reset
        stage_done_event.reset();

    endtask


    task run_body_stage(int stage_id, int dw_op, int pw_op, int src_stage);

        `uvm_info(get_type_name(), $sformatf("Starting Stage%0d", stage_id), UVM_LOW)

        // DW / PW weight load
        send_weight_op(dw_op);
        send_weight_op(pw_op);

        `uvm_info(get_type_name(), $sformatf("Stage%0d weights loaded", stage_id), UVM_LOW)

        // 이전 stage feature 입력
        send_feature_stage(src_stage);

        `uvm_info(get_type_name(), $sformatf("Stage%0d feature input sent", stage_id), UVM_LOW)

        // 현재 stage feature output 완료 대기
        wait_feature_stage_done(stage_id);

        `uvm_info(get_type_name(), $sformatf("G01 Stage%0d completed", stage_id), UVM_LOW)

    endtask


    task feed_head_stage(int stage_id, int op_id);

        `uvm_info(get_type_name(), $sformatf("Starting Head Stage%0d with OP%0d", stage_id, op_id),
                  UVM_LOW)

        send_weight_op(op_id);

        // Heatmap과 Offset은 둘 다 Stage13 feature 사용
        send_feature_stage(13);

        `uvm_info(get_type_name(), $sformatf("Head Stage%0d input sent", stage_id), UVM_LOW)

    endtask


    task wait_g01_done();

        bit [31:0] status;
        bit [1:0] resp;

        int unsigned poll_count;

        poll_count = 0;

        `uvm_info(get_type_name(), "Waiting for final CNN completion", UVM_LOW)

        // 최종 publish 완료까지 STATUS polling
        forever begin

            axil_read(12'h004, status, resp);

            if (resp != 2'b00) begin

                `uvm_fatal(get_type_name(),
                           $sformatf("STATUS read failed while waiting final completion: resp=%0b",
                                     resp))

            end

            // error_pending
            if (status[2]) begin

                `uvm_fatal(get_type_name(),
                           $sformatf("CNN error before final completion: status=0x%08h", status))

            end

            // done_pending
            if (status[0]) begin

                `uvm_info(get_type_name(),
                          $sformatf("CNN final completion observed: status=0x%08h polls=%0d",
                                    status, poll_count), UVM_LOW)

                break;

            end

            poll_count++;

            if (poll_count > 200000) begin
                `uvm_fatal(get_type_name(), "Timeout waiting for final CNN completion")
            end

        end

    endtask


    task read_final_results();

        bit [31:0] data;
        bit [1:0] resp;

        int joint;
        int mismatch_count;

        mismatch_count = 0;

        `uvm_info(get_type_name(), "Reading published CNN result registers", UVM_LOW)

        // 최종 joint 결과를 Golden 값과 비교
        for (joint = 0; joint < 17; joint++) begin

            axil_read(12'h018 + (joint * 4), data, resp);

            if (resp != 2'b00) begin

                `uvm_fatal(get_type_name(), $sformatf("Failed to read joint[%0d]: resp=%0b", joint,
                                                      resp))

            end

            if (data !== golden_joint[joint]) begin

                mismatch_count++;

                `uvm_error("G01_GOLDEN", $sformatf("Joint[%0d] FAIL RTL=0x%08h GOLDEN=0x%08h",
                                                   joint, data, golden_joint[joint]))

            end else begin

                `uvm_info(
                    "G01_GOLDEN", $sformatf(
                    "Joint[%0d] PASS RTL=0x%08h GOLDEN=0x%08h", joint, data, golden_joint[joint]),
                    UVM_LOW)

            end

        end


        // joint valid flag 비교
        axil_read(12'h05C, data, resp);

        if (resp != 2'b00) `uvm_fatal(get_type_name(), "Failed to read JOINT_FLAGS")

        `uvm_info(get_type_name(), $sformatf("G01 RESULT joint_flags = 0x%08h", data), UVM_LOW)

        if (data !== golden_joint_flags) begin

            mismatch_count++;

            `uvm_error("G01_GOLDEN", $sformatf("JOINT_FLAGS FAIL RTL=0x%08h GOLDEN=0x%08h", data,
                                               golden_joint_flags))

        end else begin

            `uvm_info("G01_GOLDEN", $sformatf(
                      "JOINT_FLAGS PASS RTL=0x%08h GOLDEN=0x%08h", data, golden_joint_flags),
                      UVM_LOW)

        end


        // Color 결과는 현재 readout만 확인
        axil_read(12'h068, data, resp);

        if (resp != 2'b00) `uvm_fatal(get_type_name(), "Failed to read RED_RESULT")

        `uvm_info(get_type_name(), $sformatf("G01 RESULT red = 0x%08h", data), UVM_LOW)


        axil_read(12'h06C, data, resp);

        if (resp != 2'b00) `uvm_fatal(get_type_name(), "Failed to read BLUE_RESULT")

        `uvm_info(get_type_name(), $sformatf("G01 RESULT blue = 0x%08h", data), UVM_LOW)


        // 첫 frame이므로 result_seq는 1
        axil_read(12'h074, data, resp);

        if (resp != 2'b00) `uvm_fatal(get_type_name(), "Failed to read RESULT_SEQ")

        `uvm_info(get_type_name(), $sformatf("G01 RESULT result_seq = %0d", data), UVM_LOW)

        if (data != 32'd1) begin

            mismatch_count++;

            `uvm_error("G01_GOLDEN", $sformatf("RESULT_SEQ FAIL expected=1 actual=%0d", data))

        end else begin

            `uvm_info("G01_GOLDEN", "RESULT_SEQ PASS expected=1 actual=1", UVM_LOW)

        end


        // 정상 완료 시 error_status는 0
        axil_read(12'h078, data, resp);

        if (resp != 2'b00) `uvm_fatal(get_type_name(), "Failed to read ERROR_STATUS")

        `uvm_info(get_type_name(), $sformatf("G01 RESULT error_status = 0x%08h", data), UVM_LOW)

        if (data != 32'd0) begin

            mismatch_count++;

            `uvm_error("G01_GOLDEN", $sformatf("ERROR_STATUS FAIL expected=0 actual=0x%08h", data))

        end else begin

            `uvm_info("G01_GOLDEN", "ERROR_STATUS PASS expected=0 actual=0", UVM_LOW)

        end


        // 시작할 때 넣은 frame_id가 publish 결과에도 유지되는지 확인
        axil_read(12'h084, data, resp);

        if (resp != 2'b00) `uvm_fatal(get_type_name(), "Failed to read RESULT_FRAME_ID")

        `uvm_info(get_type_name(), $sformatf("G01 RESULT result_frame_id = %0d", data), UVM_LOW)

        if (data != test_frame_id) begin

            mismatch_count++;

            `uvm_error("G01_GOLDEN", $sformatf("RESULT_FRAME_ID FAIL expected=%0d actual=%0d",
                                               test_frame_id, data))

        end else begin

            `uvm_info("G01_GOLDEN", $sformatf(
                      "RESULT_FRAME_ID PASS expected=%0d actual=%0d", test_frame_id, data), UVM_LOW)

        end


        // 최신 RTL에 추가된 green result
        axil_read(12'h0F8, data, resp);

        if (resp != 2'b00) `uvm_fatal(get_type_name(), "Failed to read GREEN_RESULT")

        `uvm_info(get_type_name(), $sformatf("G01 RESULT green = 0x%08h", data), UVM_LOW)


        if (mismatch_count == 0) begin

            `uvm_info("G01_GOLDEN", "============================================================",
                      UVM_LOW)

            `uvm_info("G01_GOLDEN", "G01 GOLDEN CHECK PASS: 17/17 joints bit-exact matched",
                      UVM_LOW)

            `uvm_info("G01_GOLDEN",
                      "JOINT_FLAGS / RESULT_SEQ / ERROR_STATUS / RESULT_FRAME_ID PASS", UVM_LOW)

            `uvm_info("G01_GOLDEN", "============================================================",
                      UVM_LOW)

        end else begin

            `uvm_error("G01_GOLDEN", $sformatf(
                       "G01 GOLDEN CHECK FAIL: mismatch_count=%0d", mismatch_count))

        end

    endtask


    task load_golden_expected();

        int fd;
        int scan_result;
        int joint;

        // Python 결과에서 만든 Golden HEX
        if (!$value$plusargs("GOLDEN_FINAL_HEX=%s", golden_final_hex_path)) begin

            `uvm_fatal(get_type_name(), "Missing +GOLDEN_FINAL_HEX=<path>")

        end

        fd = $fopen(golden_final_hex_path, "r");

        if (fd == 0) begin

            `uvm_fatal(get_type_name(), $sformatf("Failed to open Golden result file: %s",
                                                  golden_final_hex_path))

        end

        // joint 17개
        for (joint = 0; joint < 17; joint++) begin

            scan_result = $fscanf(fd, "%h", golden_joint[joint]);

            if (scan_result != 1) begin

                `uvm_fatal(get_type_name(), $sformatf("Failed to read Golden joint[%0d]", joint))

            end

        end

        // 마지막 한 줄은 joint_flags
        scan_result = $fscanf(fd, "%h", golden_joint_flags);

        if (scan_result != 1) begin

            `uvm_fatal(get_type_name(), "Failed to read Golden joint_flags")

        end

        $fclose(fd);

        `uvm_info(get_type_name(), $sformatf("Golden expected results loaded: %s",
                                             golden_final_hex_path), UVM_LOW)

    endtask


    task body();

        int stage_id;

        `uvm_info(get_type_name(), "G01 E2E sequence started", UVM_LOW)

        load_test_config();
        load_golden_expected();

        configure_g01();
        start_accelerator();


        // Stage0 Conv0
        send_weight_op(0);
        send_image_frame();
        wait_stage0_complete();

        `uvm_info(get_type_name(), "G01 Stage0 completed", UVM_LOW)


        // Stage1~13 Body
        // DW = 2*N-1, PW = 2*N
        for (stage_id = 1; stage_id <= 13; stage_id++) begin

            run_body_stage(stage_id, (2 * stage_id) - 1, (2 * stage_id), stage_id - 1);

        end

        `uvm_info(get_type_name(), "G01 all body stages completed (Stage1~13)", UVM_LOW)


        // Stage14 Heatmap
        feed_head_stage(14, 27);

        // Stage15 Offset
        // OP28 ready가 열릴 때까지 weight stream에서 대기
        feed_head_stage(15, 28);


        // publish 완료 후 최종 결과 비교
        wait_g01_done();
        read_final_results();

        `uvm_info(get_type_name(), "G01 full CNN control/data flow and result readout completed",
                  UVM_LOW)

    endtask


endclass
