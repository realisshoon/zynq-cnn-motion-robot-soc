class cnn_c02_axil_seq extends cnn_ctrl_base_seq;

    `uvm_object_utils(cnn_c02_axil_seq)


    function new(string name = "cnn_c02_axil_seq");
        super.new(name);
    endfunction


    task body();

        `uvm_info(
            "C02",
            "C02 AXI-Lite Register Legality test start",
            UVM_LOW
        )


        // -----------------------------------------------------
        // C02-1. Normal Write
        // THRESHOLD : 0xD2 -> 0x5A
        // Expected BRESP = OKAY
        // -----------------------------------------------------

        write_resp_check(
            REG_THRESHOLD,
            32'h0000_005A,
            4'hF,
            2'b00,
            "THRESHOLD VALID WRITE"
        );


        // -----------------------------------------------------
        // Write value Read-back
        // -----------------------------------------------------

        read_check(
            REG_THRESHOLD,
            32'h0000_005A,
            "THRESHOLD READBACK"
        );


        // -----------------------------------------------------
        // Restore Reset Default
        // -----------------------------------------------------

        write_resp_check(
            REG_THRESHOLD,
            32'h0000_00D2,
            4'hF,
            2'b00,
            "THRESHOLD RESTORE"
        );


        read_check(
            REG_THRESHOLD,
            32'h0000_00D2,
            "THRESHOLD RESTORE READBACK"
        );


        `uvm_info(
            "C02",
            "C02 AXI-Lite Register Legality test completed",
            UVM_LOW
        )

    endtask

endclass