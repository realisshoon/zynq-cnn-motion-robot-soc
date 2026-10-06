class cnn_ctrl_base_test extends cnn_base_test;

    `uvm_component_utils(cnn_ctrl_base_test)

    virtual cnn_if vif;


    function new(
        string name = "cnn_ctrl_base_test",
        uvm_component parent = null
    );
        super.new(name, parent);
    endfunction


    function void build_phase(uvm_phase phase);

        super.build_phase(phase);

        if (!uvm_config_db#(virtual cnn_if)::get(
                this,
                "",
                "vif",
                vif
            )) begin

            `uvm_fatal(
                "CTRL_BASE_TEST",
                "Failed to get virtual interface"
            )

        end

    endfunction


    task wait_reset_release();

        wait (vif.rst_n === 1'b1);
        @(posedge vif.clk);

    endtask

endclass