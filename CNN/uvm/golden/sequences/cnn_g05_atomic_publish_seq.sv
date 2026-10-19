class cnn_g05_atomic_publish_seq extends cnn_g04_multiframe_seq;

    `uvm_object_utils(cnn_g05_atomic_publish_seq)

    int unsigned base_frame_id;

    string g05_image_dir;
    string g05_golden_dir;


    function new(string name = "cnn_g05_atomic_publish_seq");

        super.new(name);

        base_frame_id = 121;

    endfunction


    task body();

        int unsigned second_frame_id;

        `uvm_info("G05", "G05 atomic publish sequence started", UVM_LOW)


        if (!$value$plusargs("G05_IMAGE_DIR=%s", g05_image_dir)) begin

            g05_image_dir = "image_hex";

        end


        if (!$value$plusargs("G05_GOLDEN_DIR=%s", g05_golden_dir)) begin

            g05_golden_dir = "python_golden";

        end


        void'($value$plusargs("G05_BASE_FRAME=%d", base_frame_id));


        joint_threshold = 8'hD2;

        void'($value$plusargs("JOINT_THRESHOLD=%h", joint_threshold));


        g04_image_dir   = g05_image_dir;
        g04_golden_dir  = g05_golden_dir;

        second_frame_id = base_frame_id + 1;


        `uvm_info("G05", $sformatf("Frame pair: %0d -> %0d", base_frame_id, second_frame_id),
                  UVM_LOW)


        // 첫 frame을 public result로 만든다.
        run_one_frame(base_frame_id, 1);


        // 다음 frame 실행을 위해 done_pending 해제
        clear_done_pending();


        // 두 번째 frame 계산 중 public result는
        // monitor가 매 cycle 첫 frame 값과 비교한다.
        run_one_frame(second_frame_id, 2);


        `uvm_info("G05", "G05 stimulus completed", UVM_LOW)

    endtask

endclass
