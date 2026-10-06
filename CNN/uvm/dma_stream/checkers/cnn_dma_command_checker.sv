// DMA command state is separate from the existing accepted-count scoreboard.
class cnn_dma_command_checker extends uvm_object;
    `uvm_object_utils(cnn_dma_command_checker)
    cnn_dma_stream_cfg cfg;
    cnn_dma_stream_coverage cov;
    cnn_dma_reference_model ref_model;
    int weight_state, weight_packets, weight_packet_beats;
    bit weight_run;
    bit [31:0] weight_addr, weight_length;
    time weight_commit_time, conv0_commit_time;
    bit [31:0] conv0_source,conv0_length;
    bit [31:0] actual_src[16], actual_dst[16];
    bit src_seen[16],dst_seen[16],src_run[16],dst_run[16];
    bit src_addr_seen[16],dst_addr_seen[16];
    bit [31:0] src_length[16],dst_length[16];
    time dst_arm_time[16],source_start_time[16];
    time dma_done_time,final_output_time;
    int output_packet_beats,output_packets,active_dst_stage;
    int feature_in_packet_beats,feature_in_packets,active_src_stage;
    int mapping_mismatches;
    int image_packet_beats,image_packets;
    bit image_started;
    bit [2:0] weight_pending_irq;
    int errors;
    function new(string name="cnn_dma_command_checker");
        super.new(name);
        weight_commit_time=0; conv0_commit_time=0; dma_done_time=0; final_output_time=0;
        foreach(dst_arm_time[i]) begin dst_arm_time[i]=0; source_start_time[i]=0; end
        weight_state=0; weight_packets=0; weight_packet_beats=0; errors=0;
        output_packet_beats=0; output_packets=0; active_dst_stage=0;
        feature_in_packet_beats=0; feature_in_packets=0; active_src_stage=0;
        mapping_mismatches=0; image_packet_beats=0; image_packets=0;
        ref_model=cnn_dma_reference_model::type_id::create("ref_model");
    endfunction
    function void fail(string msg);
        errors++; `uvm_error("DMA_COMMAND",msg)
    endfunction
    function void command(cnn_dma_event_item tr);
        int st;
        st=tr.stage;
        if (st>15) begin fail("Invalid observed stage"); return; end
        if (tr.kind==CNN_DMA_AXIL_READ) begin
            if (tr.addr==32'h40410004) weight_pending_irq=tr.data[14:12];
            if (tr.addr==32'h40420034 && tr.data[1] && dst_arm_time[0]!=0 &&
                st==0 && dma_done_time==0) begin
                dma_done_time=tr.observed_time;
                `uvm_info("DMA_COMPLETION_OBSERVED",$sformatf("stage=%0d SR=0x%08h time=%0t",st,tr.data,tr.observed_time),UVM_LOW)
            end
            return;
        end
        case(tr.addr)
            32'h40410004: begin
                weight_pending_irq=weight_pending_irq & ~tr.data[14:12];
            end
            32'h40410000: begin
                if (weight_pending_irq!=0) fail("Weight RUN before required SR W1C clear");
                weight_run=tr.data[0];
                if (!weight_run) fail("Weight CR must RUN before stream");
                weight_state=1;
                if (weight_packets==0 && tr.data==1 && tr.resp==0) cov.weight_dma_cg.sample(0);
            end
            32'h40410018: begin
                if (weight_state!=1) fail("Weight source programmed before RUN");
                if (weight_packets>=29 || tr.data!=ref_model.get_expected_weight_src(weight_packets))
                    fail($sformatf("Weight op%0d source expected=0x%08h actual=0x%08h",
                        weight_packets,ref_model.get_expected_weight_src(weight_packets),tr.data));
                weight_addr=tr.data; weight_state=2;
                if (weight_packets==0) begin
                    conv0_source=tr.data;
                    if (tr.data!=32'h10000000) fail("Conv0 source mismatch");
                    else cov.weight_dma_cg.sample(1);
                end
            end
            32'h40410028: begin
                if (weight_state!=2 || !weight_run || tr.data==0 || tr.resp!=0)
                    fail("Weight LENGTH committed without RUN/source/OKAY");
                if (weight_packet_beats!=0) fail("Weight command before preceding packet completion");
                if (weight_packets>=29 || tr.data!=ref_model.get_expected_weight_bytes(weight_packets))
                    fail($sformatf("Weight op%0d length expected=%0d actual=%0d",
                        weight_packets,ref_model.get_expected_weight_bytes(weight_packets),tr.data));
                weight_length=tr.data; weight_state=3; weight_commit_time=tr.observed_time;
                if (weight_packets==0) begin
                    conv0_length=tr.data; conv0_commit_time=tr.observed_time;
                    if (tr.data!=960) fail("Conv0 LENGTH mismatch");
                    else begin cov.weight_dma_cg.sample(2); cov.weight_dma_cg.sample(3); end
                end
            end
            32'h40420030: dst_run[st]=tr.data[0];
            32'h40420048: begin
                if (!dst_run[st]) fail("S2MM destination before RUN");
                if (st>=14 || tr.data!=ref_model.get_expected_fm_dst(st)) begin
                    mapping_mismatches++; fail($sformatf("Stage%0d dst actual=0x%08h expected=0x%08h",
                        st,tr.data,ref_model.get_expected_fm_dst(st)));
                end
                if (dst_addr_seen[st]) fail("Repeated destination programming in same stage");
                actual_dst[st]=tr.data; dst_addr_seen[st]=1;
                cov.fm_mapping_cg.sample(st,1,tr.data); cov.fm_stage_cg.sample(st);
                if (cfg.scenario_id==DMA_SCENARIO_S08)
                    `uvm_info("S08_MAPPING_OBSERVED",$sformatf("stage=%0d dst=0x%08h",st,tr.data),UVM_LOW)
            end
            32'h40420058: begin
                if (!dst_run[st] || !dst_addr_seen[st] || tr.data==0 || tr.resp!=0)
                    fail("S2MM LENGTH before complete destination programming");
                if (output_packet_beats!=0) fail("S2MM rearmed before output drain");
                dst_length[st]=tr.data; dst_arm_time[st]=tr.observed_time;
                dst_seen[st]=1; active_dst_stage=st;

            end
            32'h40400010: begin
                image_started=1; source_start_time[0]=tr.observed_time;
                if (!dst_seen[0] || dst_arm_time[0]>=tr.observed_time)
                    fail("Image source before fully armed S2MM");
                else cov.completion_order_cg.sample(3);
            end
            32'h40420000: src_run[st]=tr.data[0];
            32'h40420018: begin
                if (!src_run[st]) fail("MM2S source before RUN");
                if (st==0 || tr.data!=ref_model.get_expected_fm_src(st)) begin
                    mapping_mismatches++; fail($sformatf("Stage%0d src actual=0x%08h expected=0x%08h",
                        st,tr.data,ref_model.get_expected_fm_src(st)));
                end
                if (src_addr_seen[st]) fail("Repeated source programming in same stage");
                actual_src[st]=tr.data; src_addr_seen[st]=1;
                cov.fm_mapping_cg.sample(st,0,tr.data); cov.fm_stage_cg.sample(st);
                if (cfg.scenario_id==DMA_SCENARIO_S08)
                    `uvm_info("S08_MAPPING_OBSERVED",$sformatf("stage=%0d src=0x%08h",st,tr.data),UVM_LOW)
            end
            32'h40420028: begin
                if (!src_run[st] || !src_addr_seen[st] || tr.data==0 || tr.resp!=0)
                    fail("MM2S LENGTH before complete source programming");
                if (st<=13 && (dst_arm_time[st]==0 || dst_arm_time[st]>=tr.observed_time))
                    fail("MM2S source before fully armed S2MM");
                else if (st<=13) cov.body_arm_order_cg.sample(1);
                if (feature_in_packet_beats!=0) fail("MM2S rearmed before input drain");
                src_length[st]=tr.data; source_start_time[st]=tr.observed_time;
                src_seen[st]=1; active_src_stage=st;

            end
        endcase
    endfunction
    function void stream(cnn_seq_item tr);
        int expected_beats, valid_bytes;
        bit [7:0] expected_keep;
        if (tr.kind==CNN_IMAGE_BEAT) begin
            if (!image_started || $time<=source_start_time[0]) fail("Image beat before source commit");
            image_packet_beats++;
            if (tr.keep!=8'hff || tr.last!=(image_packet_beats==480)) fail("Image SG row KEEP/TLAST contract");
            if (tr.last) begin image_packets++; image_packet_beats=0; end
            if (image_packets>144) fail("Too many image rows");
            return;
        end
        if (tr.kind==CNN_FEATURE_OUT_BEAT) begin
            if (!dst_seen[active_dst_stage]) fail("Output before S2MM arm");
            output_packet_beats++;
            expected_beats=(dst_length[active_dst_stage]+7)/8;
            valid_bytes=(output_packet_beats==expected_beats && dst_length[active_dst_stage]%8!=0) ?
                dst_length[active_dst_stage]%8 : 8;
            expected_keep=(9'b1<<valid_bytes)-1;
            if (tr.keep!=expected_keep || tr.last!=(output_packet_beats==expected_beats))
                fail("Output KEEP/TLAST/accepted count mismatch");
            if (output_packet_beats>expected_beats) fail("Extra output acceptance");
            if (tr.last) begin
                if (active_dst_stage==0) begin
                    final_output_time=$time;
                    if (dma_done_time!=0 && dma_done_time<final_output_time) begin
                        cov.completion_order_cg.sample(0); cov.completion_order_cg.sample(1);
                        if (errors==0) cov.completion_order_cg.sample(2);
                    end
                end
                output_packets++; output_packet_beats=0; dst_seen[active_dst_stage]=0;
            end
            return;
        end
        if (tr.kind==CNN_FEATURE_IN_BEAT) begin
            if (!src_seen[active_src_stage] || $time<=source_start_time[active_src_stage])
                fail("Feature input before MM2S LENGTH commit");
            feature_in_packet_beats++;
            expected_beats=(src_length[active_src_stage]+7)/8;
            valid_bytes=(feature_in_packet_beats==expected_beats && src_length[active_src_stage]%8!=0) ?
                src_length[active_src_stage]%8 : 8;
            expected_keep=(9'b1<<valid_bytes)-1;
            if (tr.keep!=expected_keep || tr.last!=(feature_in_packet_beats==expected_beats))
                fail("Feature input KEEP/TLAST/count mismatch");
            if (feature_in_packet_beats>expected_beats) fail("Extra feature input acceptance");
            if (tr.last) begin
                feature_in_packets++; feature_in_packet_beats=0;
                src_seen[active_src_stage]=0;
            end
            return;
        end
        if (tr.kind!=CNN_WEIGHT_BEAT) return;
        if (weight_state!=3 || $time<=weight_commit_time) fail("Weight stream before LENGTH B commit");
        if (weight_packet_beats==0 && weight_packets==0 && $time>weight_commit_time)
            cov.weight_dma_cg.sample(4);
        expected_beats=(weight_length+7)/8;
        weight_packet_beats++;
        valid_bytes=(weight_packet_beats==expected_beats && weight_length%8!=0) ? weight_length%8 : 8;
        expected_keep=(9'b1<<valid_bytes)-1;
        if (tr.keep!=expected_keep) fail("Weight KEEP mismatch");
        if (tr.last != (weight_packet_beats==expected_beats)) fail("Weight TLAST position mismatch");
        if (weight_packet_beats>expected_beats) fail("Extra weight acceptance");
        if (tr.last) begin
            if (weight_packets==0 && weight_packet_beats==120 && errors==0)
                cov.weight_dma_cg.sample(5);
            weight_packets++; weight_packet_beats=0; weight_state=0;
        end
    endfunction
endclass
