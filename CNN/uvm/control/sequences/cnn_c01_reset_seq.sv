class cnn_c01_reset_seq extends cnn_ctrl_base_seq;

    `uvm_object_utils(cnn_c01_reset_seq)

    function new(string name = "cnn_c01_reset_seq");
        super.new(name);
    endfunction


    task body();

        int i;


        `uvm_info(
            "C01",
            "C01 Reset Default Register test start",
            UVM_LOW
        )


        // -----------------------------------------------------
        // Control / Status
        // -----------------------------------------------------

        read_check(
            REG_CONTROL,
            32'h0000_0000,
            "CONTROL"
        );

        read_check(
            REG_STATUS,
            32'h0000_0000,
            "STATUS"
        );


        // -----------------------------------------------------
        // Configuration Default Values
        // -----------------------------------------------------

        read_check(
            REG_THRESHOLD,
            32'h0000_00D2,
            "THRESHOLD"
        );

        read_check(
            REG_RED_THRESHOLD,
            32'h0064_64A0,
            "RED_THRESHOLD"
        );

        read_check(
            REG_BLUE_THRESHOLD,
            32'h00A0_6464,
            "BLUE_THRESHOLD"
        );

        read_check(
            REG_MIN_COUNT,
            32'h0000_0008,
            "MIN_COUNT"
        );


        // -----------------------------------------------------
        // Fixed Read Values
        // -----------------------------------------------------

        read_check(
            REG_CONST_00C,
            32'h0000_0005,
            "REG_00C"
        );

        read_check(
            REG_CONST_010,
            32'h0000_0118,
            "REG_010"
        );

        read_check(
            REG_CONST_014,
            32'h0000_0000,
            "REG_014"
        );


        // -----------------------------------------------------
        // Result / Status Registers
        // Reset Default = 0
        // -----------------------------------------------------

        for (i = 0; i < 17; i++) begin

            read_check(
                REG_JOINT_BASE + (i * 4),
                32'h0000_0000,
                $sformatf("JOINT_WORD[%0d]", i)
            );

        end

        read_check(
            REG_JOINT_FLAGS,
            32'h0000_0000,
            "JOINT_FLAGS"
        );

        read_check(
            REG_RED_WORD,
            32'h0000_0000,
            "RED_WORD"
        );

        read_check(
            REG_BLUE_WORD,
            32'h0000_0000,
            "BLUE_WORD"
        );

        read_check(
            REG_RESULT_SEQ,
            32'h0000_0000,
            "RESULT_SEQ"
        );

        read_check(
            REG_ERROR_CODE,
            32'h0000_0000,
            "ERROR_CODE"
        );

        read_check(
            REG_CYCLE_COUNT,
            32'h0000_0000,
            "CYCLE_COUNT"
        );

        read_check(
            REG_RESULT_FRAME_ID,
            32'h0000_0000,
            "RESULT_FRAME_ID"
        );


        // -----------------------------------------------------
        // Software Configuration Registers
        // -----------------------------------------------------

        read_check(
            REG_FRAME_ID,
            32'h0000_0000,
            "FRAME_ID"
        );

        read_check(
            REG_IRQ_ENABLE,
            32'h0000_0000,
            "IRQ_ENABLE"
        );


        // -----------------------------------------------------
        // Address Configuration Defaults
        // -----------------------------------------------------

        read_check(
            REG_WGT_BASE,
            32'h1000_0000,
            "WGT_BASE"
        );

        read_check(
            REG_FM_A_BASE,
            32'h1100_0000,
            "FM_A_BASE"
        );

        read_check(
            REG_FM_B_BASE,
            32'h1110_0000,
            "FM_B_BASE"
        );

        read_check(
            REG_SG_DESC_BASE,
            32'h1120_0000,
            "SG_DESC_BASE"
        );

        read_check(
            REG_FRAME_BASE,
            32'h0A00_0000,
            "FRAME_BASE"
        );

        read_check(
            REG_TIMEOUT,
            32'h05F5_E100,
            "TIMEOUT"
        );


        // -----------------------------------------------------
        // Fixed Read Values
        // -----------------------------------------------------

        read_check(
            REG_CONST_0A4,
            32'h0004_0000,
            "REG_0A4"
        );

        read_check(
            REG_CONST_0A8,
            32'h0000_0005,
            "REG_0A8"
        );

        read_check(
            REG_CONST_0AC,
            32'hC985_4BB2,
            "REG_0AC"
        );


        `uvm_info(
            "C01",
            "C01 Reset Default Register test completed",
            UVM_LOW
        )

    endtask

endclass