class cnn_dma_s08_full_flow_seq extends cnn_dma_stream_base_seq;
    `uvm_object_utils(cnn_dma_s08_full_flow_seq)
    virtual cnn_if vif;
    bit [31:0] final_status;
    bit done_seen;
    function new(string name="cnn_dma_s08_full_flow_seq"); super.new(name); endfunction
    task body();
        bit [1:0] resp;
        bit [31:0] debug_data;
        axil_write(12'h000,1);
        // Bounded full-flow polling; DUT's existing progress watchdog stays enabled.
        for(int poll=0;poll<30000;poll++) begin
            repeat(1000) @(posedge vif.clk);
            axil_read(12'h004,final_status,resp);
            if(poll%1000==0)
                `uvm_info("S08_PROGRESS",$sformatf("poll=%0d stage=%0d STATUS=0x%08h",poll,observed_stage,final_status),UVM_LOW)
            if(resp!=0) `uvm_fatal("S08_STATUS", "STATUS response not OKAY")
            if(final_status[2]) begin
                axil_read(12'h078,debug_data,resp);
                `uvm_info("S08_FAULT_EVIDENCE",$sformatf("ERROR_CODE=0x%08h stage=%0d",debug_data,observed_stage),UVM_NONE)
                axil_read(12'h0b0,debug_data,resp);
                `uvm_info("S08_FAULT_EVIDENCE",$sformatf("FAULT_SOURCES=0x%08h",debug_data),UVM_NONE)
                axil_read(12'h0b4,debug_data,resp);
                `uvm_info("S08_FAULT_EVIDENCE",$sformatf("DEBUG_STATE=0x%08h",debug_data),UVM_NONE)
                `uvm_fatal("S08_STATUS",$sformatf("ERROR_PENDING STATUS=0x%08h",final_status))
            end
            if(final_status[0]) begin done_seen=1; return; end
        end
        `uvm_fatal("S08_TIMEOUT",$sformatf("Full-flow did not complete in 30M cycles, stage=%0d",observed_stage))
    endtask
endclass
