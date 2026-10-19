`include "uvm_macros.svh"
import uvm_pkg::*;
module cnn_dma_stream_assertions (

    input logic clk,
    input logic rst_n,
    input logic [3:0] debug_stage,

    input logic [63:0] m_feature_data,
    input logic        m_feature_valid,
    input logic        m_feature_ready,
    input logic        m_feature_last,
    input logic [ 7:0] m_feature_keep

);

    always @* cnn_dma_stream_pkg::observed_stage = debug_stage;
    int unsigned stage0_accepts, stage0_last_accepts;
    // Same pre-edge observation as common mon_cb and SVA sampling. Reading
    // raw READY in the active region races the legacy directed sink driver.
    clocking drain_cb @(posedge clk);
        default input #1step;
        input rst_n,debug_stage,m_feature_valid,m_feature_ready,m_feature_last;
    endclocking
    always @(drain_cb) begin
        if (!drain_cb.rst_n) begin stage0_accepts<=0; stage0_last_accepts<=0; end
        else if (drain_cb.debug_stage==0 && drain_cb.m_feature_valid && drain_cb.m_feature_ready) begin
            stage0_accepts<=stage0_accepts+1;
            if (drain_cb.m_feature_last) stage0_last_accepts<=stage0_last_accepts+1;
        end
    end
    a_stage0_drain_before_transition: assert property (@(posedge clk) disable iff(!rst_n)
        debug_stage==1 && $past(debug_stage)==0 |-> stage0_accepts==49152 && stage0_last_accepts==1)
        else `uvm_error("DMA_ASSERT",$sformatf("Stage advanced before Stage0 drain: accepts=%0d LAST=%0d",$sampled(stage0_accepts),$sampled(stage0_last_accepts)))

    // S03
    // AXI4-Stream Output Backpressure Assertion
    //
    // VALID=1 && READY=0이면 transfer가 일어나지 않는다.
    //
    // 따라서 다음 cycle에도:
    //
    // VALID = 1 유지
    // DATA  = 유지
    // KEEP  = 유지
    // LAST  = 유지
    property p_m_feature_hold_under_stall;

        @(posedge clk)
        disable iff (!rst_n)

        (m_feature_valid && !m_feature_ready)

        |=>

        (
            m_feature_valid             &&
            $stable(
            m_feature_data
        ) && $stable(
            m_feature_keep
        ) && $stable(
            m_feature_last
        ));

    endproperty

    a_m_feature_hold_under_stall :
    assert property (p_m_feature_hold_under_stall)
    else begin

        `uvm_error("DMA_ASSERT", "AXIS output changed while stalled")

    end

    // Assertion Coverage
    //
    // 실제로 backpressure 상황이 발생했는지 확인
    c_m_feature_stall :
    cover property (
            @(posedge clk)
            disable iff (!rst_n)

            m_feature_valid &&
            !m_feature_ready
        );

    // Boundary Stall Coverage
    //
    // 마지막 beat(TLAST)에서 backpressure를 실제로 경험했는지
    c_m_feature_last_stall :
    cover property (
            @(posedge clk)
            disable iff (!rst_n)

            m_feature_valid &&
            !m_feature_ready &&
            m_feature_last
        );

endmodule

// DUT에 Assertion Checker 연결
//
// tb_top.sv 수정 없이 bind 사용
bind cnn_accelerator_top cnn_dma_stream_assertions u_cnn_dma_stream_assertions (
    .clk  (clk),
    .rst_n(rst_n),
    .debug_stage(debug_stage),

    .m_feature_data (m_feature_data),
    .m_feature_valid(m_feature_valid),
    .m_feature_ready(m_feature_ready),
    .m_feature_last (m_feature_last),
    .m_feature_keep (m_feature_keep)
);

module cnn_dma_axil_assertions (
    input logic clk, rst_n,
    input logic m_axil_awvalid, m_axil_awready,
    input logic [31:0] m_axil_awaddr,
    input logic m_axil_wvalid, m_axil_wready,
    input logic [31:0] m_axil_wdata,
    input logic [3:0] m_axil_wstrb,
    input logic m_axil_arvalid, m_axil_arready,
    input logic [31:0] m_axil_araddr,
    input logic m_axil_bvalid, m_axil_bready,
    input logic [1:0] m_axil_bresp,
    input logic m_axil_rvalid, m_axil_rready,
    input logic [31:0] m_axil_rdata,
    input logic [1:0] m_axil_rresp
);
    c_aw_stall: cover property (@(posedge clk) disable iff(!rst_n) m_axil_awvalid && !m_axil_awready);
    c_w_stall: cover property (@(posedge clk) disable iff(!rst_n) m_axil_wvalid && !m_axil_wready);
    c_ar_stall: cover property (@(posedge clk) disable iff(!rst_n) m_axil_arvalid && !m_axil_arready);
    a_aw_hold: assert property (@(posedge clk) disable iff(!rst_n)
        m_axil_awvalid && !m_axil_awready |=> m_axil_awvalid && $stable(m_axil_awaddr))
        else `uvm_error("DMA_ASSERT", "AWVALID/AWADDR changed while stalled")
    a_w_hold: assert property (@(posedge clk) disable iff(!rst_n)
        m_axil_wvalid && !m_axil_wready |=> m_axil_wvalid && $stable({m_axil_wdata,m_axil_wstrb}))
        else `uvm_error("DMA_ASSERT", "WVALID/WDATA/WSTRB changed while stalled")
    a_ar_hold: assert property (@(posedge clk) disable iff(!rst_n)
        m_axil_arvalid && !m_axil_arready |=> m_axil_arvalid && $stable(m_axil_araddr))
        else `uvm_error("DMA_ASSERT", "ARVALID/ARADDR changed while stalled")
    a_b_hold: assert property (@(posedge clk) disable iff(!rst_n)
        m_axil_bvalid && !m_axil_bready |=> m_axil_bvalid && $stable(m_axil_bresp))
        else `uvm_error("DMA_ASSERT", "Slave B response changed while stalled")
    a_r_hold: assert property (@(posedge clk) disable iff(!rst_n)
        m_axil_rvalid && !m_axil_rready |=> m_axil_rvalid && $stable({m_axil_rdata,m_axil_rresp}))
        else `uvm_error("DMA_ASSERT", "Slave R response changed while stalled")
endmodule
bind cnn_accelerator_top cnn_dma_axil_assertions u_cnn_dma_axil_assertions (.*);

// Input stability checks also validate the random stimulus and generic feeder.
module cnn_dma_input_assertions (
    input logic clk,rst_n,
    input logic s_weight_valid,s_weight_ready,s_weight_last,
    input logic [63:0] s_weight_data,
    input logic [7:0] s_weight_keep,
    input logic s_image_valid,s_image_ready,s_image_last,
    input logic [63:0] s_image_data,
    input logic [7:0] s_image_keep,
    input logic s_feature_valid,s_feature_ready,s_feature_last,
    input logic [63:0] s_feature_data,
    input logic [7:0] s_feature_keep
);
    a_weight_hold: assert property (@(posedge clk) disable iff(!rst_n)
        s_weight_valid && !s_weight_ready |=> s_weight_valid && $stable({s_weight_data,s_weight_keep,s_weight_last}))
        else `uvm_error("DMA_ASSERT", "Weight stimulus withdrawn/changed under stall")
    a_image_hold: assert property (@(posedge clk) disable iff(!rst_n)
        s_image_valid && !s_image_ready |=> s_image_valid && $stable({s_image_data,s_image_keep,s_image_last}))
        else `uvm_error("DMA_ASSERT", "Image stimulus withdrawn/changed under stall")
    a_feature_hold: assert property (@(posedge clk) disable iff(!rst_n)
        s_feature_valid && !s_feature_ready |=> s_feature_valid && $stable({s_feature_data,s_feature_keep,s_feature_last}))
        else `uvm_error("DMA_ASSERT", "Feature stimulus withdrawn/changed under stall")
endmodule
bind cnn_accelerator_top cnn_dma_input_assertions u_cnn_dma_input_assertions (.*);
