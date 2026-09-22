class gpio_env extends uvm_env;
    `uvm_component_utils(gpio_env)

    gpio_agent agt;
    gpio_scoreboard scb;
    gpio_coverage cov;

    function new(string name, uvm_component parent);
        super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);
        super.build_phase(phase);
        agt = gpio_agent::type_id::create("agt", this);
        scb = gpio_scoreboard::type_id::create("scb", this);
        cov = gpio_coverage::type_id::create("cov", this);
    endfunction

    function void connect_phase(uvm_phase phase);
        super.connect_phase(phase);
        agt.mon.ap.connect(scb.imp);
        agt.mon.ap.connect(cov.analysis_export);
    endfunction

endclass
