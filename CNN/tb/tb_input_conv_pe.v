`timescale 1ns / 1ps
module tb_input_conv_pe;
    reg clk, rst_n;
    reg cfg_valid;
    wire cfg_ready;
    reg [255:0] cfg_desc;
    wire done, fault;
    reg [23:0] s_rgb_data;
    reg s_rgb_valid;
    wire s_rgb_ready;
    reg [63:0] s_rgb_tag;
    wire [63:0] m_body_data;
    wire m_body_valid;
    reg m_body_ready;
    wire [3:0] m_body_mask;
    wire [63:0] m_body_tag;
    wire conv0_req_valid;
    reg conv0_req_ready;
    wire [4:0] conv0_req_addr;
    reg conv0_rsp_valid;
    wire conv0_rsp_ready;
    reg [215:0] conv0_rsp_data;
    reg [63:0] conv0_rsp_params;

    integer errors, input_index, output_beats, expected_req;
    integer expected_row, expected_col, expected_group;
    integer lane, expected_value, cycle_count, input_limit, quick_mode;
    reg [31:0] lfsr, seed_value;
    reg stall_active;
    reg [63:0] stalled_data, stalled_tag;
    reg [3:0] stalled_mask;
    reg expect_done;
    reg final_accepted;
    reg allow_fault;
    reg final_stall_active;
    integer final_stall_cycles;

    input_conv_pe dut (
        .clk(clk), .rst_n(rst_n), .cfg_valid(cfg_valid),
        .cfg_ready(cfg_ready), .cfg_desc(cfg_desc), .done(done), .fault(fault),
        .s_rgb_data(s_rgb_data), .s_rgb_valid(s_rgb_valid),
        .s_rgb_ready(s_rgb_ready), .s_rgb_tag(s_rgb_tag),
        .m_body_data(m_body_data), .m_body_valid(m_body_valid),
        .m_body_ready(m_body_ready), .m_body_mask(m_body_mask),
        .m_body_tag(m_body_tag), .conv0_req_valid(conv0_req_valid),
        .conv0_req_ready(conv0_req_ready), .conv0_req_addr(conv0_req_addr),
        .conv0_rsp_valid(conv0_rsp_valid), .conv0_rsp_ready(conv0_rsp_ready),
        .conv0_rsp_data(conv0_rsp_data), .conv0_rsp_params(conv0_rsp_params)
    );

    always #5 clk = ~clk;

    function [23:0] image_pixel;
        input integer row;
        input integer col;
        reg [7:0] r, g, b;
        begin
            r = (row + col) & 127;
            g = (3*row + col) & 127;
            b = (row + 5*col) & 127;
            if (row == 0 && col == 0) begin
                r = 0; g = 0; b = 0;
            end
            image_pixel = {b,g,r};
        end
    endfunction

    function [215:0] make_weights;
        input [4:0] channel;
        integer k, selected;
        reg [215:0] value;
        begin
            value = 216'd0;
            if (channel <= 5'd20) selected = channel;
            else if (channel == 5'd21) selected = 24;
            else if (channel == 5'd22) selected = 25;
            else selected = 26;
            for (k=0; k<27; k=k+1) begin
                if (k == selected) begin
                    if (channel == 5'd22) value[8*k +: 8] = 8'hff;
                    else value[8*k +: 8] = 8'h01;
                end
            end
            make_weights = value;
        end
    endfunction

    function [63:0] make_params;
        input [4:0] channel;
        reg signed [31:0] bias;
        reg [31:0] multiplier;
        begin
            bias = (channel == 5'd22) ? 32'sd63 : 32'sd0;
            multiplier = (channel == 5'd23) ? 32'd32768 : 32'd65536;
            make_params = {multiplier,bias};
        end
    endfunction

    function integer selected_tap;
        input integer channel;
        begin
            if (channel <= 20) selected_tap = channel;
            else if (channel == 21) selected_tap = 24;
            else if (channel == 22) selected_tap = 25;
            else selected_tap = 26;
        end
    endfunction

    function integer expected_channel;
        input integer oy;
        input integer ox;
        input integer channel;
        integer tap, ky, kx, rgb, iy, ix, sample, weight, bias, mult;
        reg [23:0] pixel;
        reg signed [63:0] accum, product, q, rem;
        begin
            tap = selected_tap(channel);
            ky = tap / 9;
            kx = (tap % 9) / 3;
            rgb = tap % 3;
            iy = 2*oy - 1 + ky;
            ix = 2*ox - 1 + kx;
            if (iy < 0 || iy > 255 || ix < 0 || ix > 255)
                sample = 255;
            else begin
                pixel = image_pixel(iy,ix);
                if (rgb == 0) sample = 2*pixel[7:0];
                else if (rgb == 1) sample = 2*pixel[15:8];
                else sample = 2*pixel[23:16];
            end
            weight = (channel == 22) ? -1 : 1;
            bias = (channel == 22) ? 63 : 0;
            mult = (channel == 23) ? 32768 : 65536;
            accum = sample*weight + 2*bias;
            product = accum*mult;
            q = product >>> 17;
            rem = product - (q <<< 17);
            if (rem > 65536 || (rem == 65536 && q[0])) q = q + 1;
            if (q < 0) expected_channel = 0;
            else if (q > 127) expected_channel = 127;
            else expected_channel = q;
        end
    endfunction

    // Ordered one-entry response model. It can accept and replace a response
    // in one cycle, matching the weight_bram_swap_fsm contract.
    always @(posedge clk) begin
        if (!rst_n) begin
            conv0_rsp_valid <= 1'b0;
            conv0_rsp_data <= 216'd0;
            conv0_rsp_params <= 64'd0;
        end else begin
            if (conv0_rsp_valid && conv0_rsp_ready)
                conv0_rsp_valid <= 1'b0;
            if (conv0_req_valid && conv0_req_ready) begin
                conv0_rsp_valid <= 1'b1;
                conv0_rsp_data <= make_weights(conv0_req_addr);
                conv0_rsp_params <= make_params(conv0_req_addr);
            end
        end
    end

    // Randomized ready patterns and deterministic LFSR.
    always @(posedge clk) begin
        if (!rst_n) begin
            lfsr <= seed_value;
            conv0_req_ready <= 1'b0;
            m_body_ready <= 1'b0;
            final_stall_active <= 1'b0;
            final_stall_cycles <= 0;
        end else begin
            lfsr <= {lfsr[30:0],lfsr[31]^lfsr[21]^lfsr[1]^lfsr[0]};
            conv0_req_ready <= lfsr[0] | lfsr[3];
            if (!final_stall_active && m_body_valid && m_body_ready &&
                m_body_tag[15:8] == 8'd127 && m_body_tag[7:0] == 8'd127 &&
                m_body_tag[26:20] == 7'd4) begin
                final_stall_active <= 1'b1;
                final_stall_cycles <= 0;
                m_body_ready <= 1'b0;
            end else if (final_stall_active) begin
                if (final_stall_cycles < 3) begin
                    final_stall_cycles <= final_stall_cycles + 1;
                    m_body_ready <= 1'b0;
                end else begin
                    final_stall_active <= 1'b0;
                    m_body_ready <= 1'b1;
                end
            end else begin
                m_body_ready <= lfsr[2] | lfsr[5];
            end
        end
    end

    // Protocol and scoreboard checks.
    always @(posedge clk) begin
        if (!rst_n) begin
            output_beats = 0;
            expected_req = 0;
            expected_row = 0;
            expected_col = 0;
            expected_group = 0;
            stall_active = 0;
            expect_done = 0;
            final_accepted = 0;
        end else begin
            if (fault && !allow_fault) begin
                $display("ERROR unexpected fault at cycle %0d",cycle_count);
                errors = errors + 1;
            end
            if (expect_done) begin
                if (!done) begin
                    $display("ERROR done missing after final accept");
                    errors = errors + 1;
                end
                expect_done = 0;
            end else if (done) begin
                $display("ERROR unexpected done");
                errors = errors + 1;
            end

            if (conv0_req_valid && conv0_req_ready) begin
                if (conv0_req_addr !== expected_req[4:0]) begin
                    $display("ERROR request addr exp=%0d got=%0d",expected_req,conv0_req_addr);
                    errors = errors + 1;
                end
                if (expected_req == 23) expected_req = 0;
                else expected_req = expected_req + 1;
            end

            if (stall_active) begin
                if (!m_body_valid || m_body_data !== stalled_data ||
                    m_body_tag !== stalled_tag || m_body_mask !== stalled_mask) begin
                    $display("ERROR output changed under stall row=%0d col=%0d group=%0d",
                             expected_row,expected_col,expected_group);
                    errors = errors + 1;
                end
                if (m_body_valid && m_body_ready) stall_active = 0;
            end else if (m_body_valid && !m_body_ready) begin
                stall_active = 1;
                stalled_data = m_body_data;
                stalled_tag = m_body_tag;
                stalled_mask = m_body_mask;
            end

            if (m_body_valid && m_body_ready) begin
                if (m_body_mask !== 4'b1111 || m_body_tag[63:40] !== 24'd0 ||
                    m_body_tag[37:33] !== 5'd0 || m_body_tag[30:27] !== 4'd0 ||
                    m_body_tag[19:16] !== 4'd0 || m_body_tag[15:8] !== expected_row[7:0] ||
                    m_body_tag[7:0] !== expected_col[7:0] ||
                    m_body_tag[26:20] !== expected_group[6:0] ||
                    m_body_tag[32] !== (expected_group==5) ||
                    m_body_tag[39] !== (expected_group==5) ||
                    m_body_tag[38] !== (expected_row==127 && expected_col==127 && expected_group==5)) begin
                    $display("ERROR tag/mask row=%0d col=%0d group=%0d tag=%h mask=%h",
                             expected_row,expected_col,expected_group,m_body_tag,m_body_mask);
                    errors = errors + 1;
                end
                for (lane=0; lane<4; lane=lane+1) begin
                    expected_value = expected_channel(expected_row,expected_col,
                                                      expected_group*4+lane);
                    if (m_body_data[16*lane +: 8] !== expected_value[7:0] ||
                        m_body_data[16*lane+8 +: 8] !== 8'd0) begin
                        $display("ERROR data y=%0d x=%0d g=%0d lane=%0d exp=%0d got=%h",
                                 expected_row,expected_col,expected_group,lane,
                                 expected_value,m_body_data[16*lane +: 16]);
                        errors = errors + 1;
                    end
                end
                output_beats = output_beats + 1;
                if (expected_group == 5) begin
                    expected_group = 0;
                    if (expected_col == 127) begin
                        expected_col = 0;
                        if (expected_row == 127) begin
                            expect_done = 1;
                            final_accepted = 1;
                        end else expected_row = expected_row + 1;
                    end else expected_col = expected_col + 1;
                end else expected_group = expected_group + 1;
            end
        end
    end

    always @(posedge clk) begin
        cycle_count = cycle_count + 1;
        if (cycle_count > 1500000) begin
            $display("TB_INPUT_CONV_PE_FAIL global timeout inputs=%0d outputs=%0d errors=%0d",
                     input_index,output_beats,errors);
            $finish;
        end
    end

    initial begin
        clk=0; rst_n=0; cfg_valid=0; cfg_desc=0;
        s_rgb_data=0; s_rgb_valid=0; s_rgb_tag=0;
        conv0_req_ready=0; m_body_ready=0;
        conv0_rsp_valid=0; conv0_rsp_data=0; conv0_rsp_params=0;
        errors=0; input_index=0; cycle_count=0; allow_fault=1;
        seed_value=32'h1acebeef;
        if ($value$plusargs("SEED_%d",seed_value)) begin end
        quick_mode = $test$plusargs("QUICK");
        input_limit = quick_mode ? 512 : 65536;
        repeat (5) @(posedge clk);
        @(negedge clk); rst_n=1;

        cfg_desc = 256'd0;
        cfg_desc[4:0]=0; cfg_desc[6:5]=0; cfg_desc[8:7]=0;
        cfg_desc[17:9]=256; cfg_desc[26:18]=256;
        cfg_desc[35:27]=128; cfg_desc[44:36]=128;
        cfg_desc[53:45]=3; cfg_desc[62:54]=24;
        cfg_desc[64:63]=2; cfg_desc[66:65]=1; cfg_desc[68:67]=1;
        // An otherwise valid descriptor with shift=16 must enter sticky fault.
        cfg_desc[74:69]=16;
        cfg_valid=1;
        #1;
        while (!cfg_ready) @(negedge clk);
        @(posedge clk); #1; cfg_valid=0;
        if (!fault || cfg_ready) begin
            $display("ERROR invalid cfg did not enter sticky fault");
            errors = errors + 1;
        end
        cfg_desc[74:69]=17;
        cfg_valid=1;
        repeat (2) @(posedge clk);
        #1;
        if (!fault || cfg_ready) begin
            $display("ERROR fault was not sticky");
            errors = errors + 1;
        end
        cfg_valid=0;

        // Synchronous reset is the only recovery path.
        @(negedge clk); rst_n=0;
        repeat (5) @(posedge clk);
        @(negedge clk); rst_n=1;
        #1;
        allow_fault=0;
        if (fault || !cfg_ready) begin
            $display("ERROR reset did not recover from fault");
            errors = errors + 1;
        end

        cfg_valid=1;
        #1;
        while (!cfg_ready) @(negedge clk);
        @(posedge clk); #1; cfg_valid=0;

        while (input_index < input_limit) begin
            @(negedge clk);
            if (lfsr[4] | lfsr[7]) begin
                s_rgb_valid = 1;
                s_rgb_data = image_pixel(input_index/256,input_index%256);
                s_rgb_tag = {48'd0,(input_index/256), (input_index%256)};
                if (s_rgb_ready) input_index = input_index + 1;
            end else s_rgb_valid = 0;
        end
        @(negedge clk); s_rgb_valid=0;

        if (quick_mode) begin
            while (output_beats < 768 && cycle_count < 100000) @(posedge clk);
            repeat (3) @(posedge clk);
            if (output_beats != 768) begin
                $display("ERROR quick output count exp=768 got=%0d",output_beats);
                errors = errors + 1;
            end
            if (errors == 0)
                $display("TB_INPUT_CONV_PE_QUICK_PASS seed=%0d cycles=%0d inputs=%0d outputs=%0d",
                         seed_value,cycle_count,input_index,output_beats);
            else
                $display("TB_INPUT_CONV_PE_QUICK_FAIL errors=%0d cycles=%0d inputs=%0d outputs=%0d",
                         errors,cycle_count,input_index,output_beats);
            $finish;
        end
        while (!final_accepted && cycle_count < 1500000) @(posedge clk);
        repeat (3) @(posedge clk);
        if (output_beats != 98304) begin
            $display("ERROR output count exp=98304 got=%0d",output_beats);
            errors = errors + 1;
        end
        if (!final_accepted) begin
            $display("ERROR timeout cycle=%0d inputs=%0d outputs=%0d",
                     cycle_count,input_index,output_beats);
            errors = errors + 1;
        end
        if (errors == 0)
            $display("TB_INPUT_CONV_PE_PASS seed=%0d cycles=%0d inputs=%0d outputs=%0d",
                     seed_value,cycle_count,input_index,output_beats);
        else
            $display("TB_INPUT_CONV_PE_FAIL errors=%0d cycles=%0d inputs=%0d outputs=%0d",
                     errors,cycle_count,input_index,output_beats);
        $finish;
    end
endmodule
