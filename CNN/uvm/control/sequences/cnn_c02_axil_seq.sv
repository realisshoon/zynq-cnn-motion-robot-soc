class cnn_c02_axil_seq extends cnn_ctrl_base_seq;

    `uvm_object_utils(cnn_c02_axil_seq)


    function new(string name = "cnn_c02_axil_seq");
        super.new(name);
    endfunction


    task body();

        `uvm_info("C02", "C02 AXI-Lite Register Legality test start", UVM_LOW)


        // -----------------------------------------------------
        // C02-1. Normal Write
        // THRESHOLD : 0xD2 -> 0x5A
        // Expected BRESP = OKAY
        // -----------------------------------------------------

        write_resp_check(REG_THRESHOLD, 32'h0000_005A, 4'hF, 2'b00,
                         "THRESHOLD VALID WRITE");


        // -----------------------------------------------------
        // Write value Read-back
        // -----------------------------------------------------

        read_check(REG_THRESHOLD, 32'h0000_005A, "THRESHOLD READBACK");


        // -----------------------------------------------------
        // Restore Reset Default
        // -----------------------------------------------------

        write_resp_check(REG_THRESHOLD, 32'h0000_00D2, 4'hF, 2'b00,
                         "THRESHOLD RESTORE");


        read_check(REG_THRESHOLD, 32'h0000_00D2, "THRESHOLD RESTORE READBACK");

        // -----------------------------------------------------
        // C02-2. WSTRB Partial Write
        //
        // FRAME_ID initial value = 0x00000000
        //
        // WDATA = 0xAABB_CCDD
        // WSTRB = 4'b0101
        //
        // byte3 : AA -> not written
        // byte2 : BB -> written
        // byte1 : CC -> not written
        // byte0 : DD -> written
        //
        // Expected = 0x00BB_00DD
        // -----------------------------------------------------

        read_check(REG_FRAME_ID, 32'h0000_0000, "FRAME_ID INITIAL");


        write_resp_check(REG_FRAME_ID, 32'hAABB_CCDD, 4'b0101, 2'b00,
                         "FRAME_ID PARTIAL WRITE");


        read_check(REG_FRAME_ID, 32'h00BB_00DD, "FRAME_ID PARTIAL READBACK");

        // -----------------------------------------------------
        // C02-3. Unaligned Address
        //
        // AXI-Lite register address must be 4-byte aligned.
        // 0x009 is not aligned because addr[1:0] != 2'b00.
        //
        // Expected:
        //   Write BRESP = SLVERR (2'b10)
        //   Read  RRESP = SLVERR (2'b10)
        // -----------------------------------------------------

        write_resp_check(12'h009, 32'hDEAD_BEEF, 4'hF, 2'b10,
                         "UNALIGNED WRITE");


        read_resp_check(12'h009, 2'b10, "UNALIGNED READ");


        // Illegal write must not modify THRESHOLD.
        read_check(REG_THRESHOLD, 32'h0000_00D2,
                   "THRESHOLD AFTER UNALIGNED WRITE");

        // -----------------------------------------------------
        // C02-4. Unmapped Aligned Address
        //
        // 0x104 is 4-byte aligned,
        // but no register is mapped to this address.
        //
        // Expected:
        //   Write BRESP = SLVERR (2'b10)
        //   Read  RRESP = SLVERR (2'b10)
        // -----------------------------------------------------

        write_resp_check(12'h104, 32'hCAFE_BABE, 4'hF, 2'b10, "UNMAPPED WRITE");


        read_resp_check(12'h104, 2'b10, "UNMAPPED READ");


        // Illegal access must not affect a valid register.
        read_check(REG_THRESHOLD, 32'h0000_00D2,
                   "THRESHOLD AFTER UNMAPPED WRITE");

        // -----------------------------------------------------
        // C02-5. Write to Read-Only / Fixed Read Register
        //
        // 0x0A4 is a valid mapped read address (VERSION),
        // but it is not writable.
        //
        // Expected:
        //   Write BRESP = SLVERR (2'b10)
        //   Read value remains 0x00040003
        // -----------------------------------------------------

        read_check(12'h0A4, 32'h0004_0003, "VERSION INITIAL");


        write_resp_check(12'h0A4, 32'hDEAD_BEEF, 4'hF, 2'b10,
                         "VERSION RO WRITE");


        read_check(12'h0A4, 32'h0004_0003, "VERSION AFTER RO WRITE");

        // -----------------------------------------------------
        // Restore Reset Default
        // -----------------------------------------------------

        write_resp_check(REG_FRAME_ID, 32'h0000_0000, 4'hF, 2'b00,
                         "FRAME_ID RESTORE");


        read_check(REG_FRAME_ID, 32'h0000_0000, "FRAME_ID RESTORE READBACK");

        `uvm_info("C02", "C02 AXI-Lite Register Legality test completed",
                  UVM_LOW)

    endtask

endclass
