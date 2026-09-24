class cnn_golden_env extends cnn_env;
    `uvm_component_utils(cnn_golden_env)

    function new(string name, uvm_component parent);
        super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);

        cnn_scoreboard::type_id::set_type_override(cnn_golden_scoreboard::get_type());

        super.build_phase(phase);

    endfunction

endclass
