// Targeted AXI fault injection. A test arms one status-address response;
// the DUT's transaction engine and fault priority logic do the rest.
class cnn_c08_m_axil_responder extends cnn_c05_m_axil_responder;
    `uvm_component_utils(cnn_c08_m_axil_responder)

    localparam int NO_FAULT = 0, RESPONSE_ERROR = 1, STATUS_ERROR = 2;
    int fault_mode;
    bit [31:0] fault_addr;
    bit injected_pending;
    uvm_event injected_handshake;

    function new(string name, uvm_component parent);
        super.new(name, parent);
        injected_handshake = new("injected_handshake");
    endfunction

    function void arm_response_error(bit [31:0] addr);
        fault_addr = addr;
        fault_mode = RESPONSE_ERROR;
        injected_pending = 0;
        injected_handshake.reset();
    endfunction

    function void arm_status_error(bit [31:0] addr);
        fault_addr = addr;
        fault_mode = STATUS_ERROR;
        injected_pending = 0;
        injected_handshake.reset();
    endfunction

    virtual function bit [31:0] read_status_data(bit [31:0] addr, int ch);
        if (fault_mode == STATUS_ERROR && addr == fault_addr)
            return status_word(ch) | 32'h0000_0070; // AXI DMA error bits.
        return super.read_status_data(addr, ch);
    endfunction

    virtual function bit [1:0] read_status_resp(bit [31:0] addr);
        if (fault_mode != NO_FAULT && addr == fault_addr) begin
            injected_pending = 1;
            return fault_mode == RESPONSE_ERROR ? 2'b10 : 2'b00;
        end
        return super.read_status_resp(addr);
    endfunction

    virtual function void read_response_accepted();
        if (injected_pending) begin
            `uvm_info("C08_INJECT", $sformatf("AXI fault response accepted: mode=%0d addr=%08h",
                                             fault_mode, fault_addr), UVM_LOW)
            injected_pending = 0;
            fault_mode = NO_FAULT;
            injected_handshake.trigger();
        end
    endfunction
endclass
