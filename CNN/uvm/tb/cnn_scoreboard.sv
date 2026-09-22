class gpio_scoreboard extends uvm_scoreboard;
    `uvm_component_utils(gpio_scoreboard)

    localparam bit [3:0] GPIO_CR_ADDR  = 4'h0;
    localparam bit [3:0] GPIO_IDR_ADDR = 4'h4;
    localparam bit [3:0] GPIO_ODR_ADDR = 4'h8;

    uvm_analysis_imp #(gpio_seq_item, gpio_scoreboard) imp;

    logic [31:0] cr_mirror;
    logic [31:0] odr_mirror;

    int total_cnt = 0;
    int pass_cnt = 0;
    int fail_cnt = 0;

    function new(string name, uvm_component parent);
        super.new(name, parent);
        imp = new("imp", this);
        cr_mirror  = 32'h0000_0000;
        odr_mirror = 32'h0000_0000;
    endfunction

    function void write(gpio_seq_item tr);
        bit pass;

        total_cnt++;

        if (tr.op == GPIO_OP_WRITE) begin
            update_write_mirror(tr);
            pass = check_io_after_write(tr);
        end else begin
            pass = check_read_data(tr);
        end

        if (pass) begin
            pass_cnt++;
            `uvm_info(get_type_name(), $sformatf(
                      "%s PASS: %s", tr.op_name(), tr.convert2string()), UVM_LOW)
        end else begin
            fail_cnt++;
        end
    endfunction

    function void update_write_mirror(gpio_seq_item tr);
        case (tr.addr)
            GPIO_CR_ADDR:  cr_mirror  = tr.wdata;
            GPIO_ODR_ADDR: odr_mirror = tr.wdata;
            GPIO_IDR_ADDR: begin
                // IDR write must not affect GPIO output state.
            end
            default: begin
                `uvm_error(get_type_name(), $sformatf(
                           "Unexpected write address: 0x%0h", tr.addr))
            end
        endcase
    endfunction

    function bit check_io_after_write(gpio_seq_item tr);
        bit pass;
        logic exp_bit;

        pass = 1'b1;

        for (int i = 0; i < 8; i++) begin
            exp_bit = cr_mirror[i] ? odr_mirror[i] : 1'bz;

            if (tr.io_sample[i] !== exp_bit) begin
                pass = 1'b0;
                `uvm_error(get_type_name(), $sformatf(
                           {"GPIO WRITE output check fail: ",
                            "%s write addr=%s(0x%0h), wdata=0x%08h, ",
                            "bit[%0d] CR=%0b ODR=%0b expected io=%s actual io=%s"},
                           tr.op_name(),
                           tr.addr_name(),
                           tr.addr,
                           tr.wdata,
                           i,
                           cr_mirror[i],
                           odr_mirror[i],
                           bit_to_string(exp_bit),
                           bit_to_string(tr.io_sample[i])))
            end
        end

        return pass;
    endfunction

    function bit check_read_data(gpio_seq_item tr);
        bit pass;
        logic [31:0] exp_rdata;

        pass = 1'b1;
        exp_rdata = 32'h0000_0000;

        case (tr.addr)
            GPIO_CR_ADDR: begin
                exp_rdata = cr_mirror;
            end
            GPIO_ODR_ADDR: begin
                exp_rdata = odr_mirror;
            end
            GPIO_IDR_ADDR: begin
                for (int i = 0; i < 8; i++) begin
                    exp_rdata[i] = cr_mirror[i] ? 1'bz : tr.ext_data[i];
                end
            end
            default: begin
                pass = 1'b0;
                `uvm_error(get_type_name(), $sformatf(
                           "Unexpected read address: 0x%0h", tr.addr))
            end
        endcase

        for (int i = 0; i < 32; i++) begin
            if (tr.rdata[i] !== exp_rdata[i]) begin
                pass = 1'b0;
                `uvm_error(get_type_name(), $sformatf(
                           {"GPIO READ data check fail: ",
                            "read addr=%s(0x%0h), bit[%0d] ",
                            "expected=%s actual=%s full_expected=0x%08h full_actual=0x%08h"},
                           tr.addr_name(),
                           tr.addr,
                           i,
                           bit_to_string(exp_rdata[i]),
                           bit_to_string(tr.rdata[i]),
                           exp_rdata,
                           tr.rdata))
            end
        end

        return pass;
    endfunction

    function string bit_to_string(logic value);
        case (value)
            1'b0: return "0";
            1'b1: return "1";
            1'bz: return "Z";
            default: return "X";
        endcase
    endfunction

    function void report_phase(uvm_phase phase);
        super.report_phase(phase);
        `uvm_info("SCB", "====================================", UVM_LOW)
        `uvm_info("SCB", "======= GPIO Scoreboard Report =====", UVM_LOW)
        `uvm_info("SCB", $sformatf("    total count : %0d", total_cnt), UVM_LOW)
        `uvm_info("SCB", $sformatf("     fail count : %0d", fail_cnt), UVM_LOW)
        `uvm_info("SCB", $sformatf("     pass count : %0d", pass_cnt), UVM_LOW)
        `uvm_info("SCB", "====================================", UVM_LOW)
    endfunction
endclass
