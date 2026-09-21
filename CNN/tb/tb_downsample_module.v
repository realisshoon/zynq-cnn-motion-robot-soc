`timescale 1ns / 1ps
// Verilog-2001. Run with +SEED_<integer>; +GOLDEN uses local read-only vectors.
// Edges are sampled before NBA; #1 checks the committed state after that edge.
module tb_downsample_module;
reg clk=0; always #5 clk=~clk;
reg rst_n=0, cfg_valid=0, s_dma_valid=0, s_dma_last=0;
reg [255:0] cfg_desc=0;
reg [63:0] s_dma_data=0;
reg [7:0] s_dma_keep=8'hff;
reg m_rgb_ready=0;
wire cfg_ready,done,fault,s_dma_ready,m_rgb_valid,tap_accept,tap_last;
wire [23:0] m_rgb_data,tap_data;
wire [63:0] m_rgb_tag;
wire [9:0] tap_row;
wire [10:0] tap_col;
downsample_module dut(clk,rst_n,cfg_valid,cfg_ready,cfg_desc,done,fault,
 s_dma_data,s_dma_valid,s_dma_ready,s_dma_last,s_dma_keep,
 m_rgb_data,m_rgb_valid,m_rgb_ready,m_rgb_tag,
 tap_data,tap_row,tap_col,tap_accept,tap_last);

integer cycle=0, seed=1, initial_seed=1, dummy, random_word;
integer scenario=0, trace_fd, rgb_fd, golden_mode=0;
integer dma_n, output_n, real_n, tap_n, source_n, done_n, last_n;
integer frame_cycle, final_cycle, dma_complete_cycle, final_stalls;
integer top_n,bottom_n,full_stalls,selected_stalls,skip_stalls;
integer simultaneous_n,wr_wrap_n,rd_wrap_n,append_pop_n,row_end_n,last_order_n;
integer r,c,k,b,idx,residual_byte;
reg [255:0] accepted_cfg;
reg [4:0] expected_op;
reg frame_check=0, auto_drive=0, prev_stall=0;
reg [23:0] prev_data;
reg [63:0] prev_tag;
reg [7:0] image_bytes[0:552959];
reg [23:0] golden_rgb[0:65535];
reg [64:0] fifo_queue[0:99999];
integer queue_in=0,queue_out=0;
reg q_valid=0;
reg a_dma,a_write,a_read,a_append,a_pop,a_output,a_error,a_active;
reg [127:0] expected_hold,old_hold;
reg [64:0] old_read;
integer old_bytes,old_fc,old_wp,old_rp,old_source,old_out,old_dma;
integer old_sr,old_sc,old_or,old_oc,old_state;
integer old_pop_beat;
reg [2:0] old_mod;

task fail;
 input [8*180-1:0] reason;
 begin
   $display("FAILED cycle=%0d scenario=%0d: %0s",cycle,scenario,reason);
   $display("TEST RESULT: FAIL");
   $finish;
 end
endtask
function [7:0] component;
 input integer row,col,ch;
 begin
   if (golden_mode) component=image_bytes[(row*1280+col)*3+ch];
   else component=(row*37+col*13+ch*71+ch*col)&255;
 end
endfunction
function [23:0] pixel;
 input integer row,col;
 begin pixel={component(row,col,2),component(row,col,1),component(row,col,0)}; end
endfunction
task clear_counts;
 begin
   dma_n=0;output_n=0;real_n=0;tap_n=0;source_n=0;done_n=0;last_n=0;
   top_n=0;bottom_n=0;full_stalls=0;selected_stalls=0;skip_stalls=0;
   simultaneous_n=0;wr_wrap_n=0;rd_wrap_n=0;append_pop_n=0;row_end_n=0;
   last_order_n=0;
   frame_cycle=0;final_cycle=-1;dma_complete_cycle=-1;final_stalls=0;
 end
endtask
task reset_dut;
 begin
   auto_drive=0;frame_check=0;
   @(negedge clk);rst_n=0;cfg_valid=0;s_dma_valid=0;m_rgb_ready=0;
   repeat(5) begin
     @(posedge clk);#1;
     if (cfg_ready || s_dma_ready || m_rgb_valid || tap_accept || tap_last ||
         fault || done || m_rgb_data!==0 || m_rgb_tag!==0 ||
         tap_data!==0 || tap_row!==0 || tap_col!==0) fail("reset public outputs");
   end
   @(negedge clk);rst_n=1;
   clear_counts;
   #1;
   if (!cfg_ready || fault || dut.state!=0 || s_dma_ready || m_rgb_valid ||
       m_rgb_data!==0 || m_rgb_tag!==0 || tap_data!==0 || tap_row!==0 ||
       tap_col!==0 || tap_accept || tap_last || done) fail("post-reset idle outputs");
   if (^{cfg_ready,s_dma_ready,m_rgb_valid,m_rgb_data,m_rgb_tag,tap_data,
         tap_row,tap_col,tap_accept,tap_last,done,fault} === 1'bx)
     fail("post-reset idle X/Z");
 end
endtask
task start_cfg;
 begin
   cfg_desc=256'h000000000000000000000000000000000123456789abcdef0123456789abcde7;
   accepted_cfg=cfg_desc;expected_op=cfg_desc[4:0];cfg_valid=1;
   @(posedge clk);if (!cfg_ready) fail("cfg accept");
   #1;if (dut.cfg_latched!==accepted_cfg || dut.state!=1) fail("cfg latch/start");
   @(negedge clk);cfg_valid=0;
 end
endtask
task drive_dma;
 begin
   s_dma_valid=(dma_n<69120);
   s_dma_keep=8'hff;s_dma_last=(dma_n%480==479);
   for(k=0;k<8;k=k+1) begin
     b=(dma_n%480)*8+k;
     s_dma_data[k*8 +: 8]=component(dma_n/480,b/3,b%3);
   end
 end
endtask

// No TB/DUT race: only the driver writes inputs, on falling edges.
always @(negedge clk) begin
 if (rst_n && auto_drive) begin
   drive_dma;
   random_word=$random(seed);
   m_rgb_ready=(frame_cycle<600)?0:((random_word&7)!=0);
   // A sustained selected stall followed by randomized stalls.
   if (dut.state==2 && source_n==0 && selected_stalls<8) m_rgb_ready=0;
   if (output_n==65535 && final_stalls<6) begin
     m_rgb_ready=0; final_stalls=final_stalls+1;
   end
   if (frame_cycle==700) begin cfg_desc=~accepted_cfg;cfg_valid=1;end
   if (frame_cycle==705) cfg_valid=0;
 end
end

// Global independent transaction accounting and post-edge atomicity checker.
always @(posedge clk) begin
 cycle=cycle+1;
 if (!rst_n) begin
   prev_stall=0;queue_in=0;queue_out=0;q_valid=1;
 end else begin
   if (prev_stall && (!m_rgb_valid || m_rgb_data!==prev_data || m_rgb_tag!==prev_tag))
     fail("stalled valid/data/TAG changed");
   prev_stall=m_rgb_valid&&!m_rgb_ready;prev_data=m_rgb_data;prev_tag=m_rgb_tag;
   a_dma=s_dma_valid&&s_dma_ready;
   a_write=dut.fifo_write;a_read=dut.fifo_read;a_append=dut.append;
   a_pop=dut.rgb_pop;a_output=m_rgb_valid&&m_rgb_ready;
   a_error=dut.error_now;a_active=dut.active;
   old_bytes=dut.hold_bytes;old_hold=dut.hold_data;old_read=dut.read_data;
   old_pop_beat=dut.fifo_pop_beat;
   old_fc=dut.fifo_count;old_wp=dut.wr_ptr;old_rp=dut.rd_ptr;
   old_source=dut.real_count;old_out=dut.output_count;old_dma=dut.dma_total;
   old_sr=dut.source_row;old_sc=dut.source_col;old_mod=dut.col_mod5;
   old_or=dut.output_row;old_oc=dut.output_col;old_state=dut.state;
   expected_hold=old_hold;
   if(a_pop) expected_hold=expected_hold>>24;
   if(a_append) expected_hold=expected_hold|
      ({64'd0,old_read[63:0]}<<((old_bytes-3*a_pop)*8));
   if(a_write && q_valid) begin
     fifo_queue[queue_in]={s_dma_last,s_dma_data};queue_in=queue_in+1;
   end
   if(a_dma) dma_n=dma_n+1;
   if(a_pop) source_n=source_n+1;
   if(a_read&&a_write) simultaneous_n=simultaneous_n+1;
   if(a_write&&old_wp==511) wr_wrap_n=wr_wrap_n+1;
   if(a_read&&old_rp==511) rd_wrap_n=rd_wrap_n+1;
   if(a_append&&a_pop) append_pop_n=append_pop_n+1;
   if(a_error && a_active)
     $fdisplay(trace_fd,"FAULT cycle=%0d scenario=%0d A=%0d write=%0d read=%0d append=%0d B=%0d tap=%0d C=%0d hold=%0d source=%0d output=%0d",
       cycle,scenario,a_dma,a_write,a_read,a_append,a_pop,tap_accept,a_output,old_bytes,old_source,old_out);
   if (frame_check) begin
     frame_cycle=frame_cycle+1;
     if(fault) fail("normal frame fault");
     if(dut.cfg_latched!==accepted_cfg || dut.op_id!==expected_op) fail("active cfg mutated");
     if(dut.active && cfg_ready) fail("active cfg accepted");
     if(dut.state!=2 && tap_accept) fail("padding tap");
     if(dut.state==2 && dut.hold_bytes>=3) begin
       if(dut.source_col%5==0) begin
         if(tap_accept !== (m_rgb_valid&&m_rgb_ready)) fail("selected pop/handshake");
         if(!m_rgb_ready) selected_stalls=selected_stalls+1;
       end else begin
         if(!tap_accept || m_rgb_valid) fail("skip selector");
         if(!m_rgb_ready) skip_stalls=skip_stalls+1;
       end
     end
     if(s_dma_valid&&!s_dma_ready&&dut.fifo_count==512) full_stalls=full_stalls+1;
     if(a_dma&&dma_n==69120) dma_complete_cycle=cycle;
     if(tap_accept) begin
       r=tap_n/1280;c=tap_n%1280;
       if(tap_row!==r*5 || tap_col!==c || tap_data!==pixel(r,c)) fail("RGB unpack/tap coordinate/order");
       if(tap_last!==(tap_n==184319)) fail("tap_last");
       if(tap_last) last_n=last_n+1;
       if(c==1279) row_end_n=row_end_n+1;
       tap_n=tap_n+1;
     end else if(tap_last) fail("tap_last without tap");
     if(a_output) begin
       r=output_n/256;c=output_n%256;
       if(m_rgb_tag!=={24'd0,1'b0,(output_n==65535),expected_op,1'b1,16'd0,r[7:0],c[7:0]})
         fail("full TAG64");
       if(r<56 || r>=200) begin
         if(m_rgb_data!==24'd0) fail("padding RGB");
         if(r<56) top_n=top_n+1;else bottom_n=bottom_n+1;
       end else begin
         if(m_rgb_data!==pixel(r-56,c*5)) fail("stride5 RGB/order");
         real_n=real_n+1;
       end
       if(golden_mode) begin
         if(m_rgb_data!==golden_rgb[output_n]) fail("canonical golden RGB");
         if(scenario==35) $fdisplay(rgb_fd,"%06h",m_rgb_data);
       end
       if(output_n==65535) begin
         if(done) fail("early done on final handshake");
         final_cycle=cycle;
         $fdisplay(trace_fd,"FINAL cycle=%0d done=%0d tag=%h",cycle,done,m_rgb_tag);
       end
       output_n=output_n+1;
     end
     if(done !== (final_cycle>=0 && cycle==final_cycle+1)) fail("exact next-cycle done");
     if(done) begin
       done_n=done_n+1;
       $fdisplay(trace_fd,"DONE cycle=%0d",cycle);
     end
   end
   // Skip cfg edge because it intentionally reinitializes all frame state.
   if(!(cfg_valid&&cfg_ready)) begin
     #1;
     if(dut.real_count!==old_source+a_pop || dut.output_count!==old_out+a_output ||
        dut.dma_total!==old_dma+a_dma) fail("transaction counter half-commit");
     if(dut.hold_bytes!==old_bytes+8*a_append-3*a_pop ||
        dut.hold_data!==expected_hold) fail("holding append/pop atomicity");
     if(dut.fifo_count!==old_fc+a_write-a_read ||
        dut.wr_ptr!==((old_wp+a_write)&511) || dut.rd_ptr!==((old_rp+a_read)&511))
       fail("FIFO count/pointer atomicity");
     if(a_read&&q_valid) begin
       if(queue_out>=queue_in || dut.read_data!==fifo_queue[queue_out]) fail("FIFO ordering");
       queue_out=queue_out+1;
     end
     if(a_append) begin
       if(old_read[64] !== (old_pop_beat==479)) fail("FIFO last ordering");
       last_order_n=last_order_n+old_read[64];
       if(dut.fifo_pop_beat!==((old_pop_beat==479)?0:old_pop_beat+1))
         fail("FIFO stored-last beat counter");
     end
     if(a_pop) begin
       if(dut.source_col!==((old_sc==1279)?0:old_sc+1) ||
          dut.source_row!==old_sr+(old_sc==1279)) fail("source coordinate half-commit");
       if(old_sc==1279 && dut.col_mod5!==0) fail("row residual pixel alignment");
       // Prefetch may already contain the next row. No byte of the OLD row
       // may remain; every retained byte must be the next row's RGB stream.
       if(frame_check && old_sc==1279) begin
         for(residual_byte=0;residual_byte<dut.hold_bytes;residual_byte=residual_byte+1)
           if(dut.hold_data[residual_byte*8 +: 8]!==
              component(old_sr+1,residual_byte/3,residual_byte%3))
             fail("row boundary residual belongs to wrong row");
       end
     end else if(dut.source_col!==old_sc || dut.source_row!==old_sr)
       fail("source advanced without consume");
     if(a_output && (dut.output_col!==((old_oc+1)&255) ||
        dut.output_row!==((old_or+(old_oc==255))&255))) fail("output coordinate half-commit");
     if(!a_output && (dut.output_col!==old_oc || dut.output_row!==old_or))
       fail("output coordinate advanced without handshake");
     if(a_error&&a_active && (!fault || dut.state!=5)) fail("fault priority");
   end else begin queue_in=0;queue_out=0;q_valid=1;end
 end
 if(cycle>4000000) fail("watchdog timeout");
end

task check_lockout;
 integer n,old_count;
 reg [23:0] saved_data;
 reg [63:0] saved_tag;
 reg held;
 begin
   s_dma_valid=0;cfg_valid=1;held=m_rgb_valid;
   saved_data=m_rgb_data;saved_tag=m_rgb_tag;old_count=dut.output_count;
   for(n=0;n<4;n=n+1) begin
     @(negedge clk);
     if(!fault || cfg_ready || s_dma_ready || tap_accept) fail("fault sticky/new work lockout");
     if(held && (!m_rgb_valid || m_rgb_data!==saved_data || m_rgb_tag!==saved_tag))
       fail("fault held payload");
   end
   m_rgb_ready=1;
   @(negedge clk);
   if(m_rgb_valid || tap_accept || dut.output_count!=old_count+held)
     fail("fault drain once/count");
   repeat(2) @(negedge clk);
   if(m_rgb_valid || tap_accept || !fault) fail("post-fault new event");
   cfg_valid=0;m_rgb_ready=0;
 end
endtask
task protocol_fault;
 input integer which;
 integer n;
 begin
   scenario=which;reset_dut;start_cfg;
   s_dma_valid=1;s_dma_data=64'h0706050403020100;
   s_dma_keep=(which==19)?8'h7f:8'hff;s_dma_last=(which==20);
   for(n=0;n<((which==21)?480:1);n=n+1) begin
     @(posedge clk);if(!s_dma_ready) fail("protocol setup DMA blocked");
     @(negedge clk);
   end
   if(!fault) fail("protocol violation not detected");
   check_lockout;
   $display("CASE %0d PASS",which);
 end
endtask
task invalid_cfg_fault;
 begin
   scenario=42;reset_dut;
   cfg_desc=256'd31;cfg_valid=1;
   @(posedge clk);if(!cfg_ready || !dut.cfg_protocol_error)
     fail("invalid cfg not accepted/detected");
   #1;if(!fault || done || dut.state!=5 || dut.active)
     fail("invalid cfg started operation or missed fault");
   @(negedge clk);cfg_valid=0;
   check_lockout;
   $display("INVALID CFG PASS op_id range/reserved guard");
 end
endtask
// Reach REAL using only public inputs, then inject on selected/skip edge.
task real_fault;
 input integer which;
 integer found;
 begin
   scenario=which;reset_dut;start_cfg;
   found=0;
   while(!found) begin
     drive_dma;m_rgb_ready=1;
     if(dut.state==2 && dut.hold_bytes>=3 && s_dma_ready &&
        ((which==28)?(dut.col_mod5!=0):(dut.col_mod5==0))) found=1;
     else @(negedge clk);
   end
   m_rgb_ready=(which!=27);s_dma_keep=8'h7f;s_dma_valid=1;
   #1;
   if(!dut.protocol_error || tap_accept!==(which!=27) ||
      (m_rgb_valid&&m_rgb_ready)!==(which==26)) fail("fault overlap not exercised");
   @(negedge clk);
   if(!fault) fail("REAL fault missing");
   m_rgb_ready=0;check_lockout;
   $display("CASE %0d PASS",which);
 end
endtask
task range_fault;
 input integer which;
 begin
   scenario=34;reset_dut;start_cfg;
   // Illegal state injection tests otherwise unreachable counter encodings.
   // Rows/total limits also tested below by an actual excess DMA handshake.
   case(which)
    0:dut.dma_beat=480;
    1:dut.dma_row=144;
    2:dut.dma_total=69120;
    3:dut.fifo_count=513;
    4:dut.hold_bytes=17;
    5:dut.source_col=1280;
    6:dut.real_count=184321;
    7:dut.output_count=65537;
    8:dut.source_row=145;
    9:dut.col_mod5=5;
   endcase
   if(which==1 || which==2) begin s_dma_valid=1;s_dma_keep=255;s_dma_last=0;end
   @(negedge clk);
   if(!fault) fail("range/overflow missing fault");
   check_lockout;
   if(which==0) $display("CASE 29 PASS 481st-beat defensive range fault");
   $display("RANGE subcase=%0d PASS",which);
 end
endtask
task final_fault;
 begin
   scenario=37;reset_dut;start_cfg;
   // Entire legal frame prefix, then beat 69121 at the final output edge.
   // This tests public-input row/total overflow without counter forcing.
   frame_check=1;auto_drive=1;
   wait(output_n==65535);
   @(negedge clk);#1;auto_drive=0;frame_check=0;
   if(dma_n!=69120 || source_n!=184320 || dut.dma_row!=144 ||
      dut.dma_total!=69120) fail("excess DMA public-input prefix");
   m_rgb_ready=1;s_dma_valid=1;s_dma_keep=255;s_dma_last=0;
   #1;if(!m_rgb_valid || !s_dma_ready || done) fail("final fault setup");
   @(posedge clk);if(done) fail("final fault early done");
   @(negedge clk);s_dma_valid=0;
   if(!fault || done || dut.output_count!=65536 || dut.state!=5)
     fail("fault priority/done suppression");
   $fdisplay(trace_fd,"FAULT_PRIORITY_COMMIT cycle=%0d fault=%0d done=%0d state=%0d output=%0d normal_done_state=%0d",
     cycle,fault,done,dut.state,dut.output_count,(dut.state==4));
   @(posedge clk);if(done || !fault || dut.state==4) fail("fault priority N+1");
   @(negedge clk);if(done || !fault || dut.state==4) fail("fault priority sticky");
   $fdisplay(trace_fd,"FAULT_PRIORITY_STICKY cycle=%0d fault=%0d done=%0d state=%0d output=%0d normal_done_state=%0d",
     cycle,fault,done,dut.state,dut.output_count,(dut.state==4));
   m_rgb_ready=0;check_lockout;
   $display("CASE 25 PASS fault plus final output same-edge");
   $display("CASE 30 PASS 145th-row defensive fault");
   $display("CASE 37 PASS fault priority exact-cycle done suppressed");
 end
endtask
task fault_fifo_read;
 begin
   scenario=39;reset_dut;start_cfg;
   s_dma_valid=1;s_dma_keep=255;s_dma_last=0;s_dma_data=64'h0706050403020100;
   @(negedge clk);s_dma_keep=127;
   #1;if(!dut.fifo_read || !dut.protocol_error || dut.fifo_write)
     fail("fault plus FIFO read overlap not exercised");
   @(negedge clk);
   if(!fault || dut.rd_ptr!=1 || dut.wr_ptr!=1 || dut.fifo_count!=0)
     fail("fault FIFO read did not commit");
   check_lockout;
   $display("CASE 39 PASS fault plus FIFO read; invalid DMA discarded");
 end
endtask
task stored_last_fault;
 begin
   scenario=40;reset_dut;start_cfg;
   @(negedge clk);
   s_dma_valid=1;s_dma_keep=255;s_dma_last=0;s_dma_data=64'h0706050403020100;
   @(posedge clk);
   @(negedge clk);s_dma_valid=0;
   @(posedge clk);
   @(negedge clk);
   if(!dut.read_pending || dut.fifo_pop_beat!=0) fail("stored-last setup");
   // Fault-inject only the stored sideband after a legal FIFO read. This
   // proves the pop-side check uses the FIFO copy rather than live s_dma_last.
   dut.read_data[64]=1'b1;
   #1;if(!dut.stored_last_error || dut.append) fail("stored-last pop check");
   @(negedge clk);
   if(!fault || done || dut.fifo_pop_beat!=0) fail("stored-last defensive fault");
   check_lockout;
   $display("FIFO STORED LAST DEFENSIVE FAULT PASS");
 end
endtask
task tap_last_fault;
 begin
   scenario=41;reset_dut;start_cfg;
   // An input-protocol error carrying the final source RGB cannot reach the
   // holding buffer legally because the bad beat is discarded at push. This
   // state injection isolates the atomic tap_last candidate race.
   dut.state=2;dut.source_row=143;dut.source_col=1279;dut.col_mod5=4;
   dut.hold_data=128'h00000000000000000000000000332211;
   dut.hold_bytes=3;dut.real_count=184319;
   s_dma_valid=1;s_dma_keep=127;s_dma_last=0;m_rgb_ready=0;
   #1;if(!dut.protocol_error || !tap_accept || !tap_last) fail("tap_last fault setup");
   @(negedge clk);s_dma_valid=0;
   if(!fault || done || dut.real_count!=184320 || dut.source_row!=144 ||
      dut.source_col!=0) fail("tap_last fault atomic commit");
   check_lockout;
   $display("FAULT PLUS TAP_LAST CANDIDATE PASS");
 end
endtask
task full_frame;
 input integer number;
 integer n;
 begin
   scenario=number;clear_counts;start_cfg;
   frame_check=1;auto_drive=1;
   wait(final_cycle>=0);
   repeat(3) @(negedge clk);
   auto_drive=0;frame_check=0;s_dma_valid=0;cfg_valid=0;
   if(dma_n!=69120 || source_n!=184320 || tap_n!=184320 || real_n!=36864 ||
      output_n!=65536 || last_n!=1 || done_n!=1 || fault ||
      top_n!=14336 || bottom_n!=14336 || row_end_n!=144 ||
      !cfg_ready || dut.hold_bytes!=0 || dut.fifo_count!=0 || dut.read_pending ||
      full_stalls==0 || selected_stalls==0 || skip_stalls==0 ||
      simultaneous_n==0 || wr_wrap_n==0 || rd_wrap_n==0 || append_pop_n==0 ||
      last_order_n!=144 ||
      final_stalls!=6 || dma_complete_cycle>=final_cycle) fail("frame totals/coverage");
   $display("FRAME PASS seed=%0d operation=%0d DMA=%0d source=%0d REAL=%0d TOTAL=%0d tap=%0d tap_last=%0d done=%0d fault=%0d",
      initial_seed,number,dma_n,source_n,real_n,output_n,tap_n,last_n,done_n,fault);
   $display("COVERAGE full=%0d selected_stall=%0d skip_stall=%0d fifo_rw=%0d wr_wrap=%0d rd_wrap=%0d append_pop=%0d fifo_last=%0d row_end=%0d final_stall=%0d",
      full_stalls,selected_stalls,skip_stalls,simultaneous_n,wr_wrap_n,rd_wrap_n,append_pop_n,last_order_n,row_end_n,final_stalls);
   for(n=1;n<=18;n=n+1) $display("CASE %0d PASS",n);
   $display("CASE 24 PASS");
   $display("CASE 31 PASS FIFO simultaneous read/write");
   $display("CASE 32 PASS FIFO pointer wrap data/last");
   $display("CASE 33 PASS FIFO last ordering");
   $display("CASE 34 PASS active cfg_desc mutation");
   $display("CASE 35 PASS holding append+pop");
   $display("CASE 36 PASS row residual");
 end
endtask
initial begin
 dummy=$value$plusargs("SEED_%d",initial_seed);seed=initial_seed;
 golden_mode=$test$plusargs("GOLDEN");
 trace_fd=$fopen("events.log","w");if(!trace_fd) fail("event log open");
 if($test$plusargs("FAIL_PROBE")) fail("intentional regression gate probe");
 if(golden_mode) begin
   $readmemh("../image_bytes.hex",image_bytes);
   $readmemh("../golden_rgb.hex",golden_rgb);
   rgb_fd=$fopen("rtl_rgb.hex","w");if(!rgb_fd) fail("RGB log open");
 end
 clear_counts;
 invalid_cfg_fault;
 protocol_fault(19);protocol_fault(20);protocol_fault(21);
 real_fault(26);real_fault(27);real_fault(28);
 fault_fifo_read;
 stored_last_fault;
 tap_last_fault;
 for(idx=0;idx<10;idx=idx+1) range_fault(idx);
 final_fault;
 reset_dut;full_frame(35); // Full operation after sticky fault reset.
 $display("CASE 22 PASS");$display("CASE 23 PASS");
 // Repeat without reset to verify cfg reuse and per-operation initialization.
 if(!golden_mode) full_frame(38);
 if(golden_mode) begin
   $fclose(rgb_fd);$display("GOLDEN FULL FRAME PASS compared=65536 mismatch=0");
 end
 $display("RESET X/Z = 0");
 $display("CANONICAL RESET MISMATCH = 0");
 $display("POST-RESET IDLE X/Z = 0");
 $display("4-STATE RESET = PASS");
 $fclose(trace_fd);
 $display("TEST RESULT: PASS");$finish;
end
endmodule
