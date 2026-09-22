class gpio_driver extends uvm_driver #(gpio_seq_item);
    `uvm_component_utils(gpio_driver)

    localparam bit [3:0] GPIO_CR_ADDR  = 4'h0;
    localparam bit [3:0] GPIO_IDR_ADDR = 4'h4;
    localparam bit [3:0] GPIO_ODR_ADDR = 4'h8;

    virtual gpio_if g_if;

    bit [7:0] cr_mirror;
    bit [7:0] odr_mirror;
    int unsigned axi_timeout = 1000;

    function new(string name, uvm_component parent);
        super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);
        super.build_phase(phase);
        if (!uvm_config_db#(virtual gpio_if)::get(this, "", "g_if", g_if))
            `uvm_fatal(
                get_type_name(),
                "virtual interface(g_if)를 config_db에서 찾지 못함.")
    endfunction

    task run_phase(uvm_phase phase);
        init_bus();

        wait (g_if.reset == 1'b0);
        repeat (2) @(g_if.drv_cb);
        `uvm_info(get_type_name(), "Reset released. GPIO driver starts transactions.", UVM_LOW)

        forever begin
            seq_item_port.get_next_item(req);

            if (req.op == GPIO_OP_WRITE) begin
                drive_external_input(8'h00, 8'h00);
                axi_write(req.addr, req.wdata);
                update_write_mirror(req.addr, req.wdata);
            end else begin
                if (req.addr == GPIO_IDR_ADDR) begin
                    drive_external_input(~cr_mirror, req.ext_data);
                    repeat (2) @(g_if.drv_cb);
                end else begin
                    drive_external_input(8'h00, 8'h00);
                end

                axi_read(req.addr, req.rdata);
            end

            repeat (2) @(g_if.drv_cb);
            send_sample(req);

            `uvm_info(get_type_name(), $sformatf(
                      "GPIO drive: %s", req.convert2string()), UVM_HIGH)

            seq_item_port.item_done();
        end
    endtask

    task init_bus();
        cr_mirror  = 8'h00;
        odr_mirror = 8'h00;

        g_if.drv_cb.awaddr  <= '0;
        g_if.drv_cb.awvalid <= 1'b0;
        g_if.drv_cb.wdata   <= '0;
        g_if.drv_cb.wstrb   <= 4'hf;
        g_if.drv_cb.wvalid  <= 1'b0;
        g_if.drv_cb.bready  <= 1'b0;

        g_if.drv_cb.araddr  <= '0;
        g_if.drv_cb.arvalid <= 1'b0;
        g_if.drv_cb.rready  <= 1'b0;

        drive_external_input(8'h00, 8'h00);
        g_if.sample_valid   = 1'b0;
        g_if.sample_is_read = 1'b0;
        g_if.sample_addr    = 4'h0;
        g_if.sample_wdata   = 32'h0000_0000;
        g_if.sample_rdata   = 32'h0000_0000;
        g_if.sample_ext_data = 8'h00;
        g_if.sample_io      = 8'h00;
    endtask

    task drive_external_input(bit [7:0] drive_en, bit [7:0] drive_data);
        g_if.gpio_drive_en   = drive_en;
        g_if.gpio_drive_data = drive_data;
    endtask

    function void update_write_mirror(bit [3:0] addr, bit [31:0] data);
        case (addr)
            GPIO_CR_ADDR:  cr_mirror  = data[7:0];
            GPIO_ODR_ADDR: odr_mirror = data[7:0];
            default: begin
            end
        endcase
    endfunction

    task send_sample(gpio_seq_item item);
        g_if.sample_is_read  = (item.op == GPIO_OP_READ);
        g_if.sample_addr     = item.addr;
        g_if.sample_wdata    = item.wdata;
        g_if.sample_rdata    = item.rdata;
        g_if.sample_ext_data = item.ext_data;
        g_if.sample_io       = g_if.io_port_value;
        g_if.sample_valid    = 1'b1;
        @(g_if.drv_cb);
        g_if.sample_valid    = 1'b0;
    endtask

    task axi_write(bit [3:0] addr, bit [31:0] data);
        int unsigned wait_count;

        `uvm_info(get_type_name(), $sformatf(
                  "AXI WRITE start: addr=0x%0h data=0x%08h", addr, data), UVM_LOW)

        @(g_if.drv_cb);
        g_if.drv_cb.awaddr  <= addr;
        g_if.drv_cb.awvalid <= 1'b1;
        g_if.drv_cb.wdata   <= data;
        g_if.drv_cb.wstrb   <= 4'hf;
        g_if.drv_cb.wvalid  <= 1'b1;
        g_if.drv_cb.bready  <= 1'b0;

        wait_count = 0;
        do begin
            @(g_if.drv_cb);
            wait_count++;
            if (wait_count > axi_timeout) begin
                `uvm_fatal(get_type_name(), $sformatf(
                           "AXI WRITE address/data handshake timeout: addr=0x%0h awready=%0b wready=%0b",
                           addr, g_if.drv_cb.awready, g_if.drv_cb.wready))
            end
        end while (!(g_if.drv_cb.awready && g_if.drv_cb.wready));

        @(g_if.drv_cb);
        g_if.drv_cb.awvalid <= 1'b0;
        g_if.drv_cb.wvalid  <= 1'b0;
        g_if.drv_cb.bready  <= 1'b1;

        wait_count = 0;
        do begin
            @(g_if.drv_cb);
            wait_count++;
            if (wait_count > axi_timeout) begin
                `uvm_fatal(get_type_name(), $sformatf(
                           "AXI WRITE response timeout: addr=0x%0h bvalid=%0b bresp=0x%0h",
                           addr, g_if.drv_cb.bvalid, g_if.drv_cb.bresp))
            end
        end while (!g_if.drv_cb.bvalid);

        @(g_if.drv_cb);
        g_if.drv_cb.bready <= 1'b0;

        `uvm_info(get_type_name(), $sformatf(
                  "AXI WRITE done : addr=0x%0h data=0x%08h", addr, data), UVM_LOW)
    endtask

    task axi_read(bit [3:0] addr, output logic [31:0] data);
        int unsigned wait_count;

        `uvm_info(get_type_name(), $sformatf(
                  "AXI READ start : addr=0x%0h", addr), UVM_LOW)

        @(g_if.drv_cb);
        g_if.drv_cb.araddr  <= addr;
        g_if.drv_cb.arvalid <= 1'b1;
        g_if.drv_cb.rready  <= 1'b0;

        wait_count = 0;
        do begin
            @(g_if.drv_cb);
            wait_count++;
            if (wait_count > axi_timeout) begin
                `uvm_fatal(get_type_name(), $sformatf(
                           "AXI READ address handshake timeout: addr=0x%0h arready=%0b",
                           addr, g_if.drv_cb.arready))
            end
        end while (!g_if.drv_cb.arready);

        @(g_if.drv_cb);
        g_if.drv_cb.arvalid <= 1'b0;
        g_if.drv_cb.rready  <= 1'b1;

        wait_count = 0;
        do begin
            @(g_if.drv_cb);
            wait_count++;
            if (wait_count > axi_timeout) begin
                `uvm_fatal(get_type_name(), $sformatf(
                           "AXI READ data timeout: addr=0x%0h rvalid=%0b",
                           addr, g_if.drv_cb.rvalid))
            end
        end while (!g_if.drv_cb.rvalid);

        data = g_if.drv_cb.rdata;
        @(g_if.drv_cb);
        g_if.drv_cb.rready <= 1'b0;

        `uvm_info(get_type_name(), $sformatf(
                  "AXI READ done  : addr=0x%0h data=0x%08h", addr, data), UVM_LOW)
    endtask
endclass
