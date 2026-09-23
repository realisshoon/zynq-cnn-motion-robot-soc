class cnn_base_sequence extends uvm_sequence #(cnn_seq_item);
    `uvm_object_utils(cnn_base_sequence)

    function new(string name = "cnn_base_sequence");
        super.new(name);
    endfunction

    task axil_write(bit [11:0] addr, bit [31:0] data, bit [3:0] strb = 4'hf);
        cnn_seq_item req;
        req = cnn_seq_item::type_id::create("req");
        start_item(req);
        req.kind   = CNN_AXIL_WRITE;
        req.addr   = addr;
        req.data32 = data;
        req.strb   = strb;
        finish_item(req);
    endtask

    task axil_read(bit [11:0] addr, output bit [31:0] data, output bit [1:0] resp);
        cnn_seq_item req;
        req = cnn_seq_item::type_id::create("req");
        start_item(req);
        req.kind = CNN_AXIL_READ;
        req.addr = addr;
        finish_item(req);
        data = req.rdata;
        resp = req.resp;
    endtask

    task send_stream(
        cnn_item_kind_e kind,
        bit [63:0] data,
        bit [7:0] keep = 8'hff,
        bit last = 1'b0
    );
        cnn_seq_item req;
        if (!(kind inside {CNN_IMAGE_BEAT, CNN_WEIGHT_BEAT, CNN_FEATURE_IN_BEAT}))
            `uvm_fatal(get_type_name(), "send_stream() called with non-input-stream kind")
        req = cnn_seq_item::type_id::create("req");
        start_item(req);
        req.kind   = kind;
        req.data64 = data;
        req.keep   = keep;
        req.last   = last;
        finish_item(req);
    endtask

    task set_feature_ready(bit ready_value, int unsigned cycles = 1);
        cnn_seq_item req;
        req = cnn_seq_item::type_id::create("req");
        start_item(req);
        req.kind        = CNN_SET_FEATURE_READY;
        req.ready_value = ready_value;
        req.hold_cycles = cycles;
        finish_item(req);
    endtask

    task body();
        // Intentionally empty. Scenario branches derive from this class.
    endtask
endclass
