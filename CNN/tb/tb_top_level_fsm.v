// Self-checking Verilog-2001 testbench.
module tb_top_level_fsm;
reg clk;
reg rst_n;
wire input_cfg_valid;
reg input_cfg_ready;
wire [255:0] input_cfg_desc;
reg input_done;
reg input_fault;
wire line_cfg_valid;
reg line_cfg_ready;
wire [255:0] line_cfg_desc;
reg line_done;
reg line_fault;
wire dw_cfg_valid;
reg dw_cfg_ready;
wire [255:0] dw_cfg_desc;
reg dw_done;
reg dw_fault;
wire pw_cfg_valid;
reg pw_cfg_ready;
wire [255:0] pw_cfg_desc;
reg pw_done;
reg pw_fault;
wire fm_cfg_valid;
reg fm_cfg_ready;
wire [255:0] fm_cfg_desc;
reg fm_done;
reg fm_fault;
wire [31:0] fm_src_addr;
wire [31:0] fm_dst_addr;
wire [19:0] fm_src_bytes;
wire [19:0] fm_dst_bytes;
wire fm_read_en;
wire fm_write_en;
wire fm_read_dma_done;
wire fm_write_dma_done;
wire fm_dma_error;
reg fm_read_done;
reg fm_write_done;
wire down_cfg_valid;
reg down_cfg_ready;
wire [255:0] down_cfg_desc;
reg down_done;
reg down_fault;
wire arg_cfg_valid;
reg arg_cfg_ready;
wire [255:0] arg_cfg_desc;
reg arg_done;
reg arg_fault;
wire coord_cfg_valid;
reg coord_cfg_ready;
wire [255:0] coord_cfg_desc;
reg coord_done;
reg coord_fault;
reg [31:0] coord_m_joint_data;
reg coord_m_joint_valid;
wire coord_m_joint_ready;
reg coord_m_joint_last;
reg [4:0] coord_m_joint_index;
reg coord_m_joint_good;
wire [7:0] coord_threshold;
reg swap_fault;
wire swap_load_valid;
reg swap_load_ready;
wire [255:0] swap_load_desc;
reg swap_load_done;
reg color_fault;
wire color_frame_start;
wire [23:0] color_red_cfg;
wire [23:0] color_blue_cfg;
wire [17:0] color_min_count;
reg [31:0] color_red_word;
reg [31:0] color_blue_word;
reg color_results_valid;
reg rom_fault;
wire rom_req_valid;
reg rom_req_ready;
wire [4:0] rom_req_op;
reg rom_rsp_valid;
wire rom_rsp_ready;
reg [255:0] rom_rsp_desc;
reg [7:0] threshold_cfg;
reg [23:0] red_thresh_cfg;
reg [23:0] blue_thresh_cfg;
reg [17:0] min_count_cfg;
reg datapath_progress;
reg start;
reg clear_done;
reg clear_error;
reg [31:0] frame_id;
reg [31:0] sg_desc_base;
reg [31:0] wgt_base;
reg [31:0] fm_a_base;
reg [31:0] fm_b_base;
reg [31:0] timeout_cycles;
wire busy;
wire done_pending;
wire error_pending;
wire [31:0] error_code;
wire [31:0] result_seq;
wire [31:0] result_frame_id;
wire [543:0] joint_words;
wire [16:0] joint_flags;
wire [31:0] red_word;
wire [31:0] blue_word;
wire [31:0] cycle_count;
wire image_read_done;
wire [31:0] m_axil_awaddr;
wire m_axil_awvalid;
reg m_axil_awready;
wire [2:0] m_axil_awprot;
wire [31:0] m_axil_wdata;
wire [3:0] m_axil_wstrb;
wire m_axil_wvalid;
reg m_axil_wready;
reg [1:0] m_axil_bresp;
reg m_axil_bvalid;
wire m_axil_bready;
wire [31:0] m_axil_araddr;
wire m_axil_arvalid;
reg m_axil_arready;
wire [2:0] m_axil_arprot;
reg [31:0] m_axil_rdata;
reg [1:0] m_axil_rresp;
reg m_axil_rvalid;
wire m_axil_rready;
top_level_fsm dut (
    .clk(clk),
    .rst_n(rst_n),
    .input_cfg_valid(input_cfg_valid),
    .input_cfg_ready(input_cfg_ready),
    .input_cfg_desc(input_cfg_desc),
    .input_done(input_done),
    .input_fault(input_fault),
    .line_cfg_valid(line_cfg_valid),
    .line_cfg_ready(line_cfg_ready),
    .line_cfg_desc(line_cfg_desc),
    .line_done(line_done),
    .line_fault(line_fault),
    .dw_cfg_valid(dw_cfg_valid),
    .dw_cfg_ready(dw_cfg_ready),
    .dw_cfg_desc(dw_cfg_desc),
    .dw_done(dw_done),
    .dw_fault(dw_fault),
    .pw_cfg_valid(pw_cfg_valid),
    .pw_cfg_ready(pw_cfg_ready),
    .pw_cfg_desc(pw_cfg_desc),
    .pw_done(pw_done),
    .pw_fault(pw_fault),
    .fm_cfg_valid(fm_cfg_valid),
    .fm_cfg_ready(fm_cfg_ready),
    .fm_cfg_desc(fm_cfg_desc),
    .fm_done(fm_done),
    .fm_fault(fm_fault),
    .fm_src_addr(fm_src_addr),
    .fm_dst_addr(fm_dst_addr),
    .fm_src_bytes(fm_src_bytes),
    .fm_dst_bytes(fm_dst_bytes),
    .fm_read_en(fm_read_en),
    .fm_write_en(fm_write_en),
    .fm_read_dma_done(fm_read_dma_done),
    .fm_write_dma_done(fm_write_dma_done),
    .fm_dma_error(fm_dma_error),
    .fm_read_done(fm_read_done),
    .fm_write_done(fm_write_done),
    .down_cfg_valid(down_cfg_valid),
    .down_cfg_ready(down_cfg_ready),
    .down_cfg_desc(down_cfg_desc),
    .down_done(down_done),
    .down_fault(down_fault),
    .arg_cfg_valid(arg_cfg_valid),
    .arg_cfg_ready(arg_cfg_ready),
    .arg_cfg_desc(arg_cfg_desc),
    .arg_done(arg_done),
    .arg_fault(arg_fault),
    .coord_cfg_valid(coord_cfg_valid),
    .coord_cfg_ready(coord_cfg_ready),
    .coord_cfg_desc(coord_cfg_desc),
    .coord_done(coord_done),
    .coord_fault(coord_fault),
    .coord_m_joint_data(coord_m_joint_data),
    .coord_m_joint_valid(coord_m_joint_valid),
    .coord_m_joint_ready(coord_m_joint_ready),
    .coord_m_joint_last(coord_m_joint_last),
    .coord_m_joint_index(coord_m_joint_index),
    .coord_m_joint_good(coord_m_joint_good),
    .coord_threshold(coord_threshold),
    .swap_fault(swap_fault),
    .swap_load_valid(swap_load_valid),
    .swap_load_ready(swap_load_ready),
    .swap_load_desc(swap_load_desc),
    .swap_load_done(swap_load_done),
    .color_fault(color_fault),
    .color_frame_start(color_frame_start),
    .color_red_cfg(color_red_cfg),
    .color_blue_cfg(color_blue_cfg),
    .color_min_count(color_min_count),
    .color_red_word(color_red_word),
    .color_blue_word(color_blue_word),
    .color_results_valid(color_results_valid),
    .rom_fault(rom_fault),
    .rom_req_valid(rom_req_valid),
    .rom_req_ready(rom_req_ready),
    .rom_req_op(rom_req_op),
    .rom_rsp_valid(rom_rsp_valid),
    .rom_rsp_ready(rom_rsp_ready),
    .rom_rsp_desc(rom_rsp_desc),
    .threshold_cfg(threshold_cfg),
    .red_thresh_cfg(red_thresh_cfg),
    .blue_thresh_cfg(blue_thresh_cfg),
    .min_count_cfg(min_count_cfg),
    .datapath_progress(datapath_progress),
    .start(start),
    .clear_done(clear_done),
    .clear_error(clear_error),
    .frame_id(frame_id),
    .sg_desc_base(sg_desc_base),
    .wgt_base(wgt_base),
    .fm_a_base(fm_a_base),
    .fm_b_base(fm_b_base),
    .timeout_cycles(timeout_cycles),
    .busy(busy),
    .done_pending(done_pending),
    .error_pending(error_pending),
    .error_code(error_code),
    .result_seq(result_seq),
    .result_frame_id(result_frame_id),
    .joint_words(joint_words),
    .joint_flags(joint_flags),
    .red_word(red_word),
    .blue_word(blue_word),
    .cycle_count(cycle_count),
    .image_read_done(image_read_done),
    .m_axil_awaddr(m_axil_awaddr),
    .m_axil_awvalid(m_axil_awvalid),
    .m_axil_awready(m_axil_awready),
    .m_axil_awprot(m_axil_awprot),
    .m_axil_wdata(m_axil_wdata),
    .m_axil_wstrb(m_axil_wstrb),
    .m_axil_wvalid(m_axil_wvalid),
    .m_axil_wready(m_axil_wready),
    .m_axil_bresp(m_axil_bresp),
    .m_axil_bvalid(m_axil_bvalid),
    .m_axil_bready(m_axil_bready),
    .m_axil_araddr(m_axil_araddr),
    .m_axil_arvalid(m_axil_arvalid),
    .m_axil_arready(m_axil_arready),
    .m_axil_arprot(m_axil_arprot),
    .m_axil_rdata(m_axil_rdata),
    .m_axil_rresp(m_axil_rresp),
    .m_axil_rvalid(m_axil_rvalid),
    .m_axil_rready(m_axil_rready)
);

initial begin
    clk=0;
    rst_n=0;
    input_cfg_ready=0;
    input_done=0;
    input_fault=0;
    line_cfg_ready=0;
    line_done=0;
    line_fault=0;
    dw_cfg_ready=0;
    dw_done=0;
    dw_fault=0;
    pw_cfg_ready=0;
    pw_done=0;
    pw_fault=0;
    fm_cfg_ready=0;
    fm_done=0;
    fm_fault=0;
    fm_read_done=0;
    fm_write_done=0;
    down_cfg_ready=0;
    down_done=0;
    down_fault=0;
    arg_cfg_ready=0;
    arg_done=0;
    arg_fault=0;
    coord_cfg_ready=0;
    coord_done=0;
    coord_fault=0;
    coord_m_joint_data=0;
    coord_m_joint_valid=0;
    coord_m_joint_last=0;
    coord_m_joint_index=0;
    coord_m_joint_good=0;
    swap_fault=0;
    swap_load_ready=0;
    swap_load_done=0;
    color_fault=0;
    color_red_word=0;
    color_blue_word=0;
    color_results_valid=0;
    rom_fault=0;
    rom_req_ready=0;
    rom_rsp_valid=0;
    rom_rsp_desc=0;
    threshold_cfg=0;
    red_thresh_cfg=0;
    blue_thresh_cfg=0;
    min_count_cfg=0;
    datapath_progress=0;
    start=0;
    clear_done=0;
    clear_error=0;
    frame_id=0;
    sg_desc_base=0;
    wgt_base=0;
    fm_a_base=0;
    fm_b_base=0;
    timeout_cycles=0;
    m_axil_awready=0;
    m_axil_wready=0;
    m_axil_bresp=0;
    m_axil_bvalid=0;
    m_axil_arready=0;
    m_axil_rdata=0;
    m_axil_rresp=0;
    m_axil_rvalid=0;
end

integer sim_cycle, rom_count, load_count, stage_count;
integer aw_first_count, w_first_count;
integer run_age, weight_hold, feature_read_hold, feature_write_hold;
integer image_hold, rom_delay, swap_delay;
reg rom_pending, swap_pending;
reg aw_seen, w_seen, dest_armed;
reg write_mode;
reg [31:0] aw_addr, w_value;
reg [3:0] prior_stage;
reg [4:0] prior_state;
reg inject_b_error, inject_r_error;
reg inject_status_error;
reg pause_completion;
reg [31:0] expected_frame;
reg prev_rom_stall, prev_load_stall, prev_fm_stall, prev_pw_stall;
reg [4:0] prev_rom_op;
reg [255:0] prev_load_desc, prev_fm_desc, prev_pw_desc;
reg [255:0] expected_dw, expected_pw;

always #5 clk=~clk;

task fail;
    input [255:0] message;
    begin
        $display("FAIL: %0s at cycle %0d state %0d stage %0d",
                 message,sim_cycle,dut.state,dut.stage);
        $finish;
    end
endtask

integer reset_sampled_edges, reset_output_checks, reset_log_fd;
reg reset_values_logged;
initial begin
    reset_sampled_edges=0; reset_output_checks=0; reset_values_logged=0;
    reset_log_fd=$fopen("reset_output_values.csv", "w");
    if (reset_log_fd==0) fail("reset CSV open");
    $fdisplay(reset_log_fd, "port,width,canonical,observed,result");
end

// Sample after the synchronous reset nonblocking assignments settle.
always @(posedge clk) begin
    if (!rst_n) begin
        #1;
        reset_sampled_edges=reset_sampled_edges+1;
        if ((^input_cfg_valid) === 1'bx) begin
            $display("FAIL: reset X/Z input_cfg_valid"); $finish;
        end
        if (input_cfg_valid !== 1'h0) begin
            $display("FAIL: reset input_cfg_valid expected %h observed %h", 1'h0, input_cfg_valid); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^input_cfg_desc) === 1'bx) begin
            $display("FAIL: reset X/Z input_cfg_desc"); $finish;
        end
        if (input_cfg_desc !== 256'h0) begin
            $display("FAIL: reset input_cfg_desc expected %h observed %h", 256'h0, input_cfg_desc); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^line_cfg_valid) === 1'bx) begin
            $display("FAIL: reset X/Z line_cfg_valid"); $finish;
        end
        if (line_cfg_valid !== 1'h0) begin
            $display("FAIL: reset line_cfg_valid expected %h observed %h", 1'h0, line_cfg_valid); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^line_cfg_desc) === 1'bx) begin
            $display("FAIL: reset X/Z line_cfg_desc"); $finish;
        end
        if (line_cfg_desc !== 256'h0) begin
            $display("FAIL: reset line_cfg_desc expected %h observed %h", 256'h0, line_cfg_desc); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^dw_cfg_valid) === 1'bx) begin
            $display("FAIL: reset X/Z dw_cfg_valid"); $finish;
        end
        if (dw_cfg_valid !== 1'h0) begin
            $display("FAIL: reset dw_cfg_valid expected %h observed %h", 1'h0, dw_cfg_valid); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^dw_cfg_desc) === 1'bx) begin
            $display("FAIL: reset X/Z dw_cfg_desc"); $finish;
        end
        if (dw_cfg_desc !== 256'h0) begin
            $display("FAIL: reset dw_cfg_desc expected %h observed %h", 256'h0, dw_cfg_desc); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^pw_cfg_valid) === 1'bx) begin
            $display("FAIL: reset X/Z pw_cfg_valid"); $finish;
        end
        if (pw_cfg_valid !== 1'h0) begin
            $display("FAIL: reset pw_cfg_valid expected %h observed %h", 1'h0, pw_cfg_valid); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^pw_cfg_desc) === 1'bx) begin
            $display("FAIL: reset X/Z pw_cfg_desc"); $finish;
        end
        if (pw_cfg_desc !== 256'h0) begin
            $display("FAIL: reset pw_cfg_desc expected %h observed %h", 256'h0, pw_cfg_desc); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^fm_cfg_valid) === 1'bx) begin
            $display("FAIL: reset X/Z fm_cfg_valid"); $finish;
        end
        if (fm_cfg_valid !== 1'h0) begin
            $display("FAIL: reset fm_cfg_valid expected %h observed %h", 1'h0, fm_cfg_valid); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^fm_cfg_desc) === 1'bx) begin
            $display("FAIL: reset X/Z fm_cfg_desc"); $finish;
        end
        if (fm_cfg_desc !== 256'h0) begin
            $display("FAIL: reset fm_cfg_desc expected %h observed %h", 256'h0, fm_cfg_desc); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^fm_src_addr) === 1'bx) begin
            $display("FAIL: reset X/Z fm_src_addr"); $finish;
        end
        if (fm_src_addr !== 32'h0) begin
            $display("FAIL: reset fm_src_addr expected %h observed %h", 32'h0, fm_src_addr); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^fm_dst_addr) === 1'bx) begin
            $display("FAIL: reset X/Z fm_dst_addr"); $finish;
        end
        if (fm_dst_addr !== 32'h0) begin
            $display("FAIL: reset fm_dst_addr expected %h observed %h", 32'h0, fm_dst_addr); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^fm_src_bytes) === 1'bx) begin
            $display("FAIL: reset X/Z fm_src_bytes"); $finish;
        end
        if (fm_src_bytes !== 20'h0) begin
            $display("FAIL: reset fm_src_bytes expected %h observed %h", 20'h0, fm_src_bytes); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^fm_dst_bytes) === 1'bx) begin
            $display("FAIL: reset X/Z fm_dst_bytes"); $finish;
        end
        if (fm_dst_bytes !== 20'h0) begin
            $display("FAIL: reset fm_dst_bytes expected %h observed %h", 20'h0, fm_dst_bytes); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^fm_read_en) === 1'bx) begin
            $display("FAIL: reset X/Z fm_read_en"); $finish;
        end
        if (fm_read_en !== 1'h0) begin
            $display("FAIL: reset fm_read_en expected %h observed %h", 1'h0, fm_read_en); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^fm_write_en) === 1'bx) begin
            $display("FAIL: reset X/Z fm_write_en"); $finish;
        end
        if (fm_write_en !== 1'h0) begin
            $display("FAIL: reset fm_write_en expected %h observed %h", 1'h0, fm_write_en); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^fm_read_dma_done) === 1'bx) begin
            $display("FAIL: reset X/Z fm_read_dma_done"); $finish;
        end
        if (fm_read_dma_done !== 1'h0) begin
            $display("FAIL: reset fm_read_dma_done expected %h observed %h", 1'h0, fm_read_dma_done); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^fm_write_dma_done) === 1'bx) begin
            $display("FAIL: reset X/Z fm_write_dma_done"); $finish;
        end
        if (fm_write_dma_done !== 1'h0) begin
            $display("FAIL: reset fm_write_dma_done expected %h observed %h", 1'h0, fm_write_dma_done); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^fm_dma_error) === 1'bx) begin
            $display("FAIL: reset X/Z fm_dma_error"); $finish;
        end
        if (fm_dma_error !== 1'h0) begin
            $display("FAIL: reset fm_dma_error expected %h observed %h", 1'h0, fm_dma_error); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^down_cfg_valid) === 1'bx) begin
            $display("FAIL: reset X/Z down_cfg_valid"); $finish;
        end
        if (down_cfg_valid !== 1'h0) begin
            $display("FAIL: reset down_cfg_valid expected %h observed %h", 1'h0, down_cfg_valid); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^down_cfg_desc) === 1'bx) begin
            $display("FAIL: reset X/Z down_cfg_desc"); $finish;
        end
        if (down_cfg_desc !== 256'h0) begin
            $display("FAIL: reset down_cfg_desc expected %h observed %h", 256'h0, down_cfg_desc); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^arg_cfg_valid) === 1'bx) begin
            $display("FAIL: reset X/Z arg_cfg_valid"); $finish;
        end
        if (arg_cfg_valid !== 1'h0) begin
            $display("FAIL: reset arg_cfg_valid expected %h observed %h", 1'h0, arg_cfg_valid); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^arg_cfg_desc) === 1'bx) begin
            $display("FAIL: reset X/Z arg_cfg_desc"); $finish;
        end
        if (arg_cfg_desc !== 256'h0) begin
            $display("FAIL: reset arg_cfg_desc expected %h observed %h", 256'h0, arg_cfg_desc); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^coord_cfg_valid) === 1'bx) begin
            $display("FAIL: reset X/Z coord_cfg_valid"); $finish;
        end
        if (coord_cfg_valid !== 1'h0) begin
            $display("FAIL: reset coord_cfg_valid expected %h observed %h", 1'h0, coord_cfg_valid); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^coord_cfg_desc) === 1'bx) begin
            $display("FAIL: reset X/Z coord_cfg_desc"); $finish;
        end
        if (coord_cfg_desc !== 256'h0) begin
            $display("FAIL: reset coord_cfg_desc expected %h observed %h", 256'h0, coord_cfg_desc); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^coord_m_joint_ready) === 1'bx) begin
            $display("FAIL: reset X/Z coord_m_joint_ready"); $finish;
        end
        if (coord_m_joint_ready !== 1'h0) begin
            $display("FAIL: reset coord_m_joint_ready expected %h observed %h", 1'h0, coord_m_joint_ready); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^coord_threshold) === 1'bx) begin
            $display("FAIL: reset X/Z coord_threshold"); $finish;
        end
        if (coord_threshold !== 8'hd2) begin
            $display("FAIL: reset coord_threshold expected %h observed %h", 8'hd2, coord_threshold); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^swap_load_valid) === 1'bx) begin
            $display("FAIL: reset X/Z swap_load_valid"); $finish;
        end
        if (swap_load_valid !== 1'h0) begin
            $display("FAIL: reset swap_load_valid expected %h observed %h", 1'h0, swap_load_valid); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^swap_load_desc) === 1'bx) begin
            $display("FAIL: reset X/Z swap_load_desc"); $finish;
        end
        if (swap_load_desc !== 256'h0) begin
            $display("FAIL: reset swap_load_desc expected %h observed %h", 256'h0, swap_load_desc); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^color_frame_start) === 1'bx) begin
            $display("FAIL: reset X/Z color_frame_start"); $finish;
        end
        if (color_frame_start !== 1'h0) begin
            $display("FAIL: reset color_frame_start expected %h observed %h", 1'h0, color_frame_start); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^color_red_cfg) === 1'bx) begin
            $display("FAIL: reset X/Z color_red_cfg"); $finish;
        end
        if (color_red_cfg !== 24'h6464a0) begin
            $display("FAIL: reset color_red_cfg expected %h observed %h", 24'h6464a0, color_red_cfg); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^color_blue_cfg) === 1'bx) begin
            $display("FAIL: reset X/Z color_blue_cfg"); $finish;
        end
        if (color_blue_cfg !== 24'ha06464) begin
            $display("FAIL: reset color_blue_cfg expected %h observed %h", 24'ha06464, color_blue_cfg); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^color_min_count) === 1'bx) begin
            $display("FAIL: reset X/Z color_min_count"); $finish;
        end
        if (color_min_count !== 18'h8) begin
            $display("FAIL: reset color_min_count expected %h observed %h", 18'h8, color_min_count); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^rom_req_valid) === 1'bx) begin
            $display("FAIL: reset X/Z rom_req_valid"); $finish;
        end
        if (rom_req_valid !== 1'h0) begin
            $display("FAIL: reset rom_req_valid expected %h observed %h", 1'h0, rom_req_valid); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^rom_req_op) === 1'bx) begin
            $display("FAIL: reset X/Z rom_req_op"); $finish;
        end
        if (rom_req_op !== 5'h0) begin
            $display("FAIL: reset rom_req_op expected %h observed %h", 5'h0, rom_req_op); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^rom_rsp_ready) === 1'bx) begin
            $display("FAIL: reset X/Z rom_rsp_ready"); $finish;
        end
        if (rom_rsp_ready !== 1'h0) begin
            $display("FAIL: reset rom_rsp_ready expected %h observed %h", 1'h0, rom_rsp_ready); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^busy) === 1'bx) begin
            $display("FAIL: reset X/Z busy"); $finish;
        end
        if (busy !== 1'h0) begin
            $display("FAIL: reset busy expected %h observed %h", 1'h0, busy); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^done_pending) === 1'bx) begin
            $display("FAIL: reset X/Z done_pending"); $finish;
        end
        if (done_pending !== 1'h0) begin
            $display("FAIL: reset done_pending expected %h observed %h", 1'h0, done_pending); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^error_pending) === 1'bx) begin
            $display("FAIL: reset X/Z error_pending"); $finish;
        end
        if (error_pending !== 1'h0) begin
            $display("FAIL: reset error_pending expected %h observed %h", 1'h0, error_pending); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^error_code) === 1'bx) begin
            $display("FAIL: reset X/Z error_code"); $finish;
        end
        if (error_code !== 32'h0) begin
            $display("FAIL: reset error_code expected %h observed %h", 32'h0, error_code); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^result_seq) === 1'bx) begin
            $display("FAIL: reset X/Z result_seq"); $finish;
        end
        if (result_seq !== 32'h0) begin
            $display("FAIL: reset result_seq expected %h observed %h", 32'h0, result_seq); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^result_frame_id) === 1'bx) begin
            $display("FAIL: reset X/Z result_frame_id"); $finish;
        end
        if (result_frame_id !== 32'h0) begin
            $display("FAIL: reset result_frame_id expected %h observed %h", 32'h0, result_frame_id); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^joint_words) === 1'bx) begin
            $display("FAIL: reset X/Z joint_words"); $finish;
        end
        if (joint_words !== 544'h0) begin
            $display("FAIL: reset joint_words expected %h observed %h", 544'h0, joint_words); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^joint_flags) === 1'bx) begin
            $display("FAIL: reset X/Z joint_flags"); $finish;
        end
        if (joint_flags !== 17'h0) begin
            $display("FAIL: reset joint_flags expected %h observed %h", 17'h0, joint_flags); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^red_word) === 1'bx) begin
            $display("FAIL: reset X/Z red_word"); $finish;
        end
        if (red_word !== 32'h0) begin
            $display("FAIL: reset red_word expected %h observed %h", 32'h0, red_word); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^blue_word) === 1'bx) begin
            $display("FAIL: reset X/Z blue_word"); $finish;
        end
        if (blue_word !== 32'h0) begin
            $display("FAIL: reset blue_word expected %h observed %h", 32'h0, blue_word); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^cycle_count) === 1'bx) begin
            $display("FAIL: reset X/Z cycle_count"); $finish;
        end
        if (cycle_count !== 32'h0) begin
            $display("FAIL: reset cycle_count expected %h observed %h", 32'h0, cycle_count); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^image_read_done) === 1'bx) begin
            $display("FAIL: reset X/Z image_read_done"); $finish;
        end
        if (image_read_done !== 1'h0) begin
            $display("FAIL: reset image_read_done expected %h observed %h", 1'h0, image_read_done); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^m_axil_awaddr) === 1'bx) begin
            $display("FAIL: reset X/Z m_axil_awaddr"); $finish;
        end
        if (m_axil_awaddr !== 32'h0) begin
            $display("FAIL: reset m_axil_awaddr expected %h observed %h", 32'h0, m_axil_awaddr); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^m_axil_awvalid) === 1'bx) begin
            $display("FAIL: reset X/Z m_axil_awvalid"); $finish;
        end
        if (m_axil_awvalid !== 1'h0) begin
            $display("FAIL: reset m_axil_awvalid expected %h observed %h", 1'h0, m_axil_awvalid); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^m_axil_awprot) === 1'bx) begin
            $display("FAIL: reset X/Z m_axil_awprot"); $finish;
        end
        if (m_axil_awprot !== 3'h0) begin
            $display("FAIL: reset m_axil_awprot expected %h observed %h", 3'h0, m_axil_awprot); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^m_axil_wdata) === 1'bx) begin
            $display("FAIL: reset X/Z m_axil_wdata"); $finish;
        end
        if (m_axil_wdata !== 32'h0) begin
            $display("FAIL: reset m_axil_wdata expected %h observed %h", 32'h0, m_axil_wdata); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^m_axil_wstrb) === 1'bx) begin
            $display("FAIL: reset X/Z m_axil_wstrb"); $finish;
        end
        if (m_axil_wstrb !== 4'h0) begin
            $display("FAIL: reset m_axil_wstrb expected %h observed %h", 4'h0, m_axil_wstrb); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^m_axil_wvalid) === 1'bx) begin
            $display("FAIL: reset X/Z m_axil_wvalid"); $finish;
        end
        if (m_axil_wvalid !== 1'h0) begin
            $display("FAIL: reset m_axil_wvalid expected %h observed %h", 1'h0, m_axil_wvalid); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^m_axil_bready) === 1'bx) begin
            $display("FAIL: reset X/Z m_axil_bready"); $finish;
        end
        if (m_axil_bready !== 1'h0) begin
            $display("FAIL: reset m_axil_bready expected %h observed %h", 1'h0, m_axil_bready); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^m_axil_araddr) === 1'bx) begin
            $display("FAIL: reset X/Z m_axil_araddr"); $finish;
        end
        if (m_axil_araddr !== 32'h0) begin
            $display("FAIL: reset m_axil_araddr expected %h observed %h", 32'h0, m_axil_araddr); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^m_axil_arvalid) === 1'bx) begin
            $display("FAIL: reset X/Z m_axil_arvalid"); $finish;
        end
        if (m_axil_arvalid !== 1'h0) begin
            $display("FAIL: reset m_axil_arvalid expected %h observed %h", 1'h0, m_axil_arvalid); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^m_axil_arprot) === 1'bx) begin
            $display("FAIL: reset X/Z m_axil_arprot"); $finish;
        end
        if (m_axil_arprot !== 3'h0) begin
            $display("FAIL: reset m_axil_arprot expected %h observed %h", 3'h0, m_axil_arprot); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if ((^m_axil_rready) === 1'bx) begin
            $display("FAIL: reset X/Z m_axil_rready"); $finish;
        end
        if (m_axil_rready !== 1'h0) begin
            $display("FAIL: reset m_axil_rready expected %h observed %h", 1'h0, m_axil_rready); $finish;
        end
        reset_output_checks=reset_output_checks+1;
        if (!reset_values_logged) begin
            $fdisplay(reset_log_fd, "input_cfg_valid,1,0x%h,0x%h,PASS", 1'h0, input_cfg_valid);
            $fdisplay(reset_log_fd, "input_cfg_desc,256,0x%h,0x%h,PASS", 256'h0, input_cfg_desc);
            $fdisplay(reset_log_fd, "line_cfg_valid,1,0x%h,0x%h,PASS", 1'h0, line_cfg_valid);
            $fdisplay(reset_log_fd, "line_cfg_desc,256,0x%h,0x%h,PASS", 256'h0, line_cfg_desc);
            $fdisplay(reset_log_fd, "dw_cfg_valid,1,0x%h,0x%h,PASS", 1'h0, dw_cfg_valid);
            $fdisplay(reset_log_fd, "dw_cfg_desc,256,0x%h,0x%h,PASS", 256'h0, dw_cfg_desc);
            $fdisplay(reset_log_fd, "pw_cfg_valid,1,0x%h,0x%h,PASS", 1'h0, pw_cfg_valid);
            $fdisplay(reset_log_fd, "pw_cfg_desc,256,0x%h,0x%h,PASS", 256'h0, pw_cfg_desc);
            $fdisplay(reset_log_fd, "fm_cfg_valid,1,0x%h,0x%h,PASS", 1'h0, fm_cfg_valid);
            $fdisplay(reset_log_fd, "fm_cfg_desc,256,0x%h,0x%h,PASS", 256'h0, fm_cfg_desc);
            $fdisplay(reset_log_fd, "fm_src_addr,32,0x%h,0x%h,PASS", 32'h0, fm_src_addr);
            $fdisplay(reset_log_fd, "fm_dst_addr,32,0x%h,0x%h,PASS", 32'h0, fm_dst_addr);
            $fdisplay(reset_log_fd, "fm_src_bytes,20,0x%h,0x%h,PASS", 20'h0, fm_src_bytes);
            $fdisplay(reset_log_fd, "fm_dst_bytes,20,0x%h,0x%h,PASS", 20'h0, fm_dst_bytes);
            $fdisplay(reset_log_fd, "fm_read_en,1,0x%h,0x%h,PASS", 1'h0, fm_read_en);
            $fdisplay(reset_log_fd, "fm_write_en,1,0x%h,0x%h,PASS", 1'h0, fm_write_en);
            $fdisplay(reset_log_fd, "fm_read_dma_done,1,0x%h,0x%h,PASS", 1'h0, fm_read_dma_done);
            $fdisplay(reset_log_fd, "fm_write_dma_done,1,0x%h,0x%h,PASS", 1'h0, fm_write_dma_done);
            $fdisplay(reset_log_fd, "fm_dma_error,1,0x%h,0x%h,PASS", 1'h0, fm_dma_error);
            $fdisplay(reset_log_fd, "down_cfg_valid,1,0x%h,0x%h,PASS", 1'h0, down_cfg_valid);
            $fdisplay(reset_log_fd, "down_cfg_desc,256,0x%h,0x%h,PASS", 256'h0, down_cfg_desc);
            $fdisplay(reset_log_fd, "arg_cfg_valid,1,0x%h,0x%h,PASS", 1'h0, arg_cfg_valid);
            $fdisplay(reset_log_fd, "arg_cfg_desc,256,0x%h,0x%h,PASS", 256'h0, arg_cfg_desc);
            $fdisplay(reset_log_fd, "coord_cfg_valid,1,0x%h,0x%h,PASS", 1'h0, coord_cfg_valid);
            $fdisplay(reset_log_fd, "coord_cfg_desc,256,0x%h,0x%h,PASS", 256'h0, coord_cfg_desc);
            $fdisplay(reset_log_fd, "coord_m_joint_ready,1,0x%h,0x%h,PASS", 1'h0, coord_m_joint_ready);
            $fdisplay(reset_log_fd, "coord_threshold,8,0x%h,0x%h,PASS", 8'hd2, coord_threshold);
            $fdisplay(reset_log_fd, "swap_load_valid,1,0x%h,0x%h,PASS", 1'h0, swap_load_valid);
            $fdisplay(reset_log_fd, "swap_load_desc,256,0x%h,0x%h,PASS", 256'h0, swap_load_desc);
            $fdisplay(reset_log_fd, "color_frame_start,1,0x%h,0x%h,PASS", 1'h0, color_frame_start);
            $fdisplay(reset_log_fd, "color_red_cfg,24,0x%h,0x%h,PASS", 24'h6464a0, color_red_cfg);
            $fdisplay(reset_log_fd, "color_blue_cfg,24,0x%h,0x%h,PASS", 24'ha06464, color_blue_cfg);
            $fdisplay(reset_log_fd, "color_min_count,18,0x%h,0x%h,PASS", 18'h8, color_min_count);
            $fdisplay(reset_log_fd, "rom_req_valid,1,0x%h,0x%h,PASS", 1'h0, rom_req_valid);
            $fdisplay(reset_log_fd, "rom_req_op,5,0x%h,0x%h,PASS", 5'h0, rom_req_op);
            $fdisplay(reset_log_fd, "rom_rsp_ready,1,0x%h,0x%h,PASS", 1'h0, rom_rsp_ready);
            $fdisplay(reset_log_fd, "busy,1,0x%h,0x%h,PASS", 1'h0, busy);
            $fdisplay(reset_log_fd, "done_pending,1,0x%h,0x%h,PASS", 1'h0, done_pending);
            $fdisplay(reset_log_fd, "error_pending,1,0x%h,0x%h,PASS", 1'h0, error_pending);
            $fdisplay(reset_log_fd, "error_code,32,0x%h,0x%h,PASS", 32'h0, error_code);
            $fdisplay(reset_log_fd, "result_seq,32,0x%h,0x%h,PASS", 32'h0, result_seq);
            $fdisplay(reset_log_fd, "result_frame_id,32,0x%h,0x%h,PASS", 32'h0, result_frame_id);
            $fdisplay(reset_log_fd, "joint_words,544,0x%h,0x%h,PASS", 544'h0, joint_words);
            $fdisplay(reset_log_fd, "joint_flags,17,0x%h,0x%h,PASS", 17'h0, joint_flags);
            $fdisplay(reset_log_fd, "red_word,32,0x%h,0x%h,PASS", 32'h0, red_word);
            $fdisplay(reset_log_fd, "blue_word,32,0x%h,0x%h,PASS", 32'h0, blue_word);
            $fdisplay(reset_log_fd, "cycle_count,32,0x%h,0x%h,PASS", 32'h0, cycle_count);
            $fdisplay(reset_log_fd, "image_read_done,1,0x%h,0x%h,PASS", 1'h0, image_read_done);
            $fdisplay(reset_log_fd, "m_axil_awaddr,32,0x%h,0x%h,PASS", 32'h0, m_axil_awaddr);
            $fdisplay(reset_log_fd, "m_axil_awvalid,1,0x%h,0x%h,PASS", 1'h0, m_axil_awvalid);
            $fdisplay(reset_log_fd, "m_axil_awprot,3,0x%h,0x%h,PASS", 3'h0, m_axil_awprot);
            $fdisplay(reset_log_fd, "m_axil_wdata,32,0x%h,0x%h,PASS", 32'h0, m_axil_wdata);
            $fdisplay(reset_log_fd, "m_axil_wstrb,4,0x%h,0x%h,PASS", 4'h0, m_axil_wstrb);
            $fdisplay(reset_log_fd, "m_axil_wvalid,1,0x%h,0x%h,PASS", 1'h0, m_axil_wvalid);
            $fdisplay(reset_log_fd, "m_axil_bready,1,0x%h,0x%h,PASS", 1'h0, m_axil_bready);
            $fdisplay(reset_log_fd, "m_axil_araddr,32,0x%h,0x%h,PASS", 32'h0, m_axil_araddr);
            $fdisplay(reset_log_fd, "m_axil_arvalid,1,0x%h,0x%h,PASS", 1'h0, m_axil_arvalid);
            $fdisplay(reset_log_fd, "m_axil_arprot,3,0x%h,0x%h,PASS", 3'h0, m_axil_arprot);
            $fdisplay(reset_log_fd, "m_axil_rready,1,0x%h,0x%h,PASS", 1'h0, m_axil_rready);
            reset_values_logged=1;
        end
    end
end

always @(posedge clk) begin
    if (rst_n) begin
        if (fm_write_en && !fm_cfg_valid) fail("FM write outside cfg");
        if (m_axil_wstrb !== (m_axil_wvalid ? 4'hf : 4'h0))
            fail("AXI WSTRB outside W phase");
    end
end

function [255:0] make_desc;
    input [4:0] op;
    reg [255:0] d;
    reg [3:0] sid;
    begin
        d=0;
        if (op==0) sid=0;
        else if (op==27) sid=14;
        else if (op==28) sid=15;
        else sid=(op+1)/2;
        d[4:0]=op;
        d[148:145]=sid;
        d[106:75]={27'd0,op} << 12;
        d[144:125]=20'd128;
        if (op==0) begin
            d[6:5]=0;
            d[17:9]=256; d[26:18]=256; d[53:45]=3;
            d[35:27]=128; d[44:36]=128; d[62:54]=24;
        end else if (op>=27) begin
            d[6:5]=2;
            d[17:9]=16; d[26:18]=16; d[53:45]=384;
            d[35:27]=16; d[44:36]=16;
            d[62:54]=(op==27) ? 17 : 34;
            d[8:7]=(op==27) ? 1 : 2;
        end else if (op[0]) begin
            d[6:5]=1;
            d[17:9]=32; d[26:18]=32; d[53:45]=24;
            d[35:27]=32; d[44:36]=32; d[62:54]=24;
        end else begin
            d[6:5]=2;
            d[17:9]=32; d[26:18]=32; d[53:45]=24;
            d[35:27]=16; d[44:36]=16; d[62:54]=48;
        end
        make_desc=d;
    end
endfunction

task reset_core;
    begin
        rst_n=0;
        repeat(5) @(negedge clk);
        rst_n=1;
        @(negedge clk);
        if (dut.state!==0 || busy!==0 || done_pending!==0 ||
            error_pending!==0 || result_seq!==0 || error_code!==0 ||
            dut.status_pending!==0 || dut.cfg_seen!==0 ||
            dut.fault_lock!==0)
            fail("reset release control state");
        if (fm_write_en!==0 || m_axil_wstrb!==0 ||
            coord_threshold!==8'hd2 || color_red_cfg!==24'h6464a0 ||
            color_blue_cfg!==24'ha06464 || color_min_count!==18'd8)
            fail("reset release public defaults");
    end
endtask

task start_frame;
    begin
        @(negedge clk);
        start=1;
        @(negedge clk);
        start=0;
        if (!busy) fail("START did not set busy");
    end
endtask

// Deliberately skew AW and W readiness and delay the responses.
always @(posedge clk) begin
    if (!rst_n) begin
        sim_cycle<=0;
        rom_count<=0; load_count<=0; stage_count<=0;
        aw_first_count<=0; w_first_count<=0;
        run_age<=0; rom_delay<=0; swap_delay<=0;
        weight_hold<=0; feature_read_hold<=0;
        feature_write_hold<=0; image_hold<=0;
        rom_pending<=0; swap_pending<=0;
        rom_req_ready<=0; rom_rsp_valid<=0;
        swap_load_ready<=0; swap_load_done<=0;
        aw_seen<=0; w_seen<=0; aw_addr<=0; w_value<=0;
        write_mode<=0;
        m_axil_awready<=0; m_axil_wready<=0;
        m_axil_bvalid<=0; m_axil_bresp<=0;
        m_axil_arready<=0; m_axil_rvalid<=0;
        m_axil_rresp<=0; m_axil_rdata<=0;
        fm_cfg_ready<=0; pw_cfg_ready<=0;
        prev_rom_stall<=0; prev_load_stall<=0;
        prev_fm_stall<=0; prev_pw_stall<=0;
        dest_armed<=0;
        prior_stage<=0; prior_state<=0;
        down_done<=0; input_done<=0; line_done<=0;
        dw_done<=0; pw_done<=0; fm_done<=0;
        arg_done<=0; coord_done<=0;
        color_results_valid<=0;
        coord_m_joint_valid<=0; coord_m_joint_last<=0;
        coord_m_joint_index<=0; coord_m_joint_data<=0;
        coord_m_joint_good<=0;
        fm_read_done<=0; fm_write_done<=0;
    end else begin
        sim_cycle<=sim_cycle+1;
        if (sim_cycle>20000) fail("deadlock watchdog");
        m_axil_awready<=aw_seen || w_seen || !write_mode;
        m_axil_wready<=aw_seen || w_seen || write_mode;
        m_axil_arready<=(sim_cycle%3)!=1;
        rom_req_ready<=(sim_cycle%3)==0;
        swap_load_ready<=(sim_cycle%4)==0;
        fm_cfg_ready<=(sim_cycle%4)==0;
        pw_cfg_ready<=(sim_cycle%5)==0;
        if (prev_rom_stall &&
            (!rom_req_valid || rom_req_op!==prev_rom_op))
            fail("ROM request changed while stalled");
        if (prev_load_stall &&
            (!swap_load_valid || swap_load_desc!==prev_load_desc))
            fail("load request changed while stalled");
        if (prev_fm_stall &&
            (!fm_cfg_valid || fm_cfg_desc!==prev_fm_desc))
            fail("FM cfg changed while stalled");
        if (prev_pw_stall &&
            (!pw_cfg_valid || pw_cfg_desc!==prev_pw_desc))
            fail("PW cfg changed while stalled");
        prev_rom_stall<=rom_req_valid && !rom_req_ready;
        prev_load_stall<=swap_load_valid && !swap_load_ready;
        prev_fm_stall<=fm_cfg_valid && !fm_cfg_ready;
        prev_pw_stall<=pw_cfg_valid && !pw_cfg_ready;
        prev_rom_op<=rom_req_op;
        prev_load_desc<=swap_load_desc;
        prev_fm_desc<=fm_cfg_desc;
        prev_pw_desc<=pw_cfg_desc;
        if (weight_hold>0) weight_hold<=weight_hold-1;
        if (feature_read_hold>0) feature_read_hold<=feature_read_hold-1;
        if (feature_write_hold>0) feature_write_hold<=feature_write_hold-1;
        if (image_hold>0) image_hold<=image_hold-1;

        if (m_axil_awvalid && m_axil_awready) begin
            if (aw_seen) fail("duplicate AW");
            if (!w_seen && !(m_axil_wvalid && m_axil_wready))
                aw_first_count<=aw_first_count+1;
            aw_seen<=1;
            aw_addr<=m_axil_awaddr;
        end
        if (m_axil_wvalid && m_axil_wready) begin
            if (w_seen) fail("duplicate W");
            if (!aw_seen && !(m_axil_awvalid && m_axil_awready))
                w_first_count<=w_first_count+1;
            w_seen<=1;
            w_value<=m_axil_wdata;
        end
        if (aw_seen && w_seen && !m_axil_bvalid) begin
            m_axil_bvalid<=1;
            m_axil_bresp<=inject_b_error ? 2'b10 : 2'b00;
            inject_b_error<=0;
            if (aw_addr==32'h40420058) begin
                dest_armed<=1;
                feature_write_hold<=14;
            end
            if (aw_addr==32'h40420028) begin
                if (dut.stage<14 && !dest_armed)
                    fail("MM2S LENGTH before S2MM arm");
                feature_read_hold<=14;
            end
            if (aw_addr==32'h40400010) begin
                if (!dest_armed) fail("IMAGE source before S2MM arm");
                image_hold<=14;
            end
        end
        if (m_axil_bvalid && m_axil_bready) begin
            m_axil_bvalid<=0;
            aw_seen<=0; w_seen<=0;
            write_mode<=!write_mode;
        end
        if (m_axil_arvalid && m_axil_arready) begin
            if (m_axil_rvalid) fail("overlapping AXI read");
            m_axil_rvalid<=1;
            m_axil_rresp<=inject_r_error ? 2'b10 : 2'b00;
            inject_r_error<=0;
            if (m_axil_araddr==32'h40400004)
                m_axil_rdata<=(image_hold==0) ? 32'h3 : 32'h0;
            else if (m_axil_araddr==32'h40410004)
                m_axil_rdata<=(weight_hold==0) ? 32'h2 : 32'h0;
            else if (m_axil_araddr==32'h40420004)
                m_axil_rdata<=(feature_read_hold==0) ? 32'h2 : 32'h0;
            else if (m_axil_araddr==32'h40420034)
                m_axil_rdata<=(feature_write_hold==0) ? 32'h2 : 32'h0;
            else m_axil_rdata<=0;
            if (inject_status_error) begin
                m_axil_rdata<=32'h12;
                inject_status_error<=0;
            end
        end
        if (m_axil_rvalid && m_axil_rready) m_axil_rvalid<=0;

        if (rom_req_valid && rom_req_ready) begin
            if (rom_pending || rom_rsp_valid)
                fail("more than one ROM outstanding");
            if (rom_req_op !== rom_count[4:0])
                fail("ROM operation sequence");
            rom_count<=rom_count+1;
            rom_pending<=1;
            rom_delay<=1;
            rom_rsp_desc<=make_desc(rom_req_op);
        end
        if (rom_pending) begin
            if (rom_delay>0) rom_delay<=rom_delay-1;
            else begin
                rom_rsp_valid<=1;
                rom_pending<=0;
            end
        end
        if (rom_rsp_valid && rom_rsp_ready) rom_rsp_valid<=0;
        swap_load_done<=0;
        if (swap_load_valid && swap_load_ready) begin
            if (swap_load_desc[4:0] !== load_count[4:0])
                fail("weight load operation sequence");
            if (swap_pending) fail("overlapping weight loads");
            load_count<=load_count+1;
            swap_pending<=1;
            swap_delay<=(load_count%2) ? 26 : 2;
            weight_hold<=20;
        end
        if (swap_pending) begin
            if (swap_delay>0) swap_delay<=swap_delay-1;
            else begin
                swap_load_done<=1;
                swap_pending<=0;
            end
        end

        if (dut.stage!=prior_stage) begin
            if (dut.stage!==prior_stage+1'b1)
                fail("stage sequence");
            stage_count<=stage_count+1;
            dest_armed<=0;
        end
        prior_stage<=dut.stage;
        prior_state<=dut.state;
        if ((input_cfg_valid || line_cfg_valid || dw_cfg_valid ||
             pw_cfg_valid || down_cfg_valid || arg_cfg_valid ||
             coord_cfg_valid) && dut.stage<14 && !dest_armed)
            fail("producer cfg before S2MM arm");
        if (color_frame_start &&
            (color_red_cfg!==24'h010203 ||
             color_blue_cfg!==24'h040506 ||
             color_min_count!==18'd99))
            fail("color snapshot at frame start");
        if (fm_cfg_valid && fm_cfg_ready) begin
            if (dut.stage==0) begin
                if (fm_src_addr!==0 || fm_dst_addr!==fm_a_base ||
                    fm_dst_bytes!==20'd393216 || fm_read_en ||
                    !fm_write_en) fail("stage0 FM cfg");
            end else if (dut.stage<14) begin
                expected_dw=make_desc(2*dut.stage-1);
                expected_pw=make_desc(2*dut.stage);
                if (fm_cfg_desc[4:0]!==expected_dw[4:0] ||
                    fm_cfg_desc[35:27]!==expected_pw[35:27] ||
                    fm_cfg_desc[44:36]!==expected_pw[44:36] ||
                    fm_cfg_desc[62:54]!==expected_pw[62:54] ||
                    fm_cfg_desc[255:149]!==107'd0 ||
                    fm_src_bytes!==20'd24576 ||
                    fm_dst_bytes!==20'd12288 ||
                    fm_src_addr!==(dut.stage[0] ? fm_a_base : fm_b_base) ||
                    fm_dst_addr!==(dut.stage[0] ? fm_b_base : fm_a_base))
                    fail("body FM descriptor or ownership");
            end else if (fm_src_addr!==fm_b_base || fm_write_en ||
                         !fm_read_en)
                fail("head FM source");
        end
        if (dut.state==11) run_age<=run_age+1;
        else run_age<=0;
        down_done<=0; input_done<=0; line_done<=0;
        dw_done<=0; pw_done<=0; fm_done<=0;
        arg_done<=0; coord_done<=0;
        color_results_valid<=0;
        coord_m_joint_valid<=0;
        coord_m_joint_last<=0;
        fm_read_done<=(dut.state==11 && dut.stage!=0);
        fm_write_done<=(dut.state==11 && dut.stage<14);
        if (dut.state==11 && !pause_completion) begin
            if (dut.stage==0) begin
                if (run_age==2) down_done<=1;
                if (run_age==4) input_done<=1;
                if (run_age==7) fm_done<=1;
                if (run_age==3) begin
                    color_red_word<=32'h12345678;
                    color_blue_word<=32'habcdef01;
                    color_results_valid<=1;
                end
            end else if (dut.stage<14) begin
                if (run_age==2+(dut.stage%2)) line_done<=1;
                if (run_age==4) dw_done<=1;
                if (run_age==6-(dut.stage%2)) pw_done<=1;
                if (run_age==8) fm_done<=1;
            end else if (dut.stage==14) begin
                if (run_age==2) pw_done<=1;
                if (run_age==5) arg_done<=1;
                if (run_age==8) fm_done<=1;
            end else begin
                if (run_age==2) pw_done<=1;
                if (run_age==5) arg_done<=1;
                if (run_age==8) fm_done<=1;
                if (run_age==28) coord_done<=1;
                if (run_age>=9 && run_age<=25) begin
                    coord_m_joint_valid<=1;
                    coord_m_joint_index<=run_age-9;
                    coord_m_joint_data<=32'h10000000+run_age-9;
                    coord_m_joint_good<=1;
                    coord_m_joint_last<=(run_age==25);
                end
            end
        end
    end
end

initial begin
    #1;
    threshold_cfg=8'h55;
    red_thresh_cfg=24'h010203;
    blue_thresh_cfg=24'h040506;
    min_count_cfg=18'd99;
    frame_id=32'h31415926;
    sg_desc_base=32'h20000000;
    wgt_base=32'h21000000;
    fm_a_base=32'h22000000;
    fm_b_base=32'h22100000;
    timeout_cycles=32'd10000;
    input_cfg_ready=1; line_cfg_ready=1; dw_cfg_ready=1;
    down_cfg_ready=1;
    arg_cfg_ready=1; coord_cfg_ready=1;
    pause_completion=0;
    inject_b_error=0; inject_r_error=0; inject_status_error=0;
    $dumpfile("top_level_fsm.vcd");
    $dumpvars(0,tb_top_level_fsm);
    reset_core;
    start_frame;
    @(negedge clk);
    start=1;
    frame_id=32'hffffffff;
    @(negedge clk);
    start=0;
    wait(done_pending || error_pending);
    if (error_pending) fail("normal frame fault");
    if (rom_count!=29 || load_count!=29 || stage_count!=15)
        fail("operation or stage count");
    if (aw_first_count==0 || w_first_count==0)
        fail("AXI AW/W skew coverage");
    if (busy || result_seq!=1 || result_frame_id!=32'h31415926)
        fail("atomic publish status");
    if (joint_flags!==17'h1ffff ||
        joint_words[31:0]!==32'h10000000 ||
        joint_words[543:512]!==32'h10000010 ||
        red_word!==32'h12345678 || blue_word!==32'habcdef01)
        fail("atomic result contents");
    if (!image_read_done) fail("IMAGE halt/release");
    $display("PASS: full 16-stage frame, 29 descriptors/loads, AXI ordering, atomic result");
    clear_done=1;
    @(posedge clk);
    @(negedge clk);
    clear_done=0;
    if (done_pending) fail("clear_done");

    reset_core;
    @(negedge clk);
    clear_done=1;
    dut.state=16;
    @(posedge clk);
    @(negedge clk);
    clear_done=0;
    if (!done_pending) fail("new done versus W1C clear");
    $display("PASS: new done over W1C clear");

    reset_core;
    @(negedge clk);
    clear_error=1;
    input_fault=1;
    @(posedge clk);
    @(negedge clk);
    clear_error=0; input_fault=0;
    if (!error_pending || error_code!==32'h1)
        fail("new error versus W1C clear");
    $display("PASS: new error over W1C clear");

    reset_core;
    inject_b_error=1;
    start_frame;
    wait(error_pending);
    if (error_code!==32'h2 || busy || done_pending)
        fail("BRESP fault priority");
    $display("PASS: BRESP fault lock");

    reset_core;
    inject_r_error=1;
    start_frame;
    wait(error_pending);
    if (error_code!==32'h2) fail("RRESP fault");
    $display("PASS: RRESP fault");

    reset_core;
    inject_status_error=1;
    start_frame;
    wait(error_pending);
    if (error_code!==32'h4) fail("DMA status error");
    $display("PASS: DMA status error");

    reset_core;
    inject_r_error=1;
    inject_status_error=1;
    start_frame;
    wait(error_pending);
    if (error_code!==32'h2) fail("AXI response priority");
    $display("PASS: AXI response over DMA status");

    reset_core;
    @(negedge clk);
    input_fault=1;
    dut.state=16;
    @(posedge clk);
    @(negedge clk);
    input_fault=0;
    if (!error_pending || error_code!==32'h1 ||
        result_seq!==0 || done_pending)
        fail("fault versus publish priority");
    $display("PASS: fault over publish");

    reset_core;
    @(negedge clk);
    dut.state=31;
    @(posedge clk);
    @(negedge clk);
    if (!error_pending || dut.state!==17 || error_code!==32'h1)
        fail("illegal state");
    $display("PASS: illegal state");

    reset_core;
    @(negedge clk);
    dut.state=11;
    dut.snap_timeout=1;
    dut.watchdog_timeout_pending=1;
    datapath_progress=1;
    @(posedge clk);
    @(negedge clk);
    datapath_progress=0;
    if (error_pending) fail("progress versus watchdog terminal");
    $display("PASS: progress over watchdog terminal");

    reset_core;
    @(negedge clk);
    dut.state=11;
    dut.previous_state=11;
    dut.watchdog_timeout_pending=1;
    dut.txn_done=1;
    dut.txn_status=1;
    dut.txn_rdata=32'h10;
    @(posedge clk);
    @(negedge clk);
    if (!error_pending || error_code!==32'h4)
        fail("DMA status over watchdog");
    $display("PASS: DMA status over watchdog");

    reset_core;
    timeout_cycles=80;
    pause_completion=1;
    start_frame;
    wait(error_pending);
    if (error_code!==32'h8 || dut.stage!==0)
        fail("identical DMA polling watchdog timeout");
    $display("PASS: identical DMA polling does not reload watchdog");
    if (reset_sampled_edges<5 ||
        reset_output_checks!==reset_sampled_edges*59)
        fail("reset output coverage count");
    $display("PASS: public reset contract and X/Z: 59 outputs x %0d edges = %0d checks",
             reset_sampled_edges, reset_output_checks);
    $fclose(reset_log_fd);
    $display("PASS: tb_top_level_fsm");
    $finish;
end

endmodule
