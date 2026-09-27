class cnn_c02_axil_seq extends cnn_ctrl_base_seq;

    `uvm_object_utils(cnn_c02_axil_seq)


    function new(string name = "cnn_c02_axil_seq");
        super.new(name);
    endfunction

    task aw_first_write_check(bit [11:0] addr, bit [31:0] data, bit [3:0] strb,
                              int unsigned gap_cycles, bit [1:0] expected_resp,
                              string test_name);

        cnn_c02_axil_item req;

        req = cnn_c02_axil_item::type_id::create("req");

        start_item(req);

        req.kind       = CNN_AXIL_WRITE;
        req.addr       = addr;
        req.data32     = data;
        req.strb       = strb;

        // C02 special timing control
        req.aw_first   = 1'b1;
        req.gap_cycles = gap_cycles;

        finish_item(req);


        if (req.resp != expected_resp) begin

            `uvm_error(
                "C02_AW_FIRST_RESP",
                $sformatf(
                    "%s response mismatch: addr=0x%03h expected=%02b actual=%02b",
                    test_name, addr, expected_resp, req.resp))

        end else begin

            `uvm_info("C02_AW_FIRST_PASS", $sformatf(
                      "%s PASS: addr=0x%03h gap=%0d resp=%02b",
                      test_name,
                      addr,
                      gap_cycles,
                      req.resp
                      ), UVM_LOW)

        end

    endtask

    task w_first_write_check(bit [11:0] addr, bit [31:0] data, bit [3:0] strb,
                             int unsigned gap_cycles, bit [1:0] expected_resp,
                             string test_name);

        cnn_c02_axil_item req;

        req = cnn_c02_axil_item::type_id::create("req");

        start_item(req);

        req.kind       = CNN_AXIL_WRITE;
        req.addr       = addr;
        req.data32     = data;
        req.strb       = strb;

        req.aw_first   = 1'b0;
        req.w_first    = 1'b1;
        req.gap_cycles = gap_cycles;

        finish_item(req);


        if (req.resp != expected_resp) begin

            `uvm_error(
                "C02_W_FIRST_RESP",
                $sformatf(
                    "%s response mismatch: addr=0x%03h expected=%02b actual=%02b",
                    test_name, addr, expected_resp, req.resp))

        end else begin

            `uvm_info("C02_W_FIRST_PASS", $sformatf(
                      "%s PASS: addr=0x%03h gap=%0d resp=%02b",
                      test_name,
                      addr,
                      gap_cycles,
                      req.resp
                      ), UVM_LOW)

        end

    endtask

    task delayed_bready_write_check(bit [11:0] addr, bit [31:0] data,
                                    bit [3:0] strb, int unsigned delay_cycles,
                                    bit [1:0] expected_resp, string test_name);

        cnn_c02_axil_item req;

        req = cnn_c02_axil_item::type_id::create("req");

        start_item(req);

        req.kind                = CNN_AXIL_WRITE;
        req.addr                = addr;
        req.data32              = data;
        req.strb                = strb;

        req.aw_first            = 1'b0;
        req.w_first             = 1'b0;
        req.delay_bready        = 1'b1;
        req.bready_delay_cycles = delay_cycles;

        finish_item(req);


        if (req.resp != expected_resp) begin

            `uvm_error(
                "C02_DELAY_BREADY_RESP",
                $sformatf(
                    "%s response mismatch: addr=0x%03h expected=%02b actual=%02b",
                    test_name, addr, expected_resp, req.resp))

        end else begin

            `uvm_info("C02_DELAY_BREADY_PASS", $sformatf(
                      "%s PASS: addr=0x%03h delay=%0d resp=%02b",
                      test_name,
                      addr,
                      delay_cycles,
                      req.resp
                      ), UVM_LOW)

        end

    endtask

    task delayed_rready_read_check(bit [11:0] addr, int unsigned delay_cycles,
                                   bit [31:0] expected_data,
                                   bit [1:0] expected_resp, string test_name);

        cnn_c02_axil_item req;

        req = cnn_c02_axil_item::type_id::create("req");

        start_item(req);

        req.kind                = CNN_AXIL_READ;
        req.addr                = addr;

        req.aw_first            = 1'b0;
        req.w_first             = 1'b0;
        req.delay_bready        = 1'b0;
        req.delay_rready        = 1'b1;
        req.rready_delay_cycles = delay_cycles;

        finish_item(req);


        if (req.resp != expected_resp) begin

            `uvm_error(
                "C02_DELAY_RREADY_RESP",
                $sformatf(
                    "%s response mismatch: addr=0x%03h expected_resp=%02b actual_resp=%02b",
                    test_name, addr, expected_resp, req.resp))

        end else if (req.rdata != expected_data) begin

            `uvm_error(
                "C02_DELAY_RREADY_DATA",
                $sformatf(
                    "%s data mismatch: addr=0x%03h expected=0x%08h actual=0x%08h",
                    test_name, addr, expected_data, req.rdata))

        end else begin

            `uvm_info("C02_DELAY_RREADY_PASS", $sformatf(
                      "%s PASS: addr=0x%03h delay=%0d data=0x%08h resp=%02b",
                      test_name,
                      addr,
                      delay_cycles,
                      req.rdata,
                      req.resp
                      ), UVM_LOW)

        end

    endtask

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
        // C02-6. Illegal Register Value
        //
        // THRESHOLD is an 8-bit configuration register.
        // Upper 24 bits must be zero.
        //
        // Write 0x0000015A:
        //   [31:8] != 0  -> illegal
        //
        // Expected:
        //   BRESP = SLVERR (2'b10)
        //   THRESHOLD remains 0x000000D2
        // -----------------------------------------------------

        read_check(REG_THRESHOLD, 32'h0000_00D2,
                   "THRESHOLD BEFORE ILLEGAL VALUE");


        write_resp_check(REG_THRESHOLD, 32'h0000_015A, 4'hF, 2'b10,
                         "THRESHOLD ILLEGAL VALUE");


        read_check(REG_THRESHOLD, 32'h0000_00D2,
                   "THRESHOLD AFTER ILLEGAL VALUE");

        // -----------------------------------------------------
        // C02-7. IRQ_ENABLE Illegal Value
        //
        // IRQ_ENABLE only uses bit[0].
        // Valid values:
        //   0x00000000 -> IRQ Disable
        //   0x00000001 -> IRQ Enable
        //
        // Write 0x00000002:
        //   bit[1] = 1 -> illegal
        //
        // Expected:
        //   BRESP = SLVERR (2'b10)
        //   IRQ_ENABLE remains 0
        // -----------------------------------------------------

        read_check(REG_IRQ_ENABLE, 32'h0000_0000,
                   "IRQ_ENABLE BEFORE ILLEGAL VALUE");


        write_resp_check(REG_IRQ_ENABLE, 32'h0000_0002, 4'hF, 2'b10,
                         "IRQ_ENABLE ILLEGAL VALUE");


        read_check(REG_IRQ_ENABLE, 32'h0000_0000,
                   "IRQ_ENABLE AFTER ILLEGAL VALUE");

        // -----------------------------------------------------
        // C02-8. Base Address Alignment
        //
        // WGT_BASE must be 64-byte aligned.
        // Therefore bits [5:0] must be zero.
        //
        // Valid example:
        //   0x10000000
        //
        // Illegal example:
        //   0x10000001
        //
        // Expected:
        //   BRESP = SLVERR (2'b10)
        //   WGT_BASE remains 0x10000000
        // -----------------------------------------------------

        read_check(REG_WGT_BASE, 32'h1000_0000,
                   "WGT_BASE BEFORE UNALIGNED VALUE");


        write_resp_check(REG_WGT_BASE, 32'h1000_0001, 4'hF, 2'b10,
                         "WGT_BASE UNALIGNED VALUE");


        read_check(REG_WGT_BASE, 32'h1000_0000,
                   "WGT_BASE AFTER UNALIGNED VALUE");

        // -----------------------------------------------------
        // C02-9. TIMEOUT Illegal Zero Value
        //
        // TIMEOUT must not be zero.
        //
        // Write 0x00000000:
        //   merged_value == 0 -> illegal
        //
        // Expected:
        //   BRESP = SLVERR (2'b10)
        //   TIMEOUT remains 0x05F5E100
        // -----------------------------------------------------

        read_check(REG_TIMEOUT, 32'h05F5_E100, "TIMEOUT BEFORE ZERO VALUE");


        write_resp_check(REG_TIMEOUT, 32'h0000_0000, 4'hF, 2'b10,
                         "TIMEOUT ZERO VALUE");


        read_check(REG_TIMEOUT, 32'h05F5_E100, "TIMEOUT AFTER ZERO VALUE");

        // -----------------------------------------------------
        // C02-10. Illegal CONTROL Command
        //
        // Valid CONTROL commands:
        //   3'b001 -> START
        //   3'b010 -> CLEAR_DONE
        //   3'b100 -> SOFT_RESET
        //
        // 3'b011 is not a valid command.
        //
        // Expected:
        //   BRESP = SLVERR (2'b10)
        // -----------------------------------------------------

        write_resp_check(REG_CONTROL, 32'h0000_0003, 4'hF, 2'b10,
                         "CONTROL ILLEGAL COMMAND");


        read_check(REG_CONTROL, 32'h0000_0000, "CONTROL AFTER ILLEGAL COMMAND");

        // -----------------------------------------------------
        // C02-11. AXI-Lite AW-first Write
        //
        // Address channel completes first.
        // W channel is intentionally delayed by 3 cycles.
        //
        // Expected:
        //   AW handshake
        //   3-cycle gap
        //   W handshake
        //   BRESP = OKAY
        //   THRESHOLD = 0x5A
        // -----------------------------------------------------

        read_check(REG_THRESHOLD, 32'h0000_00D2, "THRESHOLD BEFORE AW-FIRST");


        aw_first_write_check(REG_THRESHOLD, 32'h0000_005A, 4'hF, 3, 2'b00,
                             "THRESHOLD AW-FIRST WRITE");


        read_check(REG_THRESHOLD, 32'h0000_005A, "THRESHOLD AFTER AW-FIRST");


        // Restore THRESHOLD
        write_resp_check(REG_THRESHOLD, 32'h0000_00D2, 4'hF, 2'b00,
                         "THRESHOLD AW-FIRST RESTORE");


        read_check(REG_THRESHOLD, 32'h0000_00D2,
                   "THRESHOLD AW-FIRST RESTORE READBACK");

        // -----------------------------------------------------
        // C02-12. AXI-Lite W-first Write
        //
        // W channel completes first.
        // AW channel is intentionally delayed by 3 cycles.
        //
        // Expected:
        //   W handshake
        //   3-cycle gap
        //   AW handshake
        //   BRESP = OKAY
        //   THRESHOLD = 0x5A
        // -----------------------------------------------------

        read_check(REG_THRESHOLD, 32'h0000_00D2, "THRESHOLD BEFORE W-FIRST");


        w_first_write_check(REG_THRESHOLD, 32'h0000_005A, 4'hF, 3, 2'b00,
                            "THRESHOLD W-FIRST WRITE");


        read_check(REG_THRESHOLD, 32'h0000_005A, "THRESHOLD AFTER W-FIRST");


        write_resp_check(REG_THRESHOLD, 32'h0000_00D2, 4'hF, 2'b00,
                         "THRESHOLD W-FIRST RESTORE");


        read_check(REG_THRESHOLD, 32'h0000_00D2,
                   "THRESHOLD W-FIRST RESTORE READBACK");

        // -----------------------------------------------------
        // C02-13. Delayed BREADY
        //
        // AW/W request completes normally.
        // BREADY is intentionally held LOW for 3 cycles.
        //
        // Expected:
        //   DUT asserts BVALID while BREADY = 0
        //   BVALID remains asserted
        //   BRESP remains stable
        //   BREADY is asserted after 3 cycles
        //   BRESP = OKAY
        //   THRESHOLD = 0x5A
        // -----------------------------------------------------

        read_check(REG_THRESHOLD, 32'h0000_00D2,
                   "THRESHOLD BEFORE DELAYED BREADY");


        delayed_bready_write_check(REG_THRESHOLD, 32'h0000_005A, 4'hF, 3, 2'b00,
                                   "THRESHOLD DELAYED BREADY WRITE");


        read_check(REG_THRESHOLD, 32'h0000_005A,
                   "THRESHOLD AFTER DELAYED BREADY");


        write_resp_check(REG_THRESHOLD, 32'h0000_00D2, 4'hF, 2'b00,
                         "THRESHOLD DELAYED BREADY RESTORE");


        read_check(REG_THRESHOLD, 32'h0000_00D2,
                   "THRESHOLD DELAYED BREADY RESTORE READBACK");

        // -----------------------------------------------------
        // C02-14. Delayed RREADY
        //
        // AR request completes normally.
        // RREADY is intentionally held LOW for 3 cycles.
        //
        // Expected:
        //   DUT asserts RVALID while RREADY = 0
        //   RVALID remains asserted
        //   RDATA remains stable
        //   RRESP remains stable
        //   RREADY is asserted after 3 cycles
        //   RDATA = 0x000000D2
        //   RRESP = OKAY
        // -----------------------------------------------------

        delayed_rready_read_check(REG_THRESHOLD, 3, 32'h0000_00D2, 2'b00,
                                  "THRESHOLD DELAYED RREADY READ");

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
