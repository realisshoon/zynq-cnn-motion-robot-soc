class cnn_c01_reset_seq extends cnn_ctrl_base_seq;

    `uvm_object_utils(cnn_c01_reset_seq)

    function new(string name = "cnn_c01_reset_seq");
        super.new(name);
    endfunction

    task body();

        `uvm_info(
            "C01",
            "C01 Reset Default Register test start",
            UVM_LOW
        )

        read_check(
            REG_STATUS,
            32'h0000_0000,
            "STATUS"
        );

        read_check(
            REG_THRESHOLD,
            32'h0000_00D2,
            "THRESHOLD"
        );

    endtask

endclass