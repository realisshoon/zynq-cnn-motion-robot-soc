class cnn_c02_axil_item extends cnn_seq_item;

    `uvm_object_utils(cnn_c02_axil_item)

    // C02 protocol timing control
    bit          aw_first;
    bit          w_first;
    bit          delay_bready;
    bit          delay_rready;
    int unsigned gap_cycles;
    int unsigned bready_delay_cycles;
    int unsigned rready_delay_cycles;


    function new(string name = "cnn_c02_axil_item");

        super.new(name);

        aw_first            = 1'b0;
        w_first             = 1'b0;
        delay_bready        = 1'b0;

        gap_cycles          = 0;
        bready_delay_cycles = 0;

        delay_rready        = 1'b0;
        rready_delay_cycles = 0;

    endfunction

endclass
