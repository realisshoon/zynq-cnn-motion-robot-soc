class gpio_base_test extends uvm_test;
    `uvm_component_utils(gpio_base_test)

    gpio_env env;

    function new(string name = "gpio_base_test", uvm_component parent = null);
        super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);
        super.build_phase(phase);
        env = gpio_env::type_id::create("env", this);
    endfunction
endclass

class gpio_test extends gpio_base_test;
    `uvm_component_utils(gpio_test)

    gpio_sequence seq;

    function new(string name = "gpio_test", uvm_component parent = null);
        super.new(name, parent);
    endfunction

    task run_phase(uvm_phase phase);
        phase.raise_objection(this);

        seq = gpio_sequence::type_id::create("seq");
        seq.num = 2000;
        seq.start(env.agt.sqr);

        phase.drop_objection(this);
    endtask
endclass

class gpio_write_output_test extends gpio_base_test;
    `uvm_component_utils(gpio_write_output_test)

    gpio_write_output_sequence seq;

    function new(string name = "gpio_write_output_test", uvm_component parent = null);
        super.new(name, parent);
    endfunction

    task run_phase(uvm_phase phase);
        phase.raise_objection(this);

        seq = gpio_write_output_sequence::type_id::create("seq");
        seq.num_random = 2000;
        seq.start(env.agt.sqr);

        phase.drop_objection(this);
    endtask
endclass

class gpio_read_test extends gpio_base_test;
    `uvm_component_utils(gpio_read_test)

    gpio_read_sequence seq;

    function new(string name = "gpio_read_test", uvm_component parent = null);
        super.new(name, parent);
    endfunction

    task run_phase(uvm_phase phase);
        phase.raise_objection(this);

        seq = gpio_read_sequence::type_id::create("seq");
        seq.start(env.agt.sqr);

        phase.drop_objection(this);
    endtask
endclass

class gpio_mixed_random_test extends gpio_base_test;
    `uvm_component_utils(gpio_mixed_random_test)

    gpio_mixed_random_sequence seq;

    function new(string name = "gpio_mixed_random_test", uvm_component parent = null);
        super.new(name, parent);
    endfunction

    task run_phase(uvm_phase phase);
        phase.raise_objection(this);

        seq = gpio_mixed_random_sequence::type_id::create("seq");
        seq.num = 2000;
        seq.start(env.agt.sqr);

        phase.drop_objection(this);
    endtask
endclass
