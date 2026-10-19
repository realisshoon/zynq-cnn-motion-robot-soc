class cnn_g05_atomic_monitor extends uvm_component;

    `uvm_component_utils(cnn_g05_atomic_monitor)

    virtual cnn_g05_probe_if vif;

    bit active_frame;
    bit prev_busy;

    logic [31:0] start_seq;
    logic start_bank;

    logic [543:0] snap_joint_words;
    logic [16:0]  snap_joint_flags;

    logic [31:0] snap_red_word;
    logic [31:0] snap_blue_word;
    logic [31:0] snap_green_word;

    logic [31:0] snap_result_frame_id;

    int unsigned frame_start_count;
    int unsigned publish_count;
    int unsigned atomic_error_count;


    function new(
        string name = "cnn_g05_atomic_monitor",
        uvm_component parent = null
    );

        super.new(name, parent);

    endfunction


    function void build_phase(uvm_phase phase);

        super.build_phase(phase);

        if (!uvm_config_db#(
                virtual cnn_g05_probe_if
            )::get(
                this,
                "",
                "g05_vif",
                vif
            )) begin

            `uvm_fatal(
                get_type_name(),
                "Missing g05_vif"
            )

        end

        active_frame       = 1'b0;
        prev_busy          = 1'b0;
        frame_start_count  = 0;
        publish_count      = 0;
        atomic_error_count = 0;

    endfunction


    function bit public_changed();

        return
            (vif.joint_words     !== snap_joint_words)     ||
            (vif.joint_flags     !== snap_joint_flags)     ||
            (vif.red_word        !== snap_red_word)        ||
            (vif.blue_word       !== snap_blue_word)       ||
            (vif.green_word      !== snap_green_word)      ||
            (vif.result_frame_id !== snap_result_frame_id);

    endfunction


    task capture_public_snapshot();

        start_seq  = vif.result_seq;
        start_bank = vif.published_bank;

        snap_joint_words =
            vif.joint_words;

        snap_joint_flags =
            vif.joint_flags;

        snap_red_word =
            vif.red_word;

        snap_blue_word =
            vif.blue_word;

        snap_green_word =
            vif.green_word;

        snap_result_frame_id =
            vif.result_frame_id;

    endtask


    task run_phase(uvm_phase phase);

        forever begin

            @(posedge vif.clk);

            // DUT nonblocking assignment 반영 후 확인
            #1ps;

            if (!vif.rst_n) begin

                active_frame = 1'b0;
                prev_busy    = 1'b0;

            end else begin

                // 새로운 frame 시작
                if (vif.busy && !prev_busy) begin

                    capture_public_snapshot();

                    active_frame = 1'b1;

                    frame_start_count++;

                    `uvm_info(
                        "G05_ATOMIC",
                        $sformatf(
                            "FRAME START #%0d seq=%0d frame_id=%0d published_bank=%0d",
                            frame_start_count,
                            start_seq,
                            snap_result_frame_id,
                            start_bank
                        ),
                        UVM_LOW
                    )

                end


                if (active_frame) begin

                    // seq가 그대로면 publish 전 상태
                    if (vif.result_seq === start_seq) begin

                        if (public_changed()) begin

                            atomic_error_count++;

                            `uvm_error(
                                "G05_ATOMIC",
                                $sformatf(
                                    "PUBLIC RESULT CHANGED BEFORE PUBLISH: seq=%0d frame_id_before=%0d frame_id_now=%0d",
                                    start_seq,
                                    snap_result_frame_id,
                                    vif.result_frame_id
                                )
                            )

                        end

                    end else begin

                        // publish는 seq가 정확히 +1 되어야 함
                        if (
                            vif.result_seq !==
                            (start_seq + 32'd1)
                        ) begin

                            atomic_error_count++;

                            `uvm_error(
                                "G05_ATOMIC",
                                $sformatf(
                                    "RESULT_SEQ jump: before=%0d after=%0d",
                                    start_seq,
                                    vif.result_seq
                                )
                            )

                        end


                        // publish 시 bank selector가 바뀌어야 함
                        if (
                            vif.published_bank ===
                            start_bank
                        ) begin

                            atomic_error_count++;

                            `uvm_error(
                                "G05_ATOMIC",
                                "published_bank did not toggle on publish"
                            )

                        end


                        if (!vif.done_pending) begin

                            atomic_error_count++;

                            `uvm_error(
                                "G05_ATOMIC",
                                "done_pending was not asserted on publish"
                            )

                        end


                        publish_count++;

                        if (atomic_error_count == 0)
                            golden_cov.sample_publish(start_bank, vif.published_bank);

                        `uvm_info(
                            "G05_ATOMIC",
                            $sformatf(
                                "ATOMIC PUBLISH #%0d seq=%0d frame_id=%0d bank=%0d",
                                publish_count,
                                vif.result_seq,
                                vif.result_frame_id,
                                vif.published_bank
                            ),
                            UVM_LOW
                        )

                        active_frame = 1'b0;

                    end

                end


                prev_busy = vif.busy;

            end

        end

    endtask


    function void report_phase(uvm_phase phase);

        super.report_phase(phase);

        if (frame_start_count != 2) begin

            `uvm_error(
                "G05_ATOMIC",
                $sformatf(
                    "Expected 2 frame starts, observed %0d",
                    frame_start_count
                )
            )

        end


        if (publish_count != 2) begin

            `uvm_error(
                "G05_ATOMIC",
                $sformatf(
                    "Expected 2 publishes, observed %0d",
                    publish_count
                )
            )

        end


        if (atomic_error_count == 0 &&
            frame_start_count == 2 &&
            publish_count == 2) begin

            `uvm_info(
                "G05_ATOMIC",
                "============================================================",
                UVM_LOW
            )

            `uvm_info(
                "G05_ATOMIC",
                "G05 ATOMIC PUBLISH CHECK PASS",
                UVM_LOW
            )

            `uvm_info(
                "G05_ATOMIC",
                "Public result stayed stable until publish",
                UVM_LOW
            )

            `uvm_info(
                "G05_ATOMIC",
                "Bank selector / RESULT_SEQ changed only at publish",
                UVM_LOW
            )

            `uvm_info(
                "G05_ATOMIC",
                "============================================================",
                UVM_LOW
            )

        end else begin

            `uvm_error(
                "G05_ATOMIC",
                $sformatf(
                    "G05 ATOMIC PUBLISH CHECK FAIL errors=%0d",
                    atomic_error_count
                )
            )

        end

    endfunction

endclass
