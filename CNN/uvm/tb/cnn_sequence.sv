class gpio_base_sequence extends uvm_sequence #(gpio_seq_item);
    `uvm_object_utils(gpio_base_sequence)

    localparam bit [3:0] GPIO_CR_ADDR  = 4'h0;
    localparam bit [3:0] GPIO_IDR_ADDR = 4'h4;
    localparam bit [3:0] GPIO_ODR_ADDR = 4'h8;

    function new(string name = "gpio_base_sequence");
        super.new(name);
    endfunction

    task send_write(bit [3:0] addr, bit [31:0] data);
        gpio_seq_item req;

        req = gpio_seq_item::type_id::create("req");
        start_item(req);
        req.op       = GPIO_OP_WRITE;
        req.addr     = addr;
        req.wdata    = data;
        req.ext_data = 8'h00;
        finish_item(req);
    endtask

    task send_read(bit [3:0] addr, bit [7:0] ext_data = 8'h00);
        gpio_seq_item req;

        req = gpio_seq_item::type_id::create("req");
        start_item(req);
        req.op       = GPIO_OP_READ;
        req.addr     = addr;
        req.wdata    = 32'h0000_0000;
        req.ext_data = ext_data;
        finish_item(req);
    endtask
endclass

class gpio_write_output_sequence extends gpio_base_sequence;
    `uvm_object_utils(gpio_write_output_sequence)

    int num_random = 2000;
    bit [7:0] cr_patterns[6] = '{8'hff, 8'h00, 8'hf0, 8'h0f, 8'haa, 8'h55};
    bit [7:0] data_patterns[8] = '{8'h00, 8'hff, 8'h01, 8'h0f, 8'h3f, 8'h7f, 8'hbf, 8'hfe};

    function new(string name = "gpio_write_output_sequence");
        super.new(name);
    endfunction

    task body();
        foreach (cr_patterns[i]) begin
            send_write(GPIO_CR_ADDR, {24'h0, cr_patterns[i]});
            send_write(GPIO_ODR_ADDR, $urandom());
        end

        foreach (data_patterns[i]) begin
            send_write(GPIO_CR_ADDR, 32'h0000_00ff);
            send_write(GPIO_ODR_ADDR, {24'h0, data_patterns[i]});
        end

        repeat (num_random) begin
            send_write(GPIO_CR_ADDR, $urandom());
            send_write(GPIO_ODR_ADDR, $urandom());
        end
    endtask
endclass

class gpio_read_sequence extends gpio_base_sequence;
    `uvm_object_utils(gpio_read_sequence)

    function new(string name = "gpio_read_sequence");
        super.new(name);
    endfunction

    task body();
        send_write(GPIO_CR_ADDR,  32'h0000_00f0);
        send_read (GPIO_CR_ADDR);

        send_write(GPIO_ODR_ADDR, 32'h0000_00a5);
        send_read (GPIO_ODR_ADDR);

        send_write(GPIO_CR_ADDR,  32'h0000_0000);
        send_read (GPIO_IDR_ADDR, 8'h3c);
        send_read (GPIO_IDR_ADDR, 8'h00);
        send_read (GPIO_IDR_ADDR, 8'hff);

        send_write(GPIO_CR_ADDR,  32'h0000_00f0);
        send_read (GPIO_IDR_ADDR, 8'h5a);
    endtask
endclass

class gpio_mixed_random_sequence extends gpio_base_sequence;
    `uvm_object_utils(gpio_mixed_random_sequence)

    int num = 2000;
    bit [3:0] addr_patterns[3] = '{GPIO_CR_ADDR, GPIO_IDR_ADDR, GPIO_ODR_ADDR};
    bit [7:0] data_patterns[8] = '{8'h00, 8'hff, 8'h01, 8'h0f, 8'h3f, 8'h7f, 8'hbf, 8'hfe};

    function new(string name = "gpio_mixed_random_sequence");
        super.new(name);
    endfunction

    task body();
        gpio_seq_item req;

        foreach (addr_patterns[a]) begin
            foreach (data_patterns[d]) begin
                send_write(addr_patterns[a], {24'h0, data_patterns[d]});
            end
        end

        foreach (data_patterns[d]) begin
            send_read(GPIO_IDR_ADDR, data_patterns[d]);
        end

        repeat (num) begin
            req = gpio_seq_item::type_id::create("req");
            start_item(req);
            if (!req.randomize()) begin
                `uvm_error(get_type_name(), "Randomize failed")
            end
            finish_item(req);
        end
    endtask
endclass

class gpio_sequence extends gpio_base_sequence;
    `uvm_object_utils(gpio_sequence)

    int num = 2000;

    function new(string name = "gpio_sequence");
        super.new(name);
    endfunction

    task body();
        gpio_write_output_sequence write_seq;
        gpio_read_sequence         read_seq;
        gpio_mixed_random_sequence mixed_seq;

        write_seq = gpio_write_output_sequence::type_id::create("write_seq");
        write_seq.num_random = num;
        write_seq.start(m_sequencer);

        read_seq = gpio_read_sequence::type_id::create("read_seq");
        read_seq.start(m_sequencer);

        mixed_seq = gpio_mixed_random_sequence::type_id::create("mixed_seq");
        mixed_seq.num = num;
        mixed_seq.start(m_sequencer);
    endtask
endclass
