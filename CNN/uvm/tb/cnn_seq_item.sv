typedef enum bit {
    GPIO_OP_WRITE,
    GPIO_OP_READ
} gpio_op_e;

class gpio_seq_item extends uvm_sequence_item;
    rand gpio_op_e op;
    rand bit [3:0]  addr;
    rand bit [31:0] wdata;
    rand bit [7:0]  ext_data;

    logic [31:0] rdata;
    logic [7:0] io_sample;

    constraint c_addr {
        addr inside {4'h0, 4'h4, 4'h8};
    }

    `uvm_object_utils_begin(gpio_seq_item)
        `uvm_field_enum(gpio_op_e, op, UVM_ALL_ON)
        `uvm_field_int(addr, UVM_ALL_ON)
        `uvm_field_int(wdata, UVM_ALL_ON)
        `uvm_field_int(ext_data, UVM_ALL_ON)
        `uvm_field_int(rdata, UVM_ALL_ON)
        `uvm_field_int(io_sample, UVM_ALL_ON)
    `uvm_object_utils_end

    function new(string name = "gpio_seq_item");
        super.new(name);
    endfunction

    function string op_name();
        return (op == GPIO_OP_WRITE) ? "WRITE" : "READ";
    endfunction

    function string addr_name();
        case (addr)
            4'h0: return "CR";
            4'h4: return "IDR";
            4'h8: return "ODR";
            default: return "UNKNOWN";
        endcase
    endfunction

    function string convert2string();
        if (op == GPIO_OP_WRITE) begin
            return $sformatf(
                "%s %s(0x%0h), wdata=0x%08h, io_port=%s",
                op_name(),
                addr_name(),
                addr,
                wdata,
                io_to_string(io_sample)
            );
        end

        return $sformatf(
            "%s %s(0x%0h), rdata=0x%08h, ext_data=0x%02h, io_port=%s",
            op_name(),
            addr_name(),
            addr,
            rdata,
            ext_data,
            io_to_string(io_sample)
        );
    endfunction

    function string io_to_string(logic [7:0] value);
        string s;

        s = "";
        for (int i = 7; i >= 0; i--) begin
            case (value[i])
                1'b0: s = {s, "0"};
                1'b1: s = {s, "1"};
                1'bz: s = {s, "Z"};
                default: s = {s, "X"};
            endcase
        end

        return s;
    endfunction
endclass
