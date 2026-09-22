class gpio_monitor extends uvm_monitor;
    `uvm_component_utils(gpio_monitor)

    virtual gpio_if g_if;
    uvm_analysis_port #(gpio_seq_item) ap;

    function new(string name, uvm_component parent);
        super.new(name, parent);
        ap = new("ap", this);
    endfunction

    function void build_phase(uvm_phase phase);
        super.build_phase(phase);
        if (!uvm_config_db#(virtual gpio_if)::get(this, "", "g_if", g_if))
            `uvm_fatal(
                get_type_name(),
                "virtual interface(g_if)를 config_db에서 찾지 못함.")
    endfunction

    task run_phase(uvm_phase phase);
        gpio_seq_item tr;

        forever begin
            @(g_if.mon_cb);

            if (g_if.mon_cb.sample_valid) begin
                tr = gpio_seq_item::type_id::create("tr");
                tr.op        = g_if.mon_cb.sample_is_read ? GPIO_OP_READ : GPIO_OP_WRITE;
                tr.addr      = g_if.mon_cb.sample_addr;
                tr.wdata     = g_if.mon_cb.sample_wdata;
                tr.rdata     = g_if.mon_cb.sample_rdata;
                tr.ext_data  = g_if.mon_cb.sample_ext_data;
                tr.io_sample = g_if.mon_cb.sample_io;

                `uvm_info(get_type_name(), $sformatf(
                          "GPIO monitor: %s", tr.convert2string()), UVM_HIGH)

                ap.write(tr);
            end
        end
    endtask
endclass
