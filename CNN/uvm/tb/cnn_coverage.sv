class gpio_coverage extends uvm_subscriber #(gpio_seq_item);
    `uvm_component_utils(gpio_coverage)

    gpio_seq_item tr;

    covergroup gpio_cg;
        option.per_instance = 1;

        op_cp: coverpoint tr.op {
            bins write = {GPIO_OP_WRITE};
            bins read  = {GPIO_OP_READ};
        }

        addr_cp: coverpoint tr.addr {
            bins cr  = {4'h0};
            bins idr = {4'h4};
            bins odr = {4'h8};
        }

        wdata_low_cp: coverpoint tr.wdata[7:0] iff (tr.op == GPIO_OP_WRITE) {
            bins zero      = {8'h00};
            bins very_low  = {[8'h01 : 8'h0F]};
            bins low       = {[8'h10 : 8'h3F]};
            bins mid_low   = {[8'h40 : 8'h7F]};
            bins mid_high  = {[8'h80 : 8'hBF]};
            bins high      = {[8'hC0 : 8'hEF]};
            bins very_high = {[8'hF0 : 8'hFE]};
            bins all_one   = {8'hFF};
        }

        ext_data_cp: coverpoint tr.ext_data iff (tr.op == GPIO_OP_READ && tr.addr == 4'h4) {
            bins zero      = {8'h00};
            bins low       = {[8'h01 : 8'h3F]};
            bins mid       = {[8'h40 : 8'hBF]};
            bins high      = {[8'hC0 : 8'hFE]};
            bins all_one   = {8'hFF};
        }

        op_x_addr: cross op_cp, addr_cp;
        write_addr_x_data: cross addr_cp, wdata_low_cp iff (tr.op == GPIO_OP_WRITE);
    endgroup

    function new(string name, uvm_component parent);
        super.new(name, parent);
        gpio_cg = new();
    endfunction

    function void write(gpio_seq_item t);
        tr = t;
        gpio_cg.sample();
    endfunction

    function void report_phase(uvm_phase phase);
        super.report_phase(phase);

        `uvm_info("COV", "==============================================================", UVM_LOW)
        `uvm_info("COV", "                  GPIO COVERAGE REPORT                        ", UVM_LOW)
        `uvm_info("COV", "==============================================================", UVM_LOW)
        `uvm_info("COV", $sformatf("  Total Coverage       | %6.2f %%", gpio_cg.get_inst_coverage()), UVM_LOW)
        `uvm_info("COV", $sformatf("  Operation Coverage   | %6.2f %%", gpio_cg.op_cp.get_inst_coverage()), UVM_LOW)
        `uvm_info("COV", $sformatf("  Address Coverage     | %6.2f %%", gpio_cg.addr_cp.get_inst_coverage()), UVM_LOW)
        `uvm_info("COV", $sformatf("  WDATA[7:0] Coverage  | %6.2f %%", gpio_cg.wdata_low_cp.get_inst_coverage()), UVM_LOW)
        `uvm_info("COV", $sformatf("  EXT_DATA Coverage    | %6.2f %%", gpio_cg.ext_data_cp.get_inst_coverage()), UVM_LOW)
        `uvm_info("COV", $sformatf("  OP x ADDR Coverage   | %6.2f %%", gpio_cg.op_x_addr.get_inst_coverage()), UVM_LOW)
        `uvm_info("COV", $sformatf("  WR ADDR x DATA Cov   | %6.2f %%", gpio_cg.write_addr_x_data.get_inst_coverage()), UVM_LOW)
        `uvm_info("COV", "==============================================================", UVM_LOW)
    endfunction
endclass
