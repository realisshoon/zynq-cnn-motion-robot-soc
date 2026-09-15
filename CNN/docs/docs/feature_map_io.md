공용 기준: <mention-page url="https://app.notion.com/p/3d9e5946183b81d7be5cfb25ec79657c"/> · 통합/레지스터 계약: <mention-page url="https://app.notion.com/p/3d9e5946183b81d59925ed678e8fb87a"/>
담당: **이영현**. 버전 **CNN-v4.0**. 기존 문서의 상충하는 고정 latency/폭/연결 규칙을 이 페이지로 대체한다. 실제 RTL 작성·합성·실기검증 완료 상태를 뜻하지 않는다.
# 기능
body stage의 입력과 출력을 DDR의 FM_A/FM_B 사이로 이동한다. DDR 접근은 외부 AXI DMA가 담당하며, 본 모듈은 DMA의 64bit AXIS와 PE의 32채널/4채널 스트림 사이에서 pack/unpack을 수행한다.
FM_A=0x11000000, FM_B=0x11100000이며 각각 1MiB를 예약한다. 유효 payload 최대는 786432byte다. stage0은 A에 쓰고, stage1은 A→B, stage2는 B→A로 번갈아 진행한다. stage13 결과 B를 두 head가 각각 읽으며 head 출력은 DDR에 쓰지 않는다. ARM linker 예약 영역 0x10000000..0x1120FFFF를 VDMA와 분리한다.
읽기에서는 MM2S byte를 HWC 순서로 모아 pixel마다 Cin byte에서 끊는다. Cin24는 24byte+8zero, Cin48의 두 번째 beat는 16byte+16zero로 구성한다. 다음 pixel을 tail에 섞지 않는다. row/col/batch와 mask를 붙여 256bit beat를 만들고 body stage는 line_buffer, head stage는 PW로 보낸다. batch_last는 현재 batch 인덱스가 ceil(Cin/32)-1인지 나타내는 1bit flag다.
쓰기에서는 4×16bit body 입력의 lane별 하위 8bit만 꺼낸다. body Cout 24/48/96/192/384의 mask는 모두 F이며 두 group을 64bit DMA 1beat로 합친다. keep=FF, 최종 beat에서 last=1이다. 수치를 다시 재양자화하지 않는다. read/write FIFO는 각각 4KiB, RAMB36 1개씩이다.
<callout icon="🚨" color="red_bg">
	**발견·이력 보존**: 기존 2026-09-15 수정 유지: batch_last는 마지막 batch 인덱스 자체가 아니라 `(현재 batch 인덱스 == ceil(Cin/32)-1)`인 1bit flag다. Cin24의 단일 beat도 마지막 batch로 표시한다.
</callout>
<callout icon="🔄" color="blue_bg">
	**결정**: DMA64↔32채널 256bit 및 4채널 64bit 변환을 맡는다. 전송 폭은 고정이며 tail mask와 TAG batch/group으로 유효 채널을 구분한다.
</callout>
<callout icon="✅" color="green_bg">
	**확정된 문서 기준 유지**: CNN-v4.0의 기술 정의를 유지한 구성 편집이다. 공용 산술·TAG·handshake 기준: <mention-page url="https://app.notion.com/p/3d9e5946183b81d7be5cfb25ec79657c"/>. 구현·합성·실기 검증 완료를 뜻하지 않는다.
</callout>
<callout icon="🚨" color="red_bg">
	**확인 요청·미확정**: FM-01: 본문은 read_done/write_done을 다음 cfg까지 유지하는 완료 flag로 규정하고, 포트표의 generic done은 마지막 출력 accept 후 다음 cycle 1cycle pulse로 규정한다. read/write 활성 조합 및 DMA 완료와 generic done 사이의 관계가 명시되지 않았다. 기존 두 정의를 유지하며 중앙 확인 전 해당 결합 조건을 확정하지 않는다.
</callout>
# 포트표
사용자가 제공한 `CNN_v4_포트명세_모듈별.xlsx`의 열 순서·포트 순서·신호명·방향·비트폭·연결 대상·타이밍·Reset을 유지한다. 의미에는 사용 상황을 덧붙였다.
<table header-row="true">
<tr>
<td>신호명</td>
<td>방향</td>
<td>비트폭</td>
<td>연결 대상</td>
<td>의미</td>
<td>타이밍</td>
<td>Reset</td>
</tr>
<tr>
<td>clk</td>
<td>in</td>
<td>1</td>
<td>cnn_accelerator_top</td>
<td>100MHz 단일 CNN 클럭 모든 상태와 stream handshake는 이 클럭의 상승 에지에서 처리한다.</td>
<td>상승 에지</td>
<td>-</td>
</tr>
<tr>
<td>rst_n</td>
<td>in</td>
<td>1</td>
<td>cnn_accelerator_top</td>
<td>동기 active-low reset; 4cycle 이상 assert, 데이터 RAM 전체 clear 금지 낮은 값이 상승 에지에서 샘플되면 제어·유효 상태를 초기화한다.</td>
<td>clk 동기</td>
<td>-</td>
</tr>
<tr>
<td>cfg_valid</td>
<td>in</td>
<td>1</td>
<td>top_level_fsm</td>
<td>새 operation 설정 새 설정 payload가 유효함을 표시하고 ready와 함께 수락될 때까지 설정을 유지한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>cfg_ready</td>
<td>out</td>
<td>1</td>
<td>top_level_fsm</td>
<td>idle 일 때만 1 idle 상태에서 설정을 받을 수 있음을 표시하며 valid와 동시에 1일 때만 수락한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>cfg_desc</td>
<td>in</td>
<td>256</td>
<td>top_level_fsm</td>
<td>공용 descriptor; 수락 후 완료까지 고정 대응하는 설정 handshake에서 256bit를 래치하고 해당 작업이 끝날 때까지 유지한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>done</td>
<td>out</td>
<td>1</td>
<td>top_level_fsm</td>
<td>마지막 출력 accept 후 다음 cycle 1cycle pulse 이 완료 이벤트는 수신 제어기가 기억하며 구체적인 mode별 완료 조건은 타이밍 요구사항을 따른다.</td>
<td>마지막 출력 accept 기준</td>
<td>0</td>
</tr>
<tr>
<td>fault</td>
<td>out</td>
<td>1</td>
<td>top_level_fsm</td>
<td>protocol/range/count 위반 sticky; reset 으로 clear 위반을 발견하면 상태를 유지하여 새 작업을 차단하고 reset으로 해제한다.</td>
<td>오류 검출 다음 cycle</td>
<td>0</td>
</tr>
<tr>
<td>s_body_data</td>
<td>in</td>
<td>64</td>
<td>pointwise_conv_pe / input_conv_pe mux</td>
<td>4×INT16 body low8 stored 해당 유효 신호와 같은 cycle에 전달하며 명세의 byte 순서·부호·lane 배치를 보존한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>s_body_valid</td>
<td>in</td>
<td>1</td>
<td>pointwise_conv_pe / input_conv_pe mux</td>
<td>payload 유효; ready 와 무관하게 assert 가능 대응 payload가 유효함을 표시한다. 수신 ready를 기다리는 동안 payload를 바꾸지 않는다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>s_body_ready</td>
<td>out</td>
<td>1</td>
<td>pointwise_conv_pe / input_conv_pe mux</td>
<td>수신 여유 대응 valid와 동시에 1인 상승 에지에서만 payload를 수락한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>s_body_mask</td>
<td>in</td>
<td>4</td>
<td>pointwise_conv_pe / input_conv_pe mux</td>
<td>lane 별 유효. bit0=lane0. 미사용 data=0 현재 beat의 실제 채널만 처리하며 mask가 0인 lane을 누산하거나 저장하지 않는다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>s_body_tag</td>
<td>in</td>
<td>64</td>
<td>pointwise_conv_pe / input_conv_pe mux</td>
<td>공용 TAG64; 데이터와 같은 cycle 에 이동 현재 payload의 좌표·batch/group·종료 정보를 같은 handshake로 전달한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>m_pixel_data</td>
<td>out</td>
<td>256</td>
<td>line_buffer / pointwise mux</td>
<td>32×UINT8 HWC 해당 유효 신호와 같은 cycle에 전달하며 명세의 byte 순서·부호·lane 배치를 보존한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>m_pixel_valid</td>
<td>out</td>
<td>1</td>
<td>line_buffer / pointwise mux</td>
<td>payload 유효; ready 와 무관하게 assert 가능 대응 payload가 유효함을 표시한다. 수신 ready를 기다리는 동안 payload를 바꾸지 않는다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>m_pixel_ready</td>
<td>in</td>
<td>1</td>
<td>line_buffer / pointwise mux</td>
<td>수신 여유 대응 valid와 동시에 1인 상승 에지에서만 payload를 수락한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>m_pixel_mask</td>
<td>out</td>
<td>32</td>
<td>line_buffer / pointwise mux</td>
<td>lane 별 유효. bit0=lane0. 미사용 data=0 현재 beat의 실제 채널만 처리하며 mask가 0인 lane을 누산하거나 저장하지 않는다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>m_pixel_tag</td>
<td>out</td>
<td>64</td>
<td>line_buffer / pointwise mux</td>
<td>공용 TAG64; 데이터와 같은 cycle 에 이동 현재 payload의 좌표·batch/group·종료 정보를 같은 handshake로 전달한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>src_addr</td>
<td>in</td>
<td>32</td>
<td>top_level_fsm</td>
<td>cfg 와함께래치 cfg handshake에서 다른 전송 설정과 함께 래치하여 실행 중 변경하지 않는다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>dst_addr</td>
<td>in</td>
<td>32</td>
<td>top_level_fsm</td>
<td>cfg 와함께래치 cfg handshake에서 다른 전송 설정과 함께 래치하여 실행 중 변경하지 않는다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>src_bytes</td>
<td>in</td>
<td>20</td>
<td>top_level_fsm</td>
<td>cfg 와함께래치 cfg handshake에서 다른 전송 설정과 함께 래치하여 실행 중 변경하지 않는다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>dst_bytes</td>
<td>in</td>
<td>20</td>
<td>top_level_fsm</td>
<td>cfg 와함께래치 cfg handshake에서 다른 전송 설정과 함께 래치하여 실행 중 변경하지 않는다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>read_en</td>
<td>in</td>
<td>1</td>
<td>top_level_fsm</td>
<td>cfg 와함께래치 cfg handshake에서 다른 전송 설정과 함께 래치하여 실행 중 변경하지 않는다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>write_en</td>
<td>in</td>
<td>1</td>
<td>top_level_fsm</td>
<td>cfg 와함께래치 cfg handshake에서 다른 전송 설정과 함께 래치하여 실행 중 변경하지 않는다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>read_dma_done</td>
<td>in</td>
<td>1</td>
<td>top_level_fsm</td>
<td>DMAsticky 완료/error 이 완료 이벤트는 수신 제어기가 기억하며 구체적인 mode별 완료 조건은 타이밍 요구사항을 따른다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>write_dma_done</td>
<td>in</td>
<td>1</td>
<td>top_level_fsm</td>
<td>DMAsticky 완료/error 이 완료 이벤트는 수신 제어기가 기억하며 구체적인 mode별 완료 조건은 타이밍 요구사항을 따른다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>dma_error</td>
<td>in</td>
<td>1</td>
<td>top_level_fsm</td>
<td>DMAsticky 완료/error top의 DMA 제어 결과를 받아 stream 수락 상태와 함께 전송 완료 또는 오류를 판정한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>read_done</td>
<td>out</td>
<td>1</td>
<td>top_level_fsm</td>
<td>마지막 stream accept 및 DMA 완료 최종 32채널 beat 소비와 DMA 읽기 완료를 모두 확인한 후 다음 cfg까지 유지한다.</td>
<td>다음 cfg 까지 hold</td>
<td>0</td>
</tr>
<tr>
<td>write_done</td>
<td>out</td>
<td>1</td>
<td>top_level_fsm</td>
<td>마지막 stream accept 및 DMA 완료 최종 쓰기 byte 수락과 DMA 쓰기 완료를 모두 확인한 후 다음 cfg까지 유지한다.</td>
<td>다음 cfg 까지 hold</td>
<td>0</td>
</tr>
<tr>
<td>s_dma_read_data</td>
<td>in</td>
<td>64</td>
<td>FEATURE DMA</td>
<td>HWC packed bytes 해당 유효 신호와 같은 cycle에 전달하며 명세의 byte 순서·부호·lane 배치를 보존한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>s_dma_read_valid</td>
<td>in</td>
<td>1</td>
<td>FEATURE DMA</td>
<td>payload 유효; ready 와 무관하게 assert 가능 대응 payload가 유효함을 표시한다. 수신 ready를 기다리는 동안 payload를 바꾸지 않는다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>s_dma_read_ready</td>
<td>out</td>
<td>1</td>
<td>FEATURE DMA</td>
<td>수신 여유 대응 valid와 동시에 1인 상승 에지에서만 payload를 수락한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>s_dma_read_last</td>
<td>in</td>
<td>1</td>
<td>FEATURE DMA</td>
<td>packet 마지막 beat 마지막 표시도 valid와 함께 수락해야 하며 stall 동안 유지한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>s_dma_read_keep</td>
<td>in</td>
<td>8</td>
<td>FEATURE DMA</td>
<td>유효바이트,현재 shape 모두 FF 수락한 DMA beat에서 유효한 byte 위치를 검증하며 기대 byte count/last와 함께 확인한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>m_dma_write_data</td>
<td>out</td>
<td>64</td>
<td>FEATURE DMA</td>
<td>HWC packed bytes 해당 유효 신호와 같은 cycle에 전달하며 명세의 byte 순서·부호·lane 배치를 보존한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>m_dma_write_valid</td>
<td>out</td>
<td>1</td>
<td>FEATURE DMA</td>
<td>payload 유효; ready 와 무관하게 assert 가능 대응 payload가 유효함을 표시한다. 수신 ready를 기다리는 동안 payload를 바꾸지 않는다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>m_dma_write_ready</td>
<td>in</td>
<td>1</td>
<td>FEATURE DMA</td>
<td>수신 여유 대응 valid와 동시에 1인 상승 에지에서만 payload를 수락한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>m_dma_write_last</td>
<td>out</td>
<td>1</td>
<td>FEATURE DMA</td>
<td>packet 마지막 beat 마지막 표시도 valid와 함께 수락해야 하며 stall 동안 유지한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>m_dma_write_keep</td>
<td>out</td>
<td>8</td>
<td>FEATURE DMA</td>
<td>유효바이트,현재 shape 모두 FF 수락한 DMA beat에서 유효한 byte 위치를 검증하며 기대 byte count/last와 함께 확인한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
</table>
## cfg_desc256 필드와 이 모듈의 역할
공용 비트 정의는 <mention-page url="https://app.notion.com/p/3d9e5946183b81d7be5cfb25ec79657c"/>를 기준으로 한다. 아래 사용 표시는 기존 동작의 역할 설명이며, 새 포트나 별도 필수 검사 회로를 추가하는 요구가 아니다.
<table header-row="true">
<tr>
<td>필드</td>
<td>bits</td>
<td>의미</td>
<td>이 모듈의 역할</td>
</tr>
<tr>
<td>op_id</td>
<td>4:0</td>
<td>operation 번호 0..28</td>
<td>생성하는 pixel TAG의 operation 식별</td>
</tr>
<tr>
<td>kind</td>
<td>6:5</td>
<td>연산 종류: 0=conv0, 1=DW, 2=PW</td>
<td>현재 연산 입출력 경로 문맥</td>
</tr>
<tr>
<td>mode</td>
<td>8:7</td>
<td>출력 mode: 0=body, 1=heatmap, 2=offset</td>
<td>body 저장 또는 head 입력 경로 문맥</td>
</tr>
<tr>
<td>hin</td>
<td>17:9</td>
<td>입력 tensor 높이</td>
<td>입력 HWC row 진행</td>
</tr>
<tr>
<td>win</td>
<td>26:18</td>
<td>입력 tensor 너비</td>
<td>입력 HWC col 진행</td>
</tr>
<tr>
<td>hout</td>
<td>35:27</td>
<td>출력 tensor 높이</td>
<td>body 출력 row/byte 진행 문맥</td>
</tr>
<tr>
<td>wout</td>
<td>44:36</td>
<td>출력 tensor 너비</td>
<td>body 출력 col/byte 진행 문맥</td>
</tr>
<tr>
<td>cin</td>
<td>53:45</td>
<td>입력 채널 수</td>
<td>pixel별 flush, 32lane batch와 tail mask</td>
</tr>
<tr>
<td>cout</td>
<td>62:54</td>
<td>출력 채널 수</td>
<td>4lane body HWC packing 문맥</td>
</tr>
<tr>
<td>stride</td>
<td>64:63</td>
<td>convolution stride</td>
<td>이 모듈 본문에 직접 사용하는 동작 없음; 소유 모듈에서 처리</td>
</tr>
<tr>
<td>dilation</td>
<td>66:65</td>
<td>convolution dilation</td>
<td>이 모듈 본문에 직접 사용하는 동작 없음; 소유 모듈에서 처리</td>
</tr>
<tr>
<td>pad</td>
<td>68:67</td>
<td>convolution padding</td>
<td>이 모듈 본문에 직접 사용하는 동작 없음; 소유 모듈에서 처리</td>
</tr>
<tr>
<td>shift</td>
<td>74:69</td>
<td>재양자화 right shift</td>
<td>이 모듈 본문에 직접 사용하는 동작 없음; 소유 모듈에서 처리</td>
</tr>
<tr>
<td>weight_offset</td>
<td>106:75</td>
<td>packed weights 시작 byte offset</td>
<td>이 모듈 본문에 직접 사용하는 동작 없음; 소유 모듈에서 처리</td>
</tr>
<tr>
<td>param_offset</td>
<td>124:107</td>
<td>operation payload 내 param 시작 byte offset</td>
<td>이 모듈 본문에 직접 사용하는 동작 없음; 소유 모듈에서 처리</td>
</tr>
<tr>
<td>dma_bytes</td>
<td>144:125</td>
<td>정렬을 포함한 operation DMA byte 수</td>
<td>이 모듈 본문에 직접 사용하는 동작 없음; 소유 모듈에서 처리</td>
</tr>
<tr>
<td>stage_id</td>
<td>148:145</td>
<td>stage 번호 0..15</td>
<td>A/B stage 및 body/head 연결 문맥; 실제 주소/enable은 별도 cfg 입력</td>
</tr>
<tr>
<td>reserved</td>
<td>255:149</td>
<td>항상 0</td>
<td>재정의 금지</td>
</tr>
</table>
# 타이밍 요구사항
cfg 수락 시 src_addr/dst_addr/src_bytes/dst_bytes/read_en/write_en을 함께 래치한다. S2MM은 PE 시작 전에 arm한다. write_done은 최종 output byte의 수락과 DMA S2MM completion을 모두 확인하고, read_done은 최종 32채널 beat 소비와 DMA 읽기 완료를 모두 확인한다. 이 완료 flag는 다음 cfg 전까지 유지한다. read/write backpressure는 독립적으로 처리하고 full인 write FIFO는 PE ready를 낮춘다. 잘못된 keep/last/bytecount 및 DMA error는 sticky fault다. reset/abort 후 partial write를 다음 stage 정상 입력으로 사용하지 않는다. 전체 DMA 전송에 대한 고정 cycle latency는 기존 명세에 없다.
reset은 공용 100MHz 상승 에지 기준 동기 active-low이며 rst_n을 4cycle 이상 assert한다. RAM 전체 clear는 하지 않는다. valid/ready 포트는 stall 동안 payload를 유지하고 accept를 기준으로 진행한다. cfg 포트가 있는 모듈은 idle에서 cfg를 수락한다. 포트가 없는 별도 frame_start/reset/ready를 추가하지 않는다.
## 필수 파형 시나리오
1. cfg와 read/write enable을 래치하고, S2MM arm 이후에만 body 데이터가 시작되는지 본다.
2. Cin24와 Cin48에서 인접 pixel에 서로 다른 패턴을 넣어 tail 0, mask 및 batch_last가 섞이지 않는지 본다.
3. read와 write에 독립적인 긴 backpressure를 걸어 pack/unpack과 FIFO의 byte 순서를 확인한다.
4. 최종 stream accept와 DMA done의 도착 순서를 서로 바꿔 read_done/write_done이 두 조건을 모두 기다리고 다음 cfg까지 유지되는지 본다.
# 체크리스트
### 기능 정확성
- [ ] pixel 경계에서 tail을 0으로 채우고 다음 pixel byte를 섞지 않는다.
- [ ] batch_last는 마지막 인덱스와의 비교 결과 1bit로 만든다.
- [ ] body low8을 HWC로 저장하며 재양자화를 하지 않는다.
### 인터페이스 프로토콜
- [ ] 64bit keep/last와 256bit mask/TAG를 byte count에 맞춘다.
- [ ] stream 완료와 DMA 완료를 분리하여 두 조건을 모두 기다린다.
### 경계조건
- [ ] Cin24/48 tail, 2group packing 및 마지막 beat를 검사한다.
- [ ] keep/last/count 오류와 partial write 이후 재시작을 시험한다.
### 통합 검증
- [ ] stage13 B를 두 head가 각각 읽고 쓰지 않는지 확인한다.
- [ ] FM/DMA 완료 flag의 유지 규칙과 generic done의 관계는 확인 요청을 해결한 뒤 통합한다.
- [ ] reset·sticky fault·새 작업 차단 및 복구가 공용 계약과 일치한다.
# AI 에이전트용 프롬프트
아래 블록 전체를 복사한다. 공용 규칙·모듈 세부 계약·전체 포트·검증 시나리오를 포함한다.
```plain text
CNN_3D Hand Interaction Platform의 feature_map_io 모듈을 CNN-v4.0 계약에 맞게 구현하라.
담당: 이영현. 대상 Zybo Z7-20 / XC7Z020. 언어는 Verilog-2001 .v만 허용한다.
SystemVerilog의 logic, always_ff, always_comb, typedef, enum, interface, package 등 문법은 금지한다.
아래 내용으로 RTL과 self-checking Verilog-2001 테스트벤치를 작성하라. 다른 Notion 페이지를 읽어야만 알 수 있는 계약을 가정하지 마라.
신호명·방향·폭·필드 위치·산술·DSP/BRAM 숫자를 바꾸거나 포트를 추가하지 마라.
데이터/가중치 및 실제 checkpoint 검증에는 원본 파일이 필요하다. 제공되지 않은 자료나 검증 결과를 만들어내지 마라.

[역할과 설계 배경]
body stage의 입력과 출력을 DDR의 FM_A/FM_B 사이로 이동한다. DDR 접근은 외부 AXI DMA가 담당하며, 본 모듈은 DMA의 64bit AXIS와 PE의 32채널/4채널 스트림 사이에서 pack/unpack을 수행한다.

FM_A=0x11000000, FM_B=0x11100000이며 각각 1MiB를 예약한다. 유효 payload 최대는 786432byte다. stage0은 A에 쓰고, stage1은 A→B, stage2는 B→A로 번갈아 진행한다. stage13 결과 B를 두 head가 각각 읽으며 head 출력은 DDR에 쓰지 않는다. ARM linker 예약 영역 0x10000000..0x1120FFFF를 VDMA와 분리한다.

읽기에서는 MM2S byte를 HWC 순서로 모아 pixel마다 Cin byte에서 끊는다. Cin24는 24byte+8zero, Cin48의 두 번째 beat는 16byte+16zero로 구성한다. 다음 pixel을 tail에 섞지 않는다. row/col/batch와 mask를 붙여 256bit beat를 만들고 body stage는 line_buffer, head stage는 PW로 보낸다. batch_last는 현재 batch 인덱스가 ceil(Cin/32)-1인지 나타내는 1bit flag다.

쓰기에서는 4×16bit body 입력의 lane별 하위 8bit만 꺼낸다. body Cout 24/48/96/192/384의 mask는 모두 F이며 두 group을 64bit DMA 1beat로 합친다. keep=FF, 최종 beat에서 last=1이다. 수치를 다시 재양자화하지 않는다. read/write FIFO는 각각 4KiB, RAMB36 1개씩이다.

[공용 파라미터 전체: 현재 cnn_common_params.vh]
`ifndef CNN_V4_PARAMS
`define CNN_V4_PARAMS
`define CNN_W_IN 32
`define CNN_W_OUT 4
`define CNN_ACCUM_WIDTH 24
`define CNN_REQUANT_IN_WIDTH 25
`define CNN_M_WIDTH 18
`define CNN_SHIFT 16
`define CNN_OPS 29
`define CNN_STAGES 16
`define CNN_VERSION 32'h00040000
`endif

[공용 설계 규칙: 필요한 계약 전문]
## 적용 기준 CNN-v4.0
이 페이지와 generated/interface.json, manifest.json 은 같은 버전이다. RTL 은 Verilog-2001(.v), 검증은 Python 정수 reference 를 사용한다. 클럭은 100MHz, reset 은 동기 active-low rst_n 이다. 자원 배정은 합성 전 예산이며 전체 SoC 합성과 timing 결과로 승인한다.
입력은 1280×720 RGB888 을 256×256 으로 전처리한다. body 는 0..127 을 INT8 로 저장하고 weight 는 signed INT8, heatmap 은 signed INT8, offset 은 signed INT16 이다. 출력채널별 M 은 0..131071 이다. bias_accum=RNE(bias_int8*bias_M/65536)을 PC pack 단계에서 계산하며 signed24 범위를 검사한다. body 누산기는 signed24, conv0 의 doubled 누산기는 signed25 다. 재양자화는 signed25 × signed18 → signed43 전체 곱을 보존한 뒤 RNE ties-to-even 을 적용한다. body/heat/offset 의 saturation 범위는 각각 [0,127], [-128,127], [-32768,32767]이다. Verilog part-select 와 concat 의 signedness 는 명시적으로 변환한다.
RNE 구현식은 q=p>>>N, r=p-(q<<N), h=1<<(N-1), result=q+((r>h)||((r==h)&&(q&1)))이다. N=0 이면 p 를 그대로 사용한다. 음수에서도 q 는 floor 방향이고 r 은 0..2^N-1 이다. 곱의 하위 비트를 먼저 버리거나 양수용 반올림 상수를 더하지 않는다.
valid 는 ready 와 무관하게 assert 할 수 있어야 한다. valid && !ready 동안 data/mask/tag/last 를 유지한다. counter 는 accept 또는 MAC issue 에서만 증가한다. cfg 는 idle 에서 수락하고 완료까지 래치한다. done 은 모듈별 최종 transfer 수락 후 1cycle pulse 이며 top 이 기억한다. 오류는 sticky fault 로 남기고 새 작업을 차단한다. PS 가 DMA 를 reset 한 뒤 core SOFT_RESET 한다. 데이터 RAM 전체를 reset 하지 않는다.
TAG64: col[7:0], row[15:8], batch[19:16], group[26:20], tap[30:27], batch_last[31], pixel_last[32], op_id[37:33], frame_end[38], group_last[39], reserved[63:40]=0. row/col 은 생산자가 내보내는 tensor 의 좌표다. line 입력은 입력 좌표, line 출력은 convolution 출력 좌표다. pixel_last 는 현재 pixel 의 마지막 beat, frame_end 는 현재 operation 의 최종 pixel 최종 beat 에만 1 이다. group_last 는 마지막 출력 group 이다. 미사용 tag 필드는 0 이며 lane0 은 최하위 byte/bit 다.
입출력 tensor 는 HWC, row→col→channel 순서다. cfg_desc256 의 [255:149]는 0 이다. mux 선택은 top 이 cfg 전에 고정하고 선택되지 않은 입력의 ready 는 0 이다. conv0 는 dilation=1, pad=1, stride=2 로 고정한다. DW 는 dilation 1/2 와 stride 1/2 를 지원한다. 제공 manifest 의 d1 은 구현 baseline 이며 원본 체크포인트 확인값이 아니다. export_geometry.py 로 실제 모델의 geometry 를 확인해야 한다.

reset rst_n은 4cycle 이상 assert한다. 위 고정 폭의 raw port에서 signed 연산은 내부에서 명시적으로 변환한다.
모듈에 없는 공용 신호를 추가하지 않는다. color 관찰 tap_accept 및 results_valid처럼 ready가 없는 포트에는 임의로 ready를 만들지 않는다.

[cfg_desc256 전체 필드: 비트 배치/의미/이 모듈의 역할]
필드	bits	의미	이 모듈의 역할
op_id	4:0	operation 번호 0..28	생성하는 pixel TAG의 operation 식별
kind	6:5	연산 종류: 0=conv0, 1=DW, 2=PW	현재 연산 입출력 경로 문맥
mode	8:7	출력 mode: 0=body, 1=heatmap, 2=offset	body 저장 또는 head 입력 경로 문맥
hin	17:9	입력 tensor 높이	입력 HWC row 진행
win	26:18	입력 tensor 너비	입력 HWC col 진행
hout	35:27	출력 tensor 높이	body 출력 row/byte 진행 문맥
wout	44:36	출력 tensor 너비	body 출력 col/byte 진행 문맥
cin	53:45	입력 채널 수	pixel별 flush, 32lane batch와 tail mask
cout	62:54	출력 채널 수	4lane body HWC packing 문맥
stride	64:63	convolution stride	이 모듈 본문에 직접 사용하는 동작 없음; 소유 모듈에서 처리
dilation	66:65	convolution dilation	이 모듈 본문에 직접 사용하는 동작 없음; 소유 모듈에서 처리
pad	68:67	convolution padding	이 모듈 본문에 직접 사용하는 동작 없음; 소유 모듈에서 처리
shift	74:69	재양자화 right shift	이 모듈 본문에 직접 사용하는 동작 없음; 소유 모듈에서 처리
weight_offset	106:75	packed weights 시작 byte offset	이 모듈 본문에 직접 사용하는 동작 없음; 소유 모듈에서 처리
param_offset	124:107	operation payload 내 param 시작 byte offset	이 모듈 본문에 직접 사용하는 동작 없음; 소유 모듈에서 처리
dma_bytes	144:125	정렬을 포함한 operation DMA byte 수	이 모듈 본문에 직접 사용하는 동작 없음; 소유 모듈에서 처리
stage_id	148:145	stage 번호 0..15	A/B stage 및 body/head 연결 문맥; 실제 주소/enable은 별도 cfg 입력
reserved	255:149	항상 0	재정의 금지
kind/mode의 값은 위 표의 정의를 사용한다. 미사용 descriptor 필드가 있다고 해당 값을 임의로 재해석하거나 포트를 제거하지 않는다.

[공용 자원 예산: 구조 배정이며 합성/실기 검증값 아님]
항목	DSP	RAMB36	근거
PW MAC+requant	132	52	weight RAM48+param4 포함
DW MAC+requant	33	5	DW weights4+params1
conv0 MAC+requant	28	6	W4+params1+line1
DW line ring	0	12	256bit×1280depth
FIFO/보조	0	8	FM read/write,IMAGE,skid 여유
CNN 설계합계	193	83	합성예측 아닌 구조별배정
CNN hard cap	196	90	초과시구현수정
외부 IP 예산	16	40	카메라/HDMI/VDMA/AXIDMA3/기타
총 물리한계	220	140	XC7Z020;BRAM은RAMB36환산
자원 표의 weight/param RAM은 weight_bram_swap_fsm 소유분을 PE에 귀속하여 표시한 값이므로 합산 때 중복 계상하지 않는다.

[포트 전체: 순서와 기술 정의 유지]
신호명	방향	비트폭	연결 대상	의미	타이밍	Reset
clk	in	1	cnn_accelerator_top	100MHz 단일 CNN 클럭 모든 상태와 stream handshake는 이 클럭의 상승 에지에서 처리한다.	상승 에지	-
rst_n	in	1	cnn_accelerator_top	동기 active-low reset; 4cycle 이상 assert, 데이터 RAM 전체 clear 금지 낮은 값이 상승 에지에서 샘플되면 제어·유효 상태를 초기화한다.	clk 동기	-
cfg_valid	in	1	top_level_fsm	새 operation 설정 새 설정 payload가 유효함을 표시하고 ready와 함께 수락될 때까지 설정을 유지한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
cfg_ready	out	1	top_level_fsm	idle 일 때만 1 idle 상태에서 설정을 받을 수 있음을 표시하며 valid와 동시에 1일 때만 수락한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
cfg_desc	in	256	top_level_fsm	공용 descriptor; 수락 후 완료까지 고정 대응하는 설정 handshake에서 256bit를 래치하고 해당 작업이 끝날 때까지 유지한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
done	out	1	top_level_fsm	마지막 출력 accept 후 다음 cycle 1cycle pulse 이 완료 이벤트는 수신 제어기가 기억하며 구체적인 mode별 완료 조건은 타이밍 요구사항을 따른다.	마지막 출력 accept 기준	0
fault	out	1	top_level_fsm	protocol/range/count 위반 sticky; reset 으로 clear 위반을 발견하면 상태를 유지하여 새 작업을 차단하고 reset으로 해제한다.	오류 검출 다음 cycle	0
s_body_data	in	64	pointwise_conv_pe / input_conv_pe mux	4×INT16 body low8 stored 해당 유효 신호와 같은 cycle에 전달하며 명세의 byte 순서·부호·lane 배치를 보존한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
s_body_valid	in	1	pointwise_conv_pe / input_conv_pe mux	payload 유효; ready 와 무관하게 assert 가능 대응 payload가 유효함을 표시한다. 수신 ready를 기다리는 동안 payload를 바꾸지 않는다.	valid && ready 에서 수락; stall 동안 payload 유지	0
s_body_ready	out	1	pointwise_conv_pe / input_conv_pe mux	수신 여유 대응 valid와 동시에 1인 상승 에지에서만 payload를 수락한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
s_body_mask	in	4	pointwise_conv_pe / input_conv_pe mux	lane 별 유효. bit0=lane0. 미사용 data=0 현재 beat의 실제 채널만 처리하며 mask가 0인 lane을 누산하거나 저장하지 않는다.	valid && ready 에서 수락; stall 동안 payload 유지	0
s_body_tag	in	64	pointwise_conv_pe / input_conv_pe mux	공용 TAG64; 데이터와 같은 cycle 에 이동 현재 payload의 좌표·batch/group·종료 정보를 같은 handshake로 전달한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
m_pixel_data	out	256	line_buffer / pointwise mux	32×UINT8 HWC 해당 유효 신호와 같은 cycle에 전달하며 명세의 byte 순서·부호·lane 배치를 보존한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
m_pixel_valid	out	1	line_buffer / pointwise mux	payload 유효; ready 와 무관하게 assert 가능 대응 payload가 유효함을 표시한다. 수신 ready를 기다리는 동안 payload를 바꾸지 않는다.	valid && ready 에서 수락; stall 동안 payload 유지	0
m_pixel_ready	in	1	line_buffer / pointwise mux	수신 여유 대응 valid와 동시에 1인 상승 에지에서만 payload를 수락한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
m_pixel_mask	out	32	line_buffer / pointwise mux	lane 별 유효. bit0=lane0. 미사용 data=0 현재 beat의 실제 채널만 처리하며 mask가 0인 lane을 누산하거나 저장하지 않는다.	valid && ready 에서 수락; stall 동안 payload 유지	0
m_pixel_tag	out	64	line_buffer / pointwise mux	공용 TAG64; 데이터와 같은 cycle 에 이동 현재 payload의 좌표·batch/group·종료 정보를 같은 handshake로 전달한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
src_addr	in	32	top_level_fsm	cfg 와함께래치 cfg handshake에서 다른 전송 설정과 함께 래치하여 실행 중 변경하지 않는다.	valid && ready 에서 수락; stall 동안 payload 유지	0
dst_addr	in	32	top_level_fsm	cfg 와함께래치 cfg handshake에서 다른 전송 설정과 함께 래치하여 실행 중 변경하지 않는다.	valid && ready 에서 수락; stall 동안 payload 유지	0
src_bytes	in	20	top_level_fsm	cfg 와함께래치 cfg handshake에서 다른 전송 설정과 함께 래치하여 실행 중 변경하지 않는다.	valid && ready 에서 수락; stall 동안 payload 유지	0
dst_bytes	in	20	top_level_fsm	cfg 와함께래치 cfg handshake에서 다른 전송 설정과 함께 래치하여 실행 중 변경하지 않는다.	valid && ready 에서 수락; stall 동안 payload 유지	0
read_en	in	1	top_level_fsm	cfg 와함께래치 cfg handshake에서 다른 전송 설정과 함께 래치하여 실행 중 변경하지 않는다.	valid && ready 에서 수락; stall 동안 payload 유지	0
write_en	in	1	top_level_fsm	cfg 와함께래치 cfg handshake에서 다른 전송 설정과 함께 래치하여 실행 중 변경하지 않는다.	valid && ready 에서 수락; stall 동안 payload 유지	0
read_dma_done	in	1	top_level_fsm	DMAsticky 완료/error 이 완료 이벤트는 수신 제어기가 기억하며 구체적인 mode별 완료 조건은 타이밍 요구사항을 따른다.	valid && ready 에서 수락; stall 동안 payload 유지	0
write_dma_done	in	1	top_level_fsm	DMAsticky 완료/error 이 완료 이벤트는 수신 제어기가 기억하며 구체적인 mode별 완료 조건은 타이밍 요구사항을 따른다.	valid && ready 에서 수락; stall 동안 payload 유지	0
dma_error	in	1	top_level_fsm	DMAsticky 완료/error top의 DMA 제어 결과를 받아 stream 수락 상태와 함께 전송 완료 또는 오류를 판정한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
read_done	out	1	top_level_fsm	마지막 stream accept 및 DMA 완료 최종 32채널 beat 소비와 DMA 읽기 완료를 모두 확인한 후 다음 cfg까지 유지한다.	다음 cfg 까지 hold	0
write_done	out	1	top_level_fsm	마지막 stream accept 및 DMA 완료 최종 쓰기 byte 수락과 DMA 쓰기 완료를 모두 확인한 후 다음 cfg까지 유지한다.	다음 cfg 까지 hold	0
s_dma_read_data	in	64	FEATURE DMA	HWC packed bytes 해당 유효 신호와 같은 cycle에 전달하며 명세의 byte 순서·부호·lane 배치를 보존한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
s_dma_read_valid	in	1	FEATURE DMA	payload 유효; ready 와 무관하게 assert 가능 대응 payload가 유효함을 표시한다. 수신 ready를 기다리는 동안 payload를 바꾸지 않는다.	valid && ready 에서 수락; stall 동안 payload 유지	0
s_dma_read_ready	out	1	FEATURE DMA	수신 여유 대응 valid와 동시에 1인 상승 에지에서만 payload를 수락한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
s_dma_read_last	in	1	FEATURE DMA	packet 마지막 beat 마지막 표시도 valid와 함께 수락해야 하며 stall 동안 유지한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
s_dma_read_keep	in	8	FEATURE DMA	유효바이트,현재 shape 모두 FF 수락한 DMA beat에서 유효한 byte 위치를 검증하며 기대 byte count/last와 함께 확인한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
m_dma_write_data	out	64	FEATURE DMA	HWC packed bytes 해당 유효 신호와 같은 cycle에 전달하며 명세의 byte 순서·부호·lane 배치를 보존한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
m_dma_write_valid	out	1	FEATURE DMA	payload 유효; ready 와 무관하게 assert 가능 대응 payload가 유효함을 표시한다. 수신 ready를 기다리는 동안 payload를 바꾸지 않는다.	valid && ready 에서 수락; stall 동안 payload 유지	0
m_dma_write_ready	in	1	FEATURE DMA	수신 여유 대응 valid와 동시에 1인 상승 에지에서만 payload를 수락한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
m_dma_write_last	out	1	FEATURE DMA	packet 마지막 beat 마지막 표시도 valid와 함께 수락해야 하며 stall 동안 유지한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
m_dma_write_keep	out	8	FEATURE DMA	유효바이트,현재 shape 모두 FF 수락한 DMA beat에서 유효한 byte 위치를 검증하며 기대 byte count/last와 함께 확인한다.	valid && ready 에서 수락; stall 동안 payload 유지	0

[모듈 세부 계약 전문: 기존 CNN-v4.0 기술 조건 보존]
## 역할
DDR FM_A/FM_B 사이의 stage input/output 을 AXI DMA(피처맵용)로 이동한다. custom AXI-Full master 는 없다. DDR master 는 외부 DMA 가 담당한다. 본 모듈은 DMA64bit AXIS 와 PE 4lane/32lane stream 사이에서 pack/unpack 한다.
## 주소와 전송
FM_A=0x11000000, FM_B=0x11100000,각 1MiB reserve. 유효 payload 최대 786432bytes. stage0 conv0 출력은 A. stage1 은 A→B,stage2 B→A 로 번갈아 사용. stage13 결과는 B 이고 두 head 는 B 를 각각읽는다. head 는 DDR output write 하지 않는다. 모든 source/destination 영역은 분리한다. ARM linker 는 0x10000000..0x1120FFFF 를 예약하고 VDMA 영역과 겹치지 않도록 한다.
## 쓰기
입력 s_body_data64(4×16)에서 body 각 lane 의하위 8bit 만 꺼내 HWC 순으로 append. body Cout 는 24/48/96/192/384 라 모든 그룹 mask=F. 2group=64bit AXIS1beat 로 출력,keep=FF. 마지막 beat 에 last=1. accumulator 결과를 다시 변환하지 않는다. 출력 FIFO 는 4KiB(1RAMB36 data64) 이하, full 이면 PW/input_conv 에 ready=0. accepted output bytes 와 DMA S2MM completion 이 모두 충족되어야 write_done. S2MM 은 PE 연산 시작 전에 arm 한다.
## 읽기
MM2S64bit 를 byte 순서로 HWC buffer 에 수집하여 32channel beat 를 생성한다. pixel 마다 Cin 바이트에서 flush. Cin24 면 24byte+8zero, Cin48 이면 32+16zero. 다음 pixel 의 byte 를 tail 에 섞지 않는다. tag 는수락된 출력 row/col/batch 에서 생성, batch_last 는 (현재 batch 인덱스 == ceil(Cin/32)-1) 인 1bit flag다(**2026-09-15 수정** — 이전에는 이걸 인덱스 값으로 서술해 Cin=24처럼 beat가 1개뿐인 경우 batch_last가 0이 되는 오류가 있었음). 4KiB read FIFO 는별도 1RAMB36. head stage 는 이출력을 PW 에,body stage 는 line_buffer 에 라우팅한다. read_done 은최종 32channelbeat 가소비되고 DMA 읽기완료까지 확인한 뒤 발생한다.
## 제어
cfg_valid/ready 에 src_addr,dst_addr,src_bytes,dst_bytes,read_en,write_en 를함께래치한다. top 이 DMA 를제어하며 read_dma_done/write_dma_done/error 를입력한다. 인터페이스완료와 DDR 완료를혼동하지않는다. 완료플래그는 cfg 전까지유지. 잘못된 keep/last/bytecount=stickyfault.
## 시험
24/48channel partial packet,64bit endian,read/writebackpressure 독립,소스 패턴 보존,마지막 beat,두헤드가같은 conv13 tensor 를읽는지 검증. reset/abort 후 partialwrite 를다음 stage 의정상입력으로사용금지.

[레이턴시·reset·stall·종료 요구사항]
cfg 수락 시 src_addr/dst_addr/src_bytes/dst_bytes/read_en/write_en을 함께 래치한다. S2MM은 PE 시작 전에 arm한다. write_done은 최종 output byte의 수락과 DMA S2MM completion을 모두 확인하고, read_done은 최종 32채널 beat 소비와 DMA 읽기 완료를 모두 확인한다. 이 완료 flag는 다음 cfg 전까지 유지한다. read/write backpressure는 독립적으로 처리하고 full인 write FIFO는 PE ready를 낮춘다. 잘못된 keep/last/bytecount 및 DMA error는 sticky fault다. reset/abort 후 partial write를 다음 stage 정상 입력으로 사용하지 않는다. 전체 DMA 전송에 대한 고정 cycle latency는 기존 명세에 없다.

[반드시 그려 검증할 파형 시나리오]
1. cfg와 read/write enable을 래치하고, S2MM arm 이후에만 body 데이터가 시작되는지 본다.
2. Cin24와 Cin48에서 인접 pixel에 서로 다른 패턴을 넣어 tail 0, mask 및 batch_last가 섞이지 않는지 본다.
3. read와 write에 독립적인 긴 backpressure를 걸어 pack/unpack과 FIFO의 byte 순서를 확인한다.
4. 최종 stream accept와 DMA done의 도착 순서를 서로 바꿔 read_done/write_done이 두 조건을 모두 기다리고 다음 cfg까지 유지되는지 본다.

[수락 체크리스트]
- pixel 경계에서 tail을 0으로 채우고 다음 pixel byte를 섞지 않는다.
- batch_last는 마지막 인덱스와의 비교 결과 1bit로 만든다.
- body low8을 HWC로 저장하며 재양자화를 하지 않는다.
- 64bit keep/last와 256bit mask/TAG를 byte count에 맞춘다.
- stream 완료와 DMA 완료를 분리하여 두 조건을 모두 기다린다.
- Cin24/48 tail, 2group packing 및 마지막 beat를 검사한다.
- keep/last/count 오류와 partial write 이후 재시작을 시험한다.
- stage13 B를 두 head가 각각 읽고 쓰지 않는지 확인한다.
- FM/DMA 완료 flag의 유지 규칙과 generic done의 관계는 확인 요청을 해결한 뒤 통합한다.

[기존 명세의 확인 요청: 임의 수정 금지]
FM-01: 본문은 read_done/write_done을 다음 cfg까지 유지하는 완료 flag로 규정하고, 포트표의 generic done은 마지막 출력 accept 후 다음 cycle 1cycle pulse로 규정한다. read/write 활성 조합 및 DMA 완료와 generic done 사이의 관계가 명시되지 않았다. 기존 두 정의를 유지하며 중앙 확인 전 해당 결합 조건을 확정하지 않는다.

[산출물]
1. feature_map_io.v: 위 포트와 고정 파라미터를 사용한 합성 가능한 Verilog-2001 RTL.
2. tb_feature_map_io.v: 위 기능·프로토콜·경계조건·통합 연결을 검사하는 self-checking Verilog-2001 테스트벤치.
3. 위 4개 시나리오 각각의 VCD 파형, 사람이 읽을 수 있는 타이밍 그림, 실행 명령과 가공하지 않은 전체 로그.
4. 검사 결과표: 예상값/실제값/판정, no-stall latency와 stall 복구, 포트 일치 여부. 산술 모듈은 정수 reference와 bit 단위 비교. 비산술 모듈은 주소·byte·태그·순서·완료 조건을 비교.
5. 합성 도구를 사용할 수 있으면 DSP/RAMB36/LUT 및 timing 결과. 도구·원본 모델이 없으면 해당 항목을 미검증으로 표시하고 측정값을 추정으로 대신하지 않는다.
완료 기준은 테스트 통과와 명세 일치다. 아래 템플릿의 TODO를 남겨 둔 상태를 구현 완료라고 보고하지 마라.

[확인 요청 처리]
외부 포트/타이밍 충돌, 원본 실행 대조 오류, 전체 설계에 영향을 주는 모호함, 공용 자원 예산 변경은 기초 설계 AI에 확인 요청한다.
영향 없는 사소한 내부 구현 선택은 담당자가 결정할 수 있다. 중앙 확정 전 문제 부분을 보류하거나 "미확정" 잠정 구현으로 분리한다.
다음 형식으로 기초 설계 AI에게 전달할 확인 요청 보고서를 작성하라.
주장과 확인된 사실을 구분하고, 증거가 없으면 "미확인"으로 표시하라.
정해지지 않은 사양을 확정값으로 구현하거나 다른 페이지를 자동 수정하지 마라.

🚨 이슈 [번호] — feature_map_io: [구체적 제목]
1. 요청 정보
   - 모듈 / 담당 / 기준 버전: feature_map_io / [담당자] / CNN-v4.0
   - 상태: 확인 요청 / 미확정
   - 유형: 인터페이스 충돌 / 원본 대조 오류 / 구현 불가능한 모호함 / 공용 자원 영향
2. 현재 명세
   - 페이지 URL, 섹션, 포트명 또는 필드명:
   - 문제가 되는 원문 문장·표 행 전체:
3. 발견 내용과 재현 근거
   - 실제 실행 명령, 입력 파일·버전, 환경, 재현 순서:
   - 기대 동작과 실제 관찰 결과:
   - 계산식·비트 배치·상수 대조와 원본 코드 위치:
   - 첨부 raw 전체 로그와 함수/모듈 전체 파일명:
4. 영향 범위
   - 영향받는 모듈 페이지 및 연결 포트:
   - 공용 규칙, 통합 계약, 모듈 담당표, 사용자 포트명세 XLSX의 시트/행:
   - Python 모델, packer, interface.json/manifest, ROM, 테스트 영향:
   - DSP/BRAM·레이턴시·대역폭 변화: 기존 / 제안 / 근거 / 미확인 사항
5. 해결 방안 비교
   - 방안 A: 정확한 수정문, 장점, 비용, 호환성
   - 방안 B(있는 경우): 정확한 수정문, 장점, 비용, 호환성
   - 담당자 추천과 이유(중앙 확정 전 제안 상태):
6. 중앙 확인 요청
   - 독립 검증을 요청하는 주장:
   - 선택·확정이 필요한 질문:
   - 제안된 검증 시나리오와 통과 기준:
7. 첨부 목록 및 구현 상태
   - 가공하지 않은 전체 로그, 영향 함수/모듈 전체, 텍스트 제안 문서:
   - 보류한 부분 또는 "미확정"으로 분리한 잠정 구현:
   - 영향을 받지 않아 계속 진행할 수 있는 부분:
8. 중앙 처리 결과(요청자는 비워 둠)
   - 재검증 결과 / 확정안과 이유 / 영향 범위 / 반영 파일·페이지 목록 / 재검증 결과
담당자가 이 보고서와 첨부파일을 중앙 창구에 직접 전달할 수 있게 완성하라.
원본 raw 전체 로그, 문제가 되는 함수/모듈 전체, Markdown 수정안을 모듈·이슈가 드러나는 파일명으로 첨부하라.
중앙은 근거를 독립 검증하고 전체 영향 페이지를 확인한 뒤 확정안과 이유를 남긴다. 확정 시 관련 Notion·사용자 XLSX·공용 규칙·필요한 모델/ROM을 함께 갱신하며, 그 전에는 제안을 승인된 사양으로 간주하지 않는다.

```
# Verilog 템플릿
포트와 고정 파라미터만 선언한 뼈대다. TODO의 실제 구현과 테스트가 필요하다.
```verilog
// CNN-v4.0 | Verilog-2001 skeleton | technical contract unchanged
module feature_map_io (
    input wire clk,
    input wire rst_n,
    input wire cfg_valid,
    output wire cfg_ready,
    input wire [255:0] cfg_desc,
    output wire done,
    output wire fault,
    input wire [63:0] s_body_data,
    input wire s_body_valid,
    output wire s_body_ready,
    input wire [3:0] s_body_mask,
    input wire [63:0] s_body_tag,
    output wire [255:0] m_pixel_data,
    output wire m_pixel_valid,
    input wire m_pixel_ready,
    output wire [31:0] m_pixel_mask,
    output wire [63:0] m_pixel_tag,
    input wire [31:0] src_addr,
    input wire [31:0] dst_addr,
    input wire [19:0] src_bytes,
    input wire [19:0] dst_bytes,
    input wire read_en,
    input wire write_en,
    input wire read_dma_done,
    input wire write_dma_done,
    input wire dma_error,
    output wire read_done,
    output wire write_done,
    input wire [63:0] s_dma_read_data,
    input wire s_dma_read_valid,
    output wire s_dma_read_ready,
    input wire s_dma_read_last,
    input wire [7:0] s_dma_read_keep,
    output wire [63:0] m_dma_write_data,
    output wire m_dma_write_valid,
    input wire m_dma_write_ready,
    output wire m_dma_write_last,
    output wire [7:0] m_dma_write_keep
);

// Fixed common parameters; do not change the external port widths.
localparam integer CNN_W_IN = 32;
localparam integer CNN_W_OUT = 4;
localparam integer CNN_ACCUM_WIDTH = 24;
localparam integer CNN_REQUANT_IN_WIDTH = 25;
localparam integer CNN_M_WIDTH = 18;
localparam integer CNN_SHIFT = 16;
localparam integer CNN_OPS = 29;
localparam integer CNN_STAGES = 16;
localparam [31:0] CNN_VERSION = 32'h00040000;

localparam integer CFG_OP_ID_LSB = 0, CFG_OP_ID_WIDTH = 5;
localparam integer CFG_KIND_LSB = 5, CFG_KIND_WIDTH = 2;
localparam integer CFG_MODE_LSB = 7, CFG_MODE_WIDTH = 2;
localparam integer CFG_HIN_LSB = 9, CFG_HIN_WIDTH = 9;
localparam integer CFG_WIN_LSB = 18, CFG_WIN_WIDTH = 9;
localparam integer CFG_HOUT_LSB = 27, CFG_HOUT_WIDTH = 9;
localparam integer CFG_WOUT_LSB = 36, CFG_WOUT_WIDTH = 9;
localparam integer CFG_CIN_LSB = 45, CFG_CIN_WIDTH = 9;
localparam integer CFG_COUT_LSB = 54, CFG_COUT_WIDTH = 9;
localparam integer CFG_STRIDE_LSB = 63, CFG_STRIDE_WIDTH = 2;
localparam integer CFG_DILATION_LSB = 65, CFG_DILATION_WIDTH = 2;
localparam integer CFG_PAD_LSB = 67, CFG_PAD_WIDTH = 2;
localparam integer CFG_SHIFT_LSB = 69, CFG_SHIFT_WIDTH = 6;
localparam integer CFG_WEIGHT_OFFSET_LSB = 75, CFG_WEIGHT_OFFSET_WIDTH = 32;
localparam integer CFG_PARAM_OFFSET_LSB = 107, CFG_PARAM_OFFSET_WIDTH = 18;
localparam integer CFG_DMA_BYTES_LSB = 125, CFG_DMA_BYTES_WIDTH = 20;
localparam integer CFG_STAGE_ID_LSB = 145, CFG_STAGE_ID_WIDTH = 4;
localparam integer CFG_RESERVED_LSB = 149, CFG_RESERVED_WIDTH = 107;

// TODO: Use synchronous active-low rst_n; preserve RAM data, reset control/valid state.
// TODO: Latch configuration, implement module-specific FSM and datapath from the prompt.
// TODO: Keep data/mask/tag/last stable while valid && !ready; count accepted work only.
// TODO: Implement the specified rounding, saturation and signedness only where applicable.
// TODO: Implement sticky fault and mode-specific completion; escalate unresolved contracts.
// TODO: Add self-checking Verilog-2001 testbench and the listed waveform scenarios.
// TODO: Meet the existing resource and latency budgets; report synthesis separately.

endmodule
```
# 확인 요청 절차
## 확인 요청이 필요한 상황
- [ ] 포트 폭·타이밍·신호 의미가 상대 모듈의 기대와 맞지 않는 것 같다.
- [ ] `golden_model.py` 또는 다른 원본을 실제 실행·대조하니 계산식·상수·구조가 틀리거나 불완전하다.
- [ ] 이 페이지로 구현할 수 없는 모호함이 다른 모듈이나 전체 설계에 영향을 준다. 담당자가 합리적으로 정할 수 있는 사소한 내부 구현 선택은 제외한다.
- [ ] 변경이 DSP/BRAM 또는 공용 레이턴시·성능 예산에 영향을 준다.
## 확인 요청 프롬프트 템플릿
담당자 AI에게 아래 양식으로 보고서를 만들도록 요청하고, 보고서와 근거 파일을 기초 설계 AI에게 전달한다.
```plain text
다음 형식으로 기초 설계 AI에게 전달할 확인 요청 보고서를 작성하라.
주장과 확인된 사실을 구분하고, 증거가 없으면 "미확인"으로 표시하라.
정해지지 않은 사양을 확정값으로 구현하거나 다른 페이지를 자동 수정하지 마라.

🚨 이슈 [번호] — feature_map_io: [구체적 제목]
1. 요청 정보
   - 모듈 / 담당 / 기준 버전: feature_map_io / [담당자] / CNN-v4.0
   - 상태: 확인 요청 / 미확정
   - 유형: 인터페이스 충돌 / 원본 대조 오류 / 구현 불가능한 모호함 / 공용 자원 영향
2. 현재 명세
   - 페이지 URL, 섹션, 포트명 또는 필드명:
   - 문제가 되는 원문 문장·표 행 전체:
3. 발견 내용과 재현 근거
   - 실제 실행 명령, 입력 파일·버전, 환경, 재현 순서:
   - 기대 동작과 실제 관찰 결과:
   - 계산식·비트 배치·상수 대조와 원본 코드 위치:
   - 첨부 raw 전체 로그와 함수/모듈 전체 파일명:
4. 영향 범위
   - 영향받는 모듈 페이지 및 연결 포트:
   - 공용 규칙, 통합 계약, 모듈 담당표, 사용자 포트명세 XLSX의 시트/행:
   - Python 모델, packer, interface.json/manifest, ROM, 테스트 영향:
   - DSP/BRAM·레이턴시·대역폭 변화: 기존 / 제안 / 근거 / 미확인 사항
5. 해결 방안 비교
   - 방안 A: 정확한 수정문, 장점, 비용, 호환성
   - 방안 B(있는 경우): 정확한 수정문, 장점, 비용, 호환성
   - 담당자 추천과 이유(중앙 확정 전 제안 상태):
6. 중앙 확인 요청
   - 독립 검증을 요청하는 주장:
   - 선택·확정이 필요한 질문:
   - 제안된 검증 시나리오와 통과 기준:
7. 첨부 목록 및 구현 상태
   - 가공하지 않은 전체 로그, 영향 함수/모듈 전체, 텍스트 제안 문서:
   - 보류한 부분 또는 "미확정"으로 분리한 잠정 구현:
   - 영향을 받지 않아 계속 진행할 수 있는 부분:
8. 중앙 처리 결과(요청자는 비워 둠)
   - 재검증 결과 / 확정안과 이유 / 영향 범위 / 반영 파일·페이지 목록 / 재검증 결과
담당자가 이 보고서와 첨부파일을 중앙 창구에 직접 전달할 수 있게 완성하라.
```
## 첨부파일 작성 가이드
- 로그·실행 결과는 가공하지 않은 raw 전체 출력을 첨부한다. 요약이나 일부 발췌를 원본 로그 대신 제출하지 않는다.
- 코드 대조가 필요하면 문제가 되는 함수 또는 모듈 전체를 첨부한다. 일부 스니펫만으로 맥락을 생략하지 않는다.
- 표·계산식·수정 제안은 Markdown 텍스트로 작성한다. 이미지나 스크린샷만으로 제출하지 않는다.
- 파일명에 모듈과 이슈를 명시한다. 예: `feature_map_io_ISSUE01_raw.log`, `feature_map_io_ISSUE01_full_source.py`, `feature_map_io_ISSUE01_proposal.md`.
## 중앙 처리와 구현 상태
중앙은 근거 재검증→전체 영향 확인→방안 확정→관련 페이지·XLSX·공용 규칙 동시 반영을 수행한다. 상세 절차: <mention-page url="https://app.notion.com/p/3d9e5946183b81d7be5cfb25ec79657c"/>.
처리가 끝나기 전 해당 부분은 보류하거나 "미확정"으로 표시한 잠정 구현으로 분리한다. 영향 없는 부분은 계속 진행할 수 있다. 로컬 FSM 인코딩처럼 외부 계약에 영향 없는 선택은 담당자가 결정하고 기록한다.
