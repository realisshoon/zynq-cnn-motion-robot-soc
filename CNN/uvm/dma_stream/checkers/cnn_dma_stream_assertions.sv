module cnn_dma_stream_assertions (

    input logic clk,
    input logic rst_n,

    input logic [63:0] m_feature_data,
    input logic        m_feature_valid,
    input logic        m_feature_ready,
    input logic        m_feature_last,
    input logic [ 7:0] m_feature_keep

);

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

        $error(
            "AXIS protocol violation: m_feature payload changed while VALID=1 READY=0");

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

    .m_feature_data (m_feature_data),
    .m_feature_valid(m_feature_valid),
    .m_feature_ready(m_feature_ready),
    .m_feature_last (m_feature_last),
    .m_feature_keep (m_feature_keep)
);
