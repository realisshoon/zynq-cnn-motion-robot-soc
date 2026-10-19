// Diagnostic only: establish the first real FSM blocking point with the
// unmodified common master AXI-Lite responder. This is not the full C05 check.
class cnn_c05_fsm_seq extends cnn_ctrl_base_seq;
    `uvm_object_utils(cnn_c05_fsm_seq)

    localparam bit [11:0] REG_DEBUG_STATE = 12'h0b4;
    localparam int S_IDLE = 0;
    localparam int S_LOAD_WAIT = 6;

    function new(string name = "cnn_c05_fsm_seq");
        super.new(name);
    endfunction

    task body();
        bit [31:0] debug, status, error_code;
        bit [1:0] resp;
        bit [20:0] visited;
        int last_state, last_stage, state_now, stage_now;
        int load_wait_samples;

        visited = '0;
        last_state = -1;
        last_stage = -1;
        load_wait_samples = 0;

        read_reg(REG_DEBUG_STATE, debug, resp);
        if (resp != 2'b00 || debug[31:24] != 8'hd1 ||
            debug[4:0] != S_IDLE || debug[16])
            `uvm_error("C05_SMOKE_INITIAL",
                       $sformatf("Expected idle debug signature; data=%08h resp=%02b", debug, resp))

        write_resp_check(REG_CONTROL, 32'h1, 4'hf, 2'b00, "C05_START");

        // The frontdoor debug register is sufficient to locate a stall. The
        // eventual C05 monitor will observe every cycle and check progression.
        for (int sample_index = 0; sample_index < 500; sample_index++) begin
            read_reg(REG_DEBUG_STATE, debug, resp);
            if (resp != 2'b00 || debug[31:24] != 8'hd1) begin
                `uvm_error("C05_SMOKE_DEBUG",
                           $sformatf("Bad debug read: data=%08h resp=%02b", debug, resp))
                break;
            end

            state_now = int'(debug[4:0]);
            stage_now = int'(debug[8:5]);
            if (state_now <= 20) visited[state_now] = 1'b1;
            if (state_now != last_state || stage_now != last_stage) begin
                `uvm_info("C05_SMOKE_TRACE",
                          $sformatf("sample=%0d state=%0d stage=%0d step=%0d txn=%0d busy=%0b error=%0b",
                                    sample_index, state_now, stage_now,
                                    debug[12:9], debug[15:13], debug[16], debug[17]), UVM_LOW)
                last_state = state_now;
                last_stage = stage_now;
            end

            if (state_now == S_LOAD_WAIT && stage_now == 0) load_wait_samples++;
            else load_wait_samples = 0;
            if (load_wait_samples == 64) break;
        end

        read_reg(REG_STATUS, status, resp);
        if (resp != 2'b00 || !status[1] || status[2] || status[0])
            `uvm_error("C05_SMOKE_STATUS",
                       $sformatf("Expected busy without done/error; status=%08h resp=%02b", status, resp))

        read_reg(REG_ERROR_CODE, error_code, resp);
        if (resp != 2'b00 || error_code != 0)
            `uvm_error("C05_SMOKE_ERROR",
                       $sformatf("Unexpected fault; error_code=%08h resp=%02b", error_code, resp))

        // Frontdoor reads take several cycles, so transient descriptor and DMA
        // states can be skipped. Only the stable stopping point is mandatory.
        if (!visited[S_LOAD_WAIT] || load_wait_samples < 64)
            `uvm_error("C05_SMOKE_PROGRESS",
                       $sformatf("Expected stable stage-0 LOAD_WAIT stall; visited=%06h last_state=%0d samples=%0d",
                                 visited, last_state, load_wait_samples))
        else
            `uvm_info("C05_SMOKE_RESULT",
                      "Common responder returned zero weight DMA status; FSM is held in stage 0 LOAD_WAIT", UVM_LOW)
    endtask
endclass
