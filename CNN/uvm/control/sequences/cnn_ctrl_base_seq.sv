class cnn_ctrl_base_seq extends cnn_base_sequence;

    `uvm_object_utils(cnn_ctrl_base_seq)

    // ---------------------------------------------------------
    // Register Address Map
    // ---------------------------------------------------------
    localparam bit [11:0] REG_CONTROL = 12'h000;
    localparam bit [11:0] REG_STATUS = 12'h004;
    localparam bit [11:0] REG_THRESHOLD = 12'h008;

    localparam bit [11:0] REG_CONST_00C = 12'h00C;
    localparam bit [11:0] REG_CONST_010 = 12'h010;
    localparam bit [11:0] REG_CONST_014 = 12'h014;

    localparam bit [11:0] REG_JOINT_BASE = 12'h018;
    localparam bit [11:0] REG_JOINT_FLAGS = 12'h05C;

    localparam bit [11:0] REG_RED_THRESHOLD = 12'h060;
    localparam bit [11:0] REG_BLUE_THRESHOLD = 12'h064;
    localparam bit [11:0] REG_RED_WORD = 12'h068;
    localparam bit [11:0] REG_BLUE_WORD = 12'h06C;

    localparam bit [11:0] REG_FRAME_ID = 12'h070;
    localparam bit [11:0] REG_RESULT_SEQ = 12'h074;
    localparam bit [11:0] REG_ERROR_CODE = 12'h078;
    localparam bit [11:0] REG_IRQ_ENABLE = 12'h07C;
    localparam bit [11:0] REG_CYCLE_COUNT = 12'h080;
    localparam bit [11:0] REG_RESULT_FRAME_ID = 12'h084;
    localparam bit [11:0] REG_MIN_COUNT = 12'h088;

    localparam bit [11:0] REG_WGT_BASE = 12'h08C;
    localparam bit [11:0] REG_FM_A_BASE = 12'h090;
    localparam bit [11:0] REG_FM_B_BASE = 12'h094;
    localparam bit [11:0] REG_SG_DESC_BASE = 12'h098;
    localparam bit [11:0] REG_FRAME_BASE = 12'h09C;
    localparam bit [11:0] REG_TIMEOUT = 12'h0A0;

    localparam bit [11:0] REG_CONST_0A4 = 12'h0A4;
    localparam bit [11:0] REG_CONST_0A8 = 12'h0A8;
    localparam bit [11:0] REG_CONST_0AC = 12'h0AC;

    localparam bit [11:0] REG_GREEN_THRESHOLD = 12'h0F4;
    localparam bit [11:0] REG_GREEN_WORD = 12'h0F8;
    localparam bit [11:0] REG_COLOR_ENABLE = 12'h0FC;
    localparam bit [11:0] REG_COLOR_MARGIN = 12'h100;


    function new(string name = "cnn_ctrl_base_seq");
        super.new(name);
    endfunction


    // ---------------------------------------------------------
    // Control Register Commands
    // ---------------------------------------------------------

    task start_cnn();
        axil_write(REG_CONTROL, 32'h0000_0001);
    endtask


    task clear_done();
        axil_write(REG_CONTROL, 32'h0000_0002);
    endtask


    task soft_reset();
        axil_write(REG_CONTROL, 32'h0000_0004);
    endtask


    // ---------------------------------------------------------
    // Simple Register Access Wrapper
    // ---------------------------------------------------------

    task read_reg(bit [11:0] addr, output bit [31:0] data,
                  output bit [1:0] resp);
        axil_read(addr, data, resp);
    endtask


    task write_reg(bit [11:0] addr, bit [31:0] data, bit [3:0] strb = 4'hF);
        axil_write(addr, data, strb);
    endtask

    task write_reg_resp(bit [11:0] addr, bit [31:0] data, bit [3:0] strb,
                        output bit [1:0] resp);

        cnn_seq_item req;

        req = cnn_seq_item::type_id::create("req");

        start_item(req);

        req.kind   = CNN_AXIL_WRITE;
        req.addr   = addr;
        req.data32 = data;
        req.strb   = strb;

        finish_item(req);

        resp = req.resp;

    endtask

    task write_resp_check(bit [11:0] addr, bit [31:0] data, bit [3:0] strb,
                          bit [1:0] expected_resp, string reg_name);

        bit [1:0] actual_resp;

        write_reg_resp(addr, data, strb, actual_resp);

        if (actual_resp != expected_resp) begin

            `uvm_error(
                "CTRL_WRITE_RESP",
                $sformatf(
                    "%s write response mismatch: addr=0x%03h expected_resp=%02b actual_resp=%02b",
                    reg_name, addr, expected_resp, actual_resp))

        end else begin

            `uvm_info(
                "CTRL_WRITE_PASS", $sformatf(
                "%s PASS: addr=0x%03h resp=%02b", reg_name, addr, actual_resp),
                UVM_LOW)

        end

    endtask

    // ---------------------------------------------------------
    // Register Check Helper
    // ---------------------------------------------------------

    task read_check(bit [11:0] addr, bit [31:0] expected, string reg_name);

        bit [31:0] actual;
        bit [ 1:0] resp;

        read_reg(addr, actual, resp);

        if (resp != 2'b00) begin
            `uvm_error("CTRL_READ_RESP",
                       $sformatf(
                           "%s read response error: addr=0x%03h resp=%02b",
                           reg_name, addr, resp))
        end else if (actual != expected) begin
            `uvm_error("CTRL_READ_MISMATCH",
                       $sformatf("%s mismatch: expected=0x%08h actual=0x%08h",
                                 reg_name, expected, actual))
        end else begin
            `uvm_info("CTRL_READ_PASS", $sformatf(
                      "%s PASS: expected=0x%08h actual=0x%08h",
                      reg_name,
                      expected,
                      actual
                      ), UVM_LOW)
        end

    endtask


    task body();
        // Base sequence itself does not run a scenario.
        // C01 ~ C08 sequences override this body().
    endtask

endclass
