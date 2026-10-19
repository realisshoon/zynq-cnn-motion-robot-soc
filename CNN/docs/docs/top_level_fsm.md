공용 기준: <mention-page url="https://app.notion.com/p/3d9e5946183b81d7be5cfb25ec79657c"/> · 통합/레지스터 계약: <mention-page url="https://app.notion.com/p/3d9e5946183b81d59925ed678e8fb87a"/>
담당: **이영현**. 버전 **CNN-v4.0**. 기존 문서의 상충하는 고정 latency/폭/연결 규칙을 이 페이지로 대체한다. 실제 RTL 작성·합성·실기검증 완료 상태를 뜻하지 않는다.
# 기능
PS가 확보한 완성 VDMA frame을 한 번 처리하도록 전체 CNN 파이프라인과 DMA를 지휘한다. PS는 FRAME_BASE/SG_DESC_BASE/FRAME_ID 및 설정을 준비한 뒤 CTRL.START를 쓴다. wrapper의 write 수락이 내부 1cycle start pulse가 된다. busy 중 start/config write는 SLVERR이며 실행 중 snapshot은 변하지 않는다. 10fps의 100ms 간격은 PS가 관리하고 완료 직후 자동 재시작하지 않는다.
stage0은 imageSG→downsample→conv0→FM_A다. stage1..13은 해당 DW/PW descriptor와 가중치를 모두 확보한 뒤 DW→PW를 실행하고 A/B를 교대로 사용한다. stage13 결과 B를 stage14 heatmap과 stage15 offset이 각각 읽는다. heatmap best를 보존한 상태로 offset을 처리한 뒤 17개 joint 결과를 수집한다.
각 모듈 done과 FM read/write 완료, DMA idle, weight load 완료를 기억하여 일찍 도착한 pulse를 잃지 않는다. 사용하지 않는 경로의 완료 조건은 미리 1로 처리한다. stage 결과의 DDR 쓰기까지 끝나야 다음 weight load와 FM swap을 진행한다. row/tap/batch/group 계수는 각 datapath 모듈이 소유한다.
내부 DMA sub-FSM은 IMAGE=0x40400000, WEIGHT=0x40410000, FEATURE=0x40420000에 32bit AXI-Lite master로 접근한다. AW와 W는 각각 독립 수락을 확인하고 B 응답을 기다리며, read는 AR 이후 R을 확인한다. register transaction은 한 번에 1개, WSTRB=F, PROT=0이다. DMA error mask는 0x770이며 IOC 하나만으로 완료를 판단하지 않는다. 상세 레지스터 순서는 아래 프롬프트와 레지스터 표의 기존 계약을 유지한다.
오류와 timeout은 sticky 상태로 남기고 새 start를 차단한다. PS가 세 DMA를 reset한 뒤 core SOFT_RESET으로 복구한다. 결과는 17joint, 색상2word, frame_id를 shadow에서 한 번에 commit하고 RESULT_SEQ를 증가시킨다. PS는 SEQ→결과→SEQ를 읽어 일관성을 확인한다. DSP0이며 정상 frame의 DMA register write 소유자는 PL, boot/error 복구 시 소유자는 PS다.
<callout icon="🔄" color="blue_bg">
	**결정**: PS는 완성 frame 확보·100ms pacing·boot/error DMA 복구를 맡고, 정상 frame의 DMA 제어는 PL이 단독 소유한다. stage 완료는 stream과 DMA/DDR 완료를 함께 확인한다.
</callout>
<callout icon="✅" color="green_bg">
	**확정된 문서 기준 유지**: CNN-v4.0의 기술 정의를 유지한 구성 편집이다. 공용 산술·TAG·handshake 기준: <mention-page url="https://app.notion.com/p/3d9e5946183b81d7be5cfb25ec79657c"/>. 구현·합성·실기 검증 완료를 뜻하지 않는다.
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
<td>input_cfg_valid</td>
<td>out</td>
<td>1</td>
<td>input_conv_pe</td>
<td>새 operation 설정 새 설정 payload가 유효함을 표시하고 ready와 함께 수락될 때까지 설정을 유지한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>input_cfg_ready</td>
<td>in</td>
<td>1</td>
<td>input_conv_pe</td>
<td>idle 일 때만 1 idle 상태에서 설정을 받을 수 있음을 표시하며 valid와 동시에 1일 때만 수락한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>input_cfg_desc</td>
<td>out</td>
<td>256</td>
<td>input_conv_pe</td>
<td>공용 descriptor; 수락 후 완료까지 고정 대응하는 설정 handshake에서 256bit를 래치하고 해당 작업이 끝날 때까지 유지한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>input_done</td>
<td>in</td>
<td>1</td>
<td>input_conv_pe</td>
<td>마지막 출력 accept 후 다음 cycle 1cycle pulse 이 완료 이벤트는 수신 제어기가 기억하며 구체적인 mode별 완료 조건은 타이밍 요구사항을 따른다.</td>
<td>마지막 출력 accept 기준</td>
<td>0</td>
</tr>
<tr>
<td>input_fault</td>
<td>in</td>
<td>1</td>
<td>input_conv_pe</td>
<td>protocol/range/count 위반 sticky; reset 으로 clear 위반을 발견하면 상태를 유지하여 새 작업을 차단하고 reset으로 해제한다.</td>
<td>오류 검출 다음 cycle</td>
<td>0</td>
</tr>
<tr>
<td>line_cfg_valid</td>
<td>out</td>
<td>1</td>
<td>line_buffer</td>
<td>새 operation 설정 새 설정 payload가 유효함을 표시하고 ready와 함께 수락될 때까지 설정을 유지한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>line_cfg_ready</td>
<td>in</td>
<td>1</td>
<td>line_buffer</td>
<td>idle 일 때만 1 idle 상태에서 설정을 받을 수 있음을 표시하며 valid와 동시에 1일 때만 수락한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>line_cfg_desc</td>
<td>out</td>
<td>256</td>
<td>line_buffer</td>
<td>공용 descriptor; 수락 후 완료까지 고정 대응하는 설정 handshake에서 256bit를 래치하고 해당 작업이 끝날 때까지 유지한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>line_done</td>
<td>in</td>
<td>1</td>
<td>line_buffer</td>
<td>마지막 출력 accept 후 다음 cycle 1cycle pulse 이 완료 이벤트는 수신 제어기가 기억하며 구체적인 mode별 완료 조건은 타이밍 요구사항을 따른다.</td>
<td>마지막 출력 accept 기준</td>
<td>0</td>
</tr>
<tr>
<td>line_fault</td>
<td>in</td>
<td>1</td>
<td>line_buffer</td>
<td>protocol/range/count 위반 sticky; reset 으로 clear 위반을 발견하면 상태를 유지하여 새 작업을 차단하고 reset으로 해제한다.</td>
<td>오류 검출 다음 cycle</td>
<td>0</td>
</tr>
<tr>
<td>dw_cfg_valid</td>
<td>out</td>
<td>1</td>
<td>depthwise_conv_pe</td>
<td>새 operation 설정 새 설정 payload가 유효함을 표시하고 ready와 함께 수락될 때까지 설정을 유지한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>dw_cfg_ready</td>
<td>in</td>
<td>1</td>
<td>depthwise_conv_pe</td>
<td>idle 일 때만 1 idle 상태에서 설정을 받을 수 있음을 표시하며 valid와 동시에 1일 때만 수락한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>dw_cfg_desc</td>
<td>out</td>
<td>256</td>
<td>depthwise_conv_pe</td>
<td>공용 descriptor; 수락 후 완료까지 고정 대응하는 설정 handshake에서 256bit를 래치하고 해당 작업이 끝날 때까지 유지한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>dw_done</td>
<td>in</td>
<td>1</td>
<td>depthwise_conv_pe</td>
<td>마지막 출력 accept 후 다음 cycle 1cycle pulse 이 완료 이벤트는 수신 제어기가 기억하며 구체적인 mode별 완료 조건은 타이밍 요구사항을 따른다.</td>
<td>마지막 출력 accept 기준</td>
<td>0</td>
</tr>
<tr>
<td>dw_fault</td>
<td>in</td>
<td>1</td>
<td>depthwise_conv_pe</td>
<td>protocol/range/count 위반 sticky; reset 으로 clear 위반을 발견하면 상태를 유지하여 새 작업을 차단하고 reset으로 해제한다.</td>
<td>오류 검출 다음 cycle</td>
<td>0</td>
</tr>
<tr>
<td>pw_cfg_valid</td>
<td>out</td>
<td>1</td>
<td>pointwise_conv_pe</td>
<td>새 operation 설정 새 설정 payload가 유효함을 표시하고 ready와 함께 수락될 때까지 설정을 유지한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>pw_cfg_ready</td>
<td>in</td>
<td>1</td>
<td>pointwise_conv_pe</td>
<td>idle 일 때만 1 idle 상태에서 설정을 받을 수 있음을 표시하며 valid와 동시에 1일 때만 수락한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>pw_cfg_desc</td>
<td>out</td>
<td>256</td>
<td>pointwise_conv_pe</td>
<td>공용 descriptor; 수락 후 완료까지 고정 대응하는 설정 handshake에서 256bit를 래치하고 해당 작업이 끝날 때까지 유지한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>pw_done</td>
<td>in</td>
<td>1</td>
<td>pointwise_conv_pe</td>
<td>마지막 출력 accept 후 다음 cycle 1cycle pulse 이 완료 이벤트는 수신 제어기가 기억하며 구체적인 mode별 완료 조건은 타이밍 요구사항을 따른다.</td>
<td>마지막 출력 accept 기준</td>
<td>0</td>
</tr>
<tr>
<td>pw_fault</td>
<td>in</td>
<td>1</td>
<td>pointwise_conv_pe</td>
<td>protocol/range/count 위반 sticky; reset 으로 clear 위반을 발견하면 상태를 유지하여 새 작업을 차단하고 reset으로 해제한다.</td>
<td>오류 검출 다음 cycle</td>
<td>0</td>
</tr>
<tr>
<td>fm_cfg_valid</td>
<td>out</td>
<td>1</td>
<td>feature_map_io</td>
<td>새 operation 설정 새 설정 payload가 유효함을 표시하고 ready와 함께 수락될 때까지 설정을 유지한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>fm_cfg_ready</td>
<td>in</td>
<td>1</td>
<td>feature_map_io</td>
<td>idle 일 때만 1 idle 상태에서 설정을 받을 수 있음을 표시하며 valid와 동시에 1일 때만 수락한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>fm_cfg_desc</td>
<td>out</td>
<td>256</td>
<td>feature_map_io</td>
<td>공용 descriptor; 수락 후 완료까지 고정 대응하는 설정 handshake에서 256bit를 래치하고 해당 작업이 끝날 때까지 유지한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>fm_done</td>
<td>in</td>
<td>1</td>
<td>feature_map_io</td>
<td>마지막 출력 accept 후 다음 cycle 1cycle pulse 이 완료 이벤트는 수신 제어기가 기억하며 구체적인 mode별 완료 조건은 타이밍 요구사항을 따른다.</td>
<td>마지막 출력 accept 기준</td>
<td>0</td>
</tr>
<tr>
<td>fm_fault</td>
<td>in</td>
<td>1</td>
<td>feature_map_io</td>
<td>protocol/range/count 위반 sticky; reset 으로 clear 위반을 발견하면 상태를 유지하여 새 작업을 차단하고 reset으로 해제한다.</td>
<td>오류 검출 다음 cycle</td>
<td>0</td>
</tr>
<tr>
<td>fm_src_addr</td>
<td>out</td>
<td>32</td>
<td>feature_map_io</td>
<td>cfg 와함께래치 cfg handshake에서 다른 전송 설정과 함께 래치하여 실행 중 변경하지 않는다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>fm_dst_addr</td>
<td>out</td>
<td>32</td>
<td>feature_map_io</td>
<td>cfg 와함께래치 cfg handshake에서 다른 전송 설정과 함께 래치하여 실행 중 변경하지 않는다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>fm_src_bytes</td>
<td>out</td>
<td>20</td>
<td>feature_map_io</td>
<td>cfg 와함께래치 cfg handshake에서 다른 전송 설정과 함께 래치하여 실행 중 변경하지 않는다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>fm_dst_bytes</td>
<td>out</td>
<td>20</td>
<td>feature_map_io</td>
<td>cfg 와함께래치 cfg handshake에서 다른 전송 설정과 함께 래치하여 실행 중 변경하지 않는다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>fm_read_en</td>
<td>out</td>
<td>1</td>
<td>feature_map_io</td>
<td>cfg 와함께래치 cfg handshake에서 다른 전송 설정과 함께 래치하여 실행 중 변경하지 않는다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>fm_write_en</td>
<td>out</td>
<td>1</td>
<td>feature_map_io</td>
<td>cfg 와함께래치 cfg handshake에서 다른 전송 설정과 함께 래치하여 실행 중 변경하지 않는다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>fm_read_dma_done</td>
<td>out</td>
<td>1</td>
<td>feature_map_io</td>
<td>DMAsticky 완료/error 이 완료 이벤트는 수신 제어기가 기억하며 구체적인 mode별 완료 조건은 타이밍 요구사항을 따른다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>fm_write_dma_done</td>
<td>out</td>
<td>1</td>
<td>feature_map_io</td>
<td>DMAsticky 완료/error 이 완료 이벤트는 수신 제어기가 기억하며 구체적인 mode별 완료 조건은 타이밍 요구사항을 따른다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>fm_dma_error</td>
<td>out</td>
<td>1</td>
<td>feature_map_io</td>
<td>DMAsticky 완료/error top의 DMA 제어 결과를 받아 stream 수락 상태와 함께 전송 완료 또는 오류를 판정한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>fm_read_done</td>
<td>in</td>
<td>1</td>
<td>feature_map_io</td>
<td>마지막 stream accept 및 DMA 완료 feature_map_io가 최종 stream 수락과 DMA 읽기 완료를 함께 확인한 상태다.</td>
<td>다음 cfg 까지 hold</td>
<td>0</td>
</tr>
<tr>
<td>fm_write_done</td>
<td>in</td>
<td>1</td>
<td>feature_map_io</td>
<td>마지막 stream accept 및 DMA 완료 feature_map_io가 최종 stream 수락과 DMA 쓰기 완료를 함께 확인한 상태다.</td>
<td>다음 cfg 까지 hold</td>
<td>0</td>
</tr>
<tr>
<td>down_cfg_valid</td>
<td>out</td>
<td>1</td>
<td>downsample_module</td>
<td>새 operation 설정 새 설정 payload가 유효함을 표시하고 ready와 함께 수락될 때까지 설정을 유지한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>down_cfg_ready</td>
<td>in</td>
<td>1</td>
<td>downsample_module</td>
<td>idle 일 때만 1 idle 상태에서 설정을 받을 수 있음을 표시하며 valid와 동시에 1일 때만 수락한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>down_cfg_desc</td>
<td>out</td>
<td>256</td>
<td>downsample_module</td>
<td>공용 descriptor; 수락 후 완료까지 고정 대응하는 설정 handshake에서 256bit를 래치하고 해당 작업이 끝날 때까지 유지한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>down_done</td>
<td>in</td>
<td>1</td>
<td>downsample_module</td>
<td>마지막 출력 accept 후 다음 cycle 1cycle pulse 이 완료 이벤트는 수신 제어기가 기억하며 구체적인 mode별 완료 조건은 타이밍 요구사항을 따른다.</td>
<td>마지막 출력 accept 기준</td>
<td>0</td>
</tr>
<tr>
<td>down_fault</td>
<td>in</td>
<td>1</td>
<td>downsample_module</td>
<td>protocol/range/count 위반 sticky; reset 으로 clear 위반을 발견하면 상태를 유지하여 새 작업을 차단하고 reset으로 해제한다.</td>
<td>오류 검출 다음 cycle</td>
<td>0</td>
</tr>
<tr>
<td>arg_cfg_valid</td>
<td>out</td>
<td>1</td>
<td>argmax_threshold</td>
<td>새 operation 설정 새 설정 payload가 유효함을 표시하고 ready와 함께 수락될 때까지 설정을 유지한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>arg_cfg_ready</td>
<td>in</td>
<td>1</td>
<td>argmax_threshold</td>
<td>idle 일 때만 1 idle 상태에서 설정을 받을 수 있음을 표시하며 valid와 동시에 1일 때만 수락한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>arg_cfg_desc</td>
<td>out</td>
<td>256</td>
<td>argmax_threshold</td>
<td>공용 descriptor; 수락 후 완료까지 고정 대응하는 설정 handshake에서 256bit를 래치하고 해당 작업이 끝날 때까지 유지한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>arg_done</td>
<td>in</td>
<td>1</td>
<td>argmax_threshold</td>
<td>마지막 출력 accept 후 다음 cycle 1cycle pulse 이 완료 이벤트는 수신 제어기가 기억하며 구체적인 mode별 완료 조건은 타이밍 요구사항을 따른다.</td>
<td>마지막 출력 accept 기준</td>
<td>0</td>
</tr>
<tr>
<td>arg_fault</td>
<td>in</td>
<td>1</td>
<td>argmax_threshold</td>
<td>protocol/range/count 위반 sticky; reset 으로 clear 위반을 발견하면 상태를 유지하여 새 작업을 차단하고 reset으로 해제한다.</td>
<td>오류 검출 다음 cycle</td>
<td>0</td>
</tr>
<tr>
<td>coord_cfg_valid</td>
<td>out</td>
<td>1</td>
<td>coord_restore</td>
<td>새 operation 설정 새 설정 payload가 유효함을 표시하고 ready와 함께 수락될 때까지 설정을 유지한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>coord_cfg_ready</td>
<td>in</td>
<td>1</td>
<td>coord_restore</td>
<td>idle 일 때만 1 idle 상태에서 설정을 받을 수 있음을 표시하며 valid와 동시에 1일 때만 수락한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>coord_cfg_desc</td>
<td>out</td>
<td>256</td>
<td>coord_restore</td>
<td>공용 descriptor; 수락 후 완료까지 고정 대응하는 설정 handshake에서 256bit를 래치하고 해당 작업이 끝날 때까지 유지한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>coord_done</td>
<td>in</td>
<td>1</td>
<td>coord_restore</td>
<td>마지막 출력 accept 후 다음 cycle 1cycle pulse 이 완료 이벤트는 수신 제어기가 기억하며 구체적인 mode별 완료 조건은 타이밍 요구사항을 따른다.</td>
<td>마지막 출력 accept 기준</td>
<td>0</td>
</tr>
<tr>
<td>coord_fault</td>
<td>in</td>
<td>1</td>
<td>coord_restore</td>
<td>protocol/range/count 위반 sticky; reset 으로 clear 위반을 발견하면 상태를 유지하여 새 작업을 차단하고 reset으로 해제한다.</td>
<td>오류 검출 다음 cycle</td>
<td>0</td>
</tr>
<tr>
<td>coord_m_joint_data</td>
<td>in</td>
<td>32</td>
<td>coord_restore</td>
<td>packed JOINT register 해당 유효 신호와 같은 cycle에 전달하며 명세의 byte 순서·부호·lane 배치를 보존한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>coord_m_joint_valid</td>
<td>in</td>
<td>1</td>
<td>coord_restore</td>
<td>payload 유효; ready 와 무관하게 assert 가능 해당 포트의 원래 pulse/handshake 타이밍에 맞춰 수신 측이 유효 이벤트를 인식한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>coord_m_joint_ready</td>
<td>out</td>
<td>1</td>
<td>coord_restore</td>
<td>수신 여유 대응 valid와 동시에 1인 상승 에지에서만 payload를 수락한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>coord_m_joint_last</td>
<td>in</td>
<td>1</td>
<td>coord_restore</td>
<td>packet 마지막 beat 마지막 표시도 valid와 함께 수락해야 하며 stall 동안 유지한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>coord_m_joint_index</td>
<td>in</td>
<td>5</td>
<td>coord_restore</td>
<td>joint0..16 현재 결과 payload가 어느 관절0..16에 해당하는지 같은 handshake로 전달한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>coord_m_joint_good</td>
<td>in</td>
<td>1</td>
<td>coord_restore</td>
<td>score &amp;&amp; image bounds valid 현재 관절의 score와 원본 영상 bounds를 모두 만족한 결과임을 나타낸다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>coord_threshold</td>
<td>out</td>
<td>8</td>
<td>coord_restore</td>
<td>signed8 snapshot frame 시작에 snapshot한 signed8 임계값을 coord_restore에 전달한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0xD2</td>
</tr>
<tr>
<td>swap_fault</td>
<td>in</td>
<td>1</td>
<td>weight_bram_swap_fsm</td>
<td>protocol/range/count 위반 sticky; reset 으로 clear 위반을 발견하면 상태를 유지하여 새 작업을 차단하고 reset으로 해제한다.</td>
<td>오류 검출 다음 cycle</td>
<td>0</td>
</tr>
<tr>
<td>swap_load_valid</td>
<td>out</td>
<td>1</td>
<td>weight_bram_swap_fsm</td>
<td>단일 operation 로드계약 대응 payload가 유효함을 표시한다. 수신 ready를 기다리는 동안 payload를 바꾸지 않는다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>swap_load_ready</td>
<td>in</td>
<td>1</td>
<td>weight_bram_swap_fsm</td>
<td>단일 operation 로드계약 대응 valid와 동시에 1인 상승 에지에서만 payload를 수락한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>swap_load_desc</td>
<td>out</td>
<td>256</td>
<td>weight_bram_swap_fsm</td>
<td>단일 operation 로드계약</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>swap_load_done</td>
<td>in</td>
<td>1</td>
<td>weight_bram_swap_fsm</td>
<td>단일 operation 로드계약 가중치 모듈의 모든 payload 검사와 최종 BRAM write가 완료된 1cycle pulse를 기억한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>color_fault</td>
<td>in</td>
<td>1</td>
<td>color_marker_detect</td>
<td>protocol/range/count 위반 sticky; reset 으로 clear 위반을 발견하면 상태를 유지하여 새 작업을 차단하고 reset으로 해제한다.</td>
<td>오류 검출 다음 cycle</td>
<td>0</td>
</tr>
<tr>
<td>color_frame_start</td>
<td>out</td>
<td>1</td>
<td>color_marker_detect</td>
<td>새 frame reset accum color_marker_detect의 새 frame 누산 초기화와 설정 snapshot을 시작한다.</td>
<td>frame_start 에서래치</td>
<td>0</td>
</tr>
<tr>
<td>color_red_cfg</td>
<td>out</td>
<td>24</td>
<td>color_marker_detect</td>
<td>Bmax/Gmax/Rmin 현재 frame에 사용할 설정값을 snapshot하여 실행 도중 값이 바뀌지 않게 전달한다.</td>
<td>frame_start 에서래치</td>
<td>0x6464A0</td>
</tr>
<tr>
<td>color_blue_cfg</td>
<td>out</td>
<td>24</td>
<td>color_marker_detect</td>
<td>Bmin/Gmax/Rmax 현재 frame에 사용할 설정값을 snapshot하여 실행 도중 값이 바뀌지 않게 전달한다.</td>
<td>frame_start 에서래치</td>
<td>0xA06464</td>
</tr>
<tr>
<td>color_min_count</td>
<td>out</td>
<td>18</td>
<td>color_marker_detect</td>
<td>최소색상 pixel 수</td>
<td>frame_start 에서래치</td>
<td>8</td>
</tr>
<tr>
<td>color_red_word</td>
<td>in</td>
<td>32</td>
<td>color_marker_detect</td>
<td>found/y/x 해당 색상의 found와 원본 y/x를 지정된 32bit 배치로 전달한다.</td>
<td>완료 후 hold</td>
<td>0</td>
</tr>
<tr>
<td>color_blue_word</td>
<td>in</td>
<td>32</td>
<td>color_marker_detect</td>
<td>found/y/x 해당 색상의 found와 원본 y/x를 지정된 32bit 배치로 전달한다.</td>
<td>완료 후 hold</td>
<td>0</td>
</tr>
<tr>
<td>color_results_valid</td>
<td>in</td>
<td>1</td>
<td>color_marker_detect</td>
<td>전체 divider 완료 pulse 색상 divider 결과 등록 완료 pulse를 받아 같은 frame의 CNN 결과와 함께 publish한다.</td>
<td>완료 후 hold</td>
<td>0</td>
</tr>
<tr>
<td>rom_fault</td>
<td>in</td>
<td>1</td>
<td>layer_param_rom</td>
<td>protocol/range/count 위반 sticky; reset 으로 clear 위반을 발견하면 상태를 유지하여 새 작업을 차단하고 reset으로 해제한다.</td>
<td>오류 검출 다음 cycle</td>
<td>0</td>
</tr>
<tr>
<td>rom_req_valid</td>
<td>out</td>
<td>1</td>
<td>layer_param_rom</td>
<td>descriptor read1cycle 대응 payload가 유효함을 표시한다. 수신 ready를 기다리는 동안 payload를 바꾸지 않는다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>rom_req_ready</td>
<td>in</td>
<td>1</td>
<td>layer_param_rom</td>
<td>descriptor read1cycle 대응 valid와 동시에 1인 상승 에지에서만 payload를 수락한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>rom_req_op</td>
<td>out</td>
<td>5</td>
<td>layer_param_rom</td>
<td>descriptor read1cycle ROM 요청 handshake에서 조회할 operation 번호를 전달한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>rom_rsp_valid</td>
<td>in</td>
<td>1</td>
<td>layer_param_rom</td>
<td>descriptor read1cycle 대응 payload가 유효함을 표시한다. 수신 ready를 기다리는 동안 payload를 바꾸지 않는다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>rom_rsp_ready</td>
<td>out</td>
<td>1</td>
<td>layer_param_rom</td>
<td>descriptor read1cycle 대응 valid와 동시에 1인 상승 에지에서만 payload를 수락한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>rom_rsp_desc</td>
<td>in</td>
<td>256</td>
<td>layer_param_rom</td>
<td>descriptor read1cycle 요청한 operation의 cfg_desc256 응답을 받아 현재 DW/PW 설정에 맞게 보관한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>threshold_cfg</td>
<td>in</td>
<td>8</td>
<td>cnn_accelerator_top</td>
<td>signed8 register snapshot 현재 frame에 사용할 설정값을 snapshot하여 실행 도중 값이 바뀌지 않게 전달한다.</td>
<td>frame snapshot / status hold</td>
<td>0</td>
</tr>
<tr>
<td>red_thresh_cfg</td>
<td>in</td>
<td>24</td>
<td>cnn_accelerator_top</td>
<td>RED_THRESH register snapshot 현재 frame에 사용할 설정값을 snapshot하여 실행 도중 값이 바뀌지 않게 전달한다.</td>
<td>frame snapshot / status hold</td>
<td>0</td>
</tr>
<tr>
<td>blue_thresh_cfg</td>
<td>in</td>
<td>24</td>
<td>cnn_accelerator_top</td>
<td>BLUE_THRESH register snapshot 현재 frame에 사용할 설정값을 snapshot하여 실행 도중 값이 바뀌지 않게 전달한다.</td>
<td>frame snapshot / status hold</td>
<td>0</td>
</tr>
<tr>
<td>min_count_cfg</td>
<td>in</td>
<td>18</td>
<td>cnn_accelerator_top</td>
<td>MIN_COUNT register snapshot 현재 frame에 사용할 설정값을 snapshot하여 실행 도중 값이 바뀌지 않게 전달한다.</td>
<td>frame snapshot / status hold</td>
<td>0</td>
</tr>
<tr>
<td>datapath_progress</td>
<td>in</td>
<td>1</td>
<td>cnn_accelerator_top</td>
<td>wrapper OR of accepted transfers and MAC issues wrapper가 내부 stream accept와 MAC issue를 OR한 값으로, 무진행 watchdog을 갱신한다.</td>
<td>frame snapshot / status hold</td>
<td>0</td>
</tr>
<tr>
<td>start</td>
<td>in</td>
<td>1</td>
<td>cnn_accelerator_top</td>
<td>wrapperCTRLwritepulse wrapper가 CTRL.START write를 수락한 1cycle pulse로 한 frame의 처리를 시작한다.</td>
<td>frame snapshot / status hold</td>
<td>0</td>
</tr>
<tr>
<td>clear_done</td>
<td>in</td>
<td>1</td>
<td>cnn_accelerator_top</td>
<td>CTRLbit1W1P CTRL의 DONE_CLEAR 쓰기로 done pending을 지우며 같은 cycle의 새 event가 우선한다.</td>
<td>frame snapshot / status hold</td>
<td>0</td>
</tr>
<tr>
<td>clear_error</td>
<td>in</td>
<td>1</td>
<td>cnn_accelerator_top</td>
<td>ERRORW1C ERROR W1C 동작을 전달하며 같은 cycle의 새 error event가 우선한다.</td>
<td>frame snapshot / status hold</td>
<td>0</td>
</tr>
<tr>
<td>frame_id</td>
<td>in</td>
<td>32</td>
<td>cnn_accelerator_top</td>
<td>PSassignedsourceid PS가 지정한 source frame 식별자를 snapshot하여 해당 결과와 함께 공개한다.</td>
<td>frame snapshot / status hold</td>
<td>0</td>
</tr>
<tr>
<td>sg_desc_base</td>
<td>in</td>
<td>32</td>
<td>cnn_accelerator_top</td>
<td>144BDbase PS가 준비한 144개 64byte-aligned 유한 SG descriptor chain의 시작 주소다.</td>
<td>frame snapshot / status hold</td>
<td>0</td>
</tr>
<tr>
<td>wgt_base</td>
<td>in</td>
<td>32</td>
<td>cnn_accelerator_top</td>
<td>packedweightsbase generated/weights_v4.bin을 배치한 DDR 기준 주소로 operation offset과 함께 DMA에 사용한다.</td>
<td>frame snapshot / status hold</td>
<td>0</td>
</tr>
<tr>
<td>fm_a_base</td>
<td>in</td>
<td>32</td>
<td>cnn_accelerator_top</td>
<td>FM_A FM_A의 DDR 기준 주소이며 기본 배치는 0x11000000이다.</td>
<td>frame snapshot / status hold</td>
<td>0</td>
</tr>
<tr>
<td>fm_b_base</td>
<td>in</td>
<td>32</td>
<td>cnn_accelerator_top</td>
<td>FM_B FM_B의 DDR 기준 주소이며 기본 배치는 0x11100000이다.</td>
<td>frame snapshot / status hold</td>
<td>0</td>
</tr>
<tr>
<td>timeout_cycles</td>
<td>in</td>
<td>32</td>
<td>cnn_accelerator_top</td>
<td>no-progress timeout 진행 없는 cycle의 상한이며 기본값은 100000000이다.</td>
<td>frame snapshot / status hold</td>
<td>0</td>
</tr>
<tr>
<td>busy</td>
<td>out</td>
<td>1</td>
<td>cnn_accelerator_top</td>
<td>inflightframe 한 frame을 처리하는 동안 활성 상태를 표시하여 중복 START와 설정 변경을 차단한다.</td>
<td>frame snapshot / status hold</td>
<td>0</td>
</tr>
<tr>
<td>done_pending</td>
<td>out</td>
<td>1</td>
<td>cnn_accelerator_top</td>
<td>stickycompletion 해당 이벤트를 sticky로 보관하며 clear와 새 이벤트가 겹치면 새 이벤트를 유지한다.</td>
<td>frame snapshot / status hold</td>
<td>0</td>
</tr>
<tr>
<td>error_pending</td>
<td>out</td>
<td>1</td>
<td>cnn_accelerator_top</td>
<td>stickyerror 해당 이벤트를 sticky로 보관하며 clear와 새 이벤트가 겹치면 새 이벤트를 유지한다.</td>
<td>frame snapshot / status hold</td>
<td>0</td>
</tr>
<tr>
<td>error_code</td>
<td>out</td>
<td>32</td>
<td>cnn_accelerator_top</td>
<td>firstfaultsource 최초 fault의 원인을 보존하여 PS가 복구 원인을 확인할 수 있게 한다.</td>
<td>frame snapshot / status hold</td>
<td>0</td>
</tr>
<tr>
<td>result_seq</td>
<td>out</td>
<td>32</td>
<td>cnn_accelerator_top</td>
<td>publishsequence shadow 결과를 한 번에 publish할 때 증가하며 PS가 결과 일관성을 확인하는 데 사용한다.</td>
<td>frame snapshot / status hold</td>
<td>0</td>
</tr>
<tr>
<td>result_frame_id</td>
<td>out</td>
<td>32</td>
<td>cnn_accelerator_top</td>
<td>publishedframeid 현재 공개된 joint 및 색상 결과에 대응하는 source frame 식별자다.</td>
<td>frame snapshot / status hold</td>
<td>0</td>
</tr>
<tr>
<td>joint_words</td>
<td>out</td>
<td>544</td>
<td>cnn_accelerator_top</td>
<td>17packed32bitresults 관절0..16의 32bit packed 결과를 공개하는 17word 버스다.</td>
<td>frame snapshot / status hold</td>
<td>0</td>
</tr>
<tr>
<td>joint_flags</td>
<td>out</td>
<td>17</td>
<td>cnn_accelerator_top</td>
<td>publishedgoodflags 공개된 관절0..16의 score/bounds 유효 판정을 각각 표시한다.</td>
<td>frame snapshot / status hold</td>
<td>0</td>
</tr>
<tr>
<td>red_word</td>
<td>out</td>
<td>32</td>
<td>cnn_accelerator_top</td>
<td>publishedmarker 해당 색상의 found와 원본 y/x를 지정된 32bit 배치로 전달한다.</td>
<td>frame snapshot / status hold</td>
<td>0</td>
</tr>
<tr>
<td>blue_word</td>
<td>out</td>
<td>32</td>
<td>cnn_accelerator_top</td>
<td>publishedmarker 해당 색상의 found와 원본 y/x를 지정된 32bit 배치로 전달한다.</td>
<td>frame snapshot / status hold</td>
<td>0</td>
</tr>
<tr>
<td>cycle_count</td>
<td>out</td>
<td>32</td>
<td>cnn_accelerator_top</td>
<td>starttopublishcycles START부터 publish까지의 frame 처리 cycle 수를 전달한다.</td>
<td>frame snapshot / status hold</td>
<td>0</td>
</tr>
<tr>
<td>image_read_done</td>
<td>out</td>
<td>1</td>
<td>cnn_accelerator_top</td>
<td>sourcebufferreleaseallowed stage0 및 이미지 SG 종료 후 IMAGE RS=0과 Halted=1까지 확인하여 PS의 원본 frame 해제를 허용한다.</td>
<td>frame snapshot / status hold</td>
<td>0</td>
</tr>
<tr>
<td>m_axil_awaddr</td>
<td>out</td>
<td>32</td>
<td>DMA Interconnect</td>
<td>AXI4-Lite awaddr DMA register write 주소를 AW handshake까지 유지한다.</td>
<td>각 channel 독립 valid/ready;singleoutstanding</td>
<td>0</td>
</tr>
<tr>
<td>m_axil_awvalid</td>
<td>out</td>
<td>1</td>
<td>DMA Interconnect</td>
<td>AXI4-Lite awvalid write 주소의 유효성을 표시하며 AWREADY와 독립적으로 발생한다.</td>
<td>각 channel 독립 valid/ready;singleoutstanding</td>
<td>0</td>
</tr>
<tr>
<td>m_axil_awready</td>
<td>in</td>
<td>1</td>
<td>DMA Interconnect</td>
<td>AXI4-Lite awready 주소 수락을 표시한다. W 채널 수락과 별도로 기억한다.</td>
<td>각 channel 독립 valid/ready;singleoutstanding</td>
<td>0</td>
</tr>
<tr>
<td>m_axil_awprot</td>
<td>out</td>
<td>3</td>
<td>DMA Interconnect</td>
<td>AXI4-Lite awprot 고정값 0으로 DMA register 접근 속성을 전달한다.</td>
<td>각 channel 독립 valid/ready;singleoutstanding</td>
<td>0</td>
</tr>
<tr>
<td>m_axil_wdata</td>
<td>out</td>
<td>32</td>
<td>DMA Interconnect</td>
<td>AXI4-Lite wdata DMA register write 값을 W handshake까지 유지한다.</td>
<td>각 channel 독립 valid/ready;singleoutstanding</td>
<td>0</td>
</tr>
<tr>
<td>m_axil_wstrb</td>
<td>out</td>
<td>4</td>
<td>DMA Interconnect</td>
<td>AXI4-Lite wstrb 32bit register 전체 쓰기에 고정 F를 사용한다.</td>
<td>각 channel 독립 valid/ready;singleoutstanding</td>
<td>0</td>
</tr>
<tr>
<td>m_axil_wvalid</td>
<td>out</td>
<td>1</td>
<td>DMA Interconnect</td>
<td>AXI4-Lite wvalid write data의 유효성을 표시하며 AW 수락과 독립적으로 진행한다.</td>
<td>각 channel 독립 valid/ready;singleoutstanding</td>
<td>0</td>
</tr>
<tr>
<td>m_axil_wready</td>
<td>in</td>
<td>1</td>
<td>DMA Interconnect</td>
<td>AXI4-Lite wready write data 수락을 표시하며 AW 수락과 별도로 기억한다.</td>
<td>각 channel 독립 valid/ready;singleoutstanding</td>
<td>0</td>
</tr>
<tr>
<td>m_axil_bresp</td>
<td>in</td>
<td>2</td>
<td>DMA Interconnect</td>
<td>AXI4-Lite bresp AW와 W 수락 후 write 응답을 확인하고 OKAY가 아니면 fault로 처리한다.</td>
<td>각 channel 독립 valid/ready;singleoutstanding</td>
<td>0</td>
</tr>
<tr>
<td>m_axil_bvalid</td>
<td>in</td>
<td>1</td>
<td>DMA Interconnect</td>
<td>AXI4-Lite bvalid write 응답이 유효함을 표시한다.</td>
<td>각 channel 독립 valid/ready;singleoutstanding</td>
<td>0</td>
</tr>
<tr>
<td>m_axil_bready</td>
<td>out</td>
<td>1</td>
<td>DMA Interconnect</td>
<td>AXI4-Lite bready 진행 중 write transaction의 B 응답을 수락한다.</td>
<td>각 channel 독립 valid/ready;singleoutstanding</td>
<td>0</td>
</tr>
<tr>
<td>m_axil_araddr</td>
<td>out</td>
<td>32</td>
<td>DMA Interconnect</td>
<td>AXI4-Lite araddr DMA register read 주소를 AR handshake까지 유지한다.</td>
<td>각 channel 독립 valid/ready;singleoutstanding</td>
<td>0</td>
</tr>
<tr>
<td>m_axil_arvalid</td>
<td>out</td>
<td>1</td>
<td>DMA Interconnect</td>
<td>AXI4-Lite arvalid read 주소의 유효성을 표시한다.</td>
<td>각 channel 독립 valid/ready;singleoutstanding</td>
<td>0</td>
</tr>
<tr>
<td>m_axil_arready</td>
<td>in</td>
<td>1</td>
<td>DMA Interconnect</td>
<td>AXI4-Lite arready read 주소의 수락을 표시한다.</td>
<td>각 channel 독립 valid/ready;singleoutstanding</td>
<td>0</td>
</tr>
<tr>
<td>m_axil_arprot</td>
<td>out</td>
<td>3</td>
<td>DMA Interconnect</td>
<td>AXI4-Lite arprot 고정값 0으로 DMA register read 속성을 전달한다.</td>
<td>각 channel 독립 valid/ready;singleoutstanding</td>
<td>0</td>
</tr>
<tr>
<td>m_axil_rdata</td>
<td>in</td>
<td>32</td>
<td>DMA Interconnect</td>
<td>AXI4-Lite rdata AR 수락에 대응하는 DMA register 값을 전달한다.</td>
<td>각 channel 독립 valid/ready;singleoutstanding</td>
<td>0</td>
</tr>
<tr>
<td>m_axil_rresp</td>
<td>in</td>
<td>2</td>
<td>DMA Interconnect</td>
<td>AXI4-Lite rresp read 응답이 OKAY가 아니면 fault로 처리한다.</td>
<td>각 channel 독립 valid/ready;singleoutstanding</td>
<td>0</td>
</tr>
<tr>
<td>m_axil_rvalid</td>
<td>in</td>
<td>1</td>
<td>DMA Interconnect</td>
<td>AXI4-Lite rvalid read 응답 data/resp의 유효성을 표시한다.</td>
<td>각 channel 독립 valid/ready;singleoutstanding</td>
<td>0</td>
</tr>
<tr>
<td>m_axil_rready</td>
<td>out</td>
<td>1</td>
<td>DMA Interconnect</td>
<td>AXI4-Lite rready 진행 중 read transaction의 R 응답을 수락한다.</td>
<td>각 channel 독립 valid/ready;singleoutstanding</td>
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
<td>ROM 응답을 래치하여 stage 제어/대상 모듈 설정·DMA에 전달</td>
</tr>
<tr>
<td>kind</td>
<td>6:5</td>
<td>연산 종류: 0=conv0, 1=DW, 2=PW</td>
<td>ROM 응답을 래치하여 stage 제어/대상 모듈 설정·DMA에 전달</td>
</tr>
<tr>
<td>mode</td>
<td>8:7</td>
<td>출력 mode: 0=body, 1=heatmap, 2=offset</td>
<td>ROM 응답을 래치하여 stage 제어/대상 모듈 설정·DMA에 전달</td>
</tr>
<tr>
<td>hin</td>
<td>17:9</td>
<td>입력 tensor 높이</td>
<td>ROM 응답을 래치하여 stage 제어/대상 모듈 설정·DMA에 전달</td>
</tr>
<tr>
<td>win</td>
<td>26:18</td>
<td>입력 tensor 너비</td>
<td>ROM 응답을 래치하여 stage 제어/대상 모듈 설정·DMA에 전달</td>
</tr>
<tr>
<td>hout</td>
<td>35:27</td>
<td>출력 tensor 높이</td>
<td>ROM 응답을 래치하여 stage 제어/대상 모듈 설정·DMA에 전달</td>
</tr>
<tr>
<td>wout</td>
<td>44:36</td>
<td>출력 tensor 너비</td>
<td>ROM 응답을 래치하여 stage 제어/대상 모듈 설정·DMA에 전달</td>
</tr>
<tr>
<td>cin</td>
<td>53:45</td>
<td>입력 채널 수</td>
<td>ROM 응답을 래치하여 stage 제어/대상 모듈 설정·DMA에 전달</td>
</tr>
<tr>
<td>cout</td>
<td>62:54</td>
<td>출력 채널 수</td>
<td>ROM 응답을 래치하여 stage 제어/대상 모듈 설정·DMA에 전달</td>
</tr>
<tr>
<td>stride</td>
<td>64:63</td>
<td>convolution stride</td>
<td>ROM 응답을 래치하여 stage 제어/대상 모듈 설정·DMA에 전달</td>
</tr>
<tr>
<td>dilation</td>
<td>66:65</td>
<td>convolution dilation</td>
<td>ROM 응답을 래치하여 stage 제어/대상 모듈 설정·DMA에 전달</td>
</tr>
<tr>
<td>pad</td>
<td>68:67</td>
<td>convolution padding</td>
<td>ROM 응답을 래치하여 stage 제어/대상 모듈 설정·DMA에 전달</td>
</tr>
<tr>
<td>shift</td>
<td>74:69</td>
<td>재양자화 right shift</td>
<td>ROM 응답을 래치하여 stage 제어/대상 모듈 설정·DMA에 전달</td>
</tr>
<tr>
<td>weight_offset</td>
<td>106:75</td>
<td>packed weights 시작 byte offset</td>
<td>ROM 응답을 래치하여 stage 제어/대상 모듈 설정·DMA에 전달</td>
</tr>
<tr>
<td>param_offset</td>
<td>124:107</td>
<td>operation payload 내 param 시작 byte offset</td>
<td>ROM 응답을 래치하여 stage 제어/대상 모듈 설정·DMA에 전달</td>
</tr>
<tr>
<td>dma_bytes</td>
<td>144:125</td>
<td>정렬을 포함한 operation DMA byte 수</td>
<td>ROM 응답을 래치하여 stage 제어/대상 모듈 설정·DMA에 전달</td>
</tr>
<tr>
<td>stage_id</td>
<td>148:145</td>
<td>stage 번호 0..15</td>
<td>ROM 응답을 래치하여 stage 제어/대상 모듈 설정·DMA에 전달</td>
</tr>
<tr>
<td>reserved</td>
<td>255:149</td>
<td>항상 0</td>
<td>재정의 금지</td>
</tr>
</table>
## 레지스터 계약
기존 레지스터맵을 그대로 유지한다.
<table header-row="true">
<tr>
<td>Offset</td>
<td>Register</td>
<td>Access</td>
<td>Reset</td>
<td>Bits</td>
<td>동작</td>
</tr>
<tr>
<td>0x000</td>
<td>CTRL</td>
<td>W1P</td>
<td>0x00000000</td>
<td>0 START;1 DONE_CLEAR;2 SOFT_RESET</td>
<td>START busy/error 시 SLVERR. SOFT_RESET 은 DMA halted 확인 후만,모든 snapshot/validreset</td>
</tr>
<tr>
<td>0x004</td>
<td>STATUS</td>
<td>R</td>
<td>0x00000000</td>
<td>0 DONE;1 BUSY;2 ERROR;3 IMAGE_READ_DONE</td>
<td>DONE/ERRORsticky,IMAGE_READ_DONE 다음 STARTclear</td>
</tr>
<tr>
<td>0x008</td>
<td>THRESHOLD</td>
<td>RW</td>
<td>0x000000D2</td>
<td>signed8 \[7:0\]</td>
<td>-46 raw;나머지 0;idlewriteonly</td>
</tr>
<tr>
<td>0x00C</td>
<td>STRIDE</td>
<td>R</td>
<td>0x00000005</td>
<td>\[31:0\]</td>
<td>V4 fixed5; 모든 write SLVERR</td>
</tr>
<tr>
<td>0x010</td>
<td>PAD_TOP</td>
<td>R</td>
<td>0x00000118</td>
<td>\[31:0\]</td>
<td>fixed280</td>
</tr>
<tr>
<td>0x014</td>
<td>PAD_LEFT</td>
<td>R</td>
<td>0x00000000</td>
<td>\[31:0\]</td>
<td>fixed0</td>
</tr>
<tr>
<td>0x018</td>
<td>JOINT_0</td>
<td>R</td>
<td>0x00000000</td>
<td>score_raw\[31:24\],y\[23:12\],x\[11:0\]</td>
<td>invalid xy0,score signed8,atomicpublish</td>
</tr>
<tr>
<td>0x01C</td>
<td>JOINT_1</td>
<td>R</td>
<td>0x00000000</td>
<td>score_raw\[31:24\],y\[23:12\],x\[11:0\]</td>
<td>invalid xy0,score signed8,atomicpublish</td>
</tr>
<tr>
<td>0x020</td>
<td>JOINT_2</td>
<td>R</td>
<td>0x00000000</td>
<td>score_raw\[31:24\],y\[23:12\],x\[11:0\]</td>
<td>invalid xy0,score signed8,atomicpublish</td>
</tr>
<tr>
<td>0x024</td>
<td>JOINT_3</td>
<td>R</td>
<td>0x00000000</td>
<td>score_raw\[31:24\],y\[23:12\],x\[11:0\]</td>
<td>invalid xy0,score signed8,atomicpublish</td>
</tr>
<tr>
<td>0x028</td>
<td>JOINT_4</td>
<td>R</td>
<td>0x00000000</td>
<td>score_raw\[31:24\],y\[23:12\],x\[11:0\]</td>
<td>invalid xy0,score signed8,atomicpublish</td>
</tr>
<tr>
<td>0x02C</td>
<td>JOINT_5</td>
<td>R</td>
<td>0x00000000</td>
<td>score_raw\[31:24\],y\[23:12\],x\[11:0\]</td>
<td>invalid xy0,score signed8,atomicpublish</td>
</tr>
<tr>
<td>0x030</td>
<td>JOINT_6</td>
<td>R</td>
<td>0x00000000</td>
<td>score_raw\[31:24\],y\[23:12\],x\[11:0\]</td>
<td>invalid xy0,score signed8,atomicpublish</td>
</tr>
<tr>
<td>0x034</td>
<td>JOINT_7</td>
<td>R</td>
<td>0x00000000</td>
<td>score_raw\[31:24\],y\[23:12\],x\[11:0\]</td>
<td>invalid xy0,score signed8,atomicpublish</td>
</tr>
<tr>
<td>0x038</td>
<td>JOINT_8</td>
<td>R</td>
<td>0x00000000</td>
<td>score_raw\[31:24\],y\[23:12\],x\[11:0\]</td>
<td>invalid xy0,score signed8,atomicpublish</td>
</tr>
<tr>
<td>0x03C</td>
<td>JOINT_9</td>
<td>R</td>
<td>0x00000000</td>
<td>score_raw\[31:24\],y\[23:12\],x\[11:0\]</td>
<td>invalid xy0,score signed8,atomicpublish</td>
</tr>
<tr>
<td>0x040</td>
<td>JOINT_10</td>
<td>R</td>
<td>0x00000000</td>
<td>score_raw\[31:24\],y\[23:12\],x\[11:0\]</td>
<td>invalid xy0,score signed8,atomicpublish</td>
</tr>
<tr>
<td>0x044</td>
<td>JOINT_11</td>
<td>R</td>
<td>0x00000000</td>
<td>score_raw\[31:24\],y\[23:12\],x\[11:0\]</td>
<td>invalid xy0,score signed8,atomicpublish</td>
</tr>
<tr>
<td>0x048</td>
<td>JOINT_12</td>
<td>R</td>
<td>0x00000000</td>
<td>score_raw\[31:24\],y\[23:12\],x\[11:0\]</td>
<td>invalid xy0,score signed8,atomicpublish</td>
</tr>
<tr>
<td>0x04C</td>
<td>JOINT_13</td>
<td>R</td>
<td>0x00000000</td>
<td>score_raw\[31:24\],y\[23:12\],x\[11:0\]</td>
<td>invalid xy0,score signed8,atomicpublish</td>
</tr>
<tr>
<td>0x050</td>
<td>JOINT_14</td>
<td>R</td>
<td>0x00000000</td>
<td>score_raw\[31:24\],y\[23:12\],x\[11:0\]</td>
<td>invalid xy0,score signed8,atomicpublish</td>
</tr>
<tr>
<td>0x054</td>
<td>JOINT_15</td>
<td>R</td>
<td>0x00000000</td>
<td>score_raw\[31:24\],y\[23:12\],x\[11:0\]</td>
<td>invalid xy0,score signed8,atomicpublish</td>
</tr>
<tr>
<td>0x058</td>
<td>JOINT_16</td>
<td>R</td>
<td>0x00000000</td>
<td>score_raw\[31:24\],y\[23:12\],x\[11:0\]</td>
<td>invalid xy0,score signed8,atomicpublish</td>
</tr>
<tr>
<td>0x05C</td>
<td>VALID_FLAGS</td>
<td>R</td>
<td>0x00000000</td>
<td>\[16:0\]</td>
<td>score &amp;&amp; bounds</td>
</tr>
<tr>
<td>0x060</td>
<td>RED_THRESH</td>
<td>RW</td>
<td>0x006464A0</td>
<td>Bmax23:16,Gmax15:8,Rmin7:0</td>
<td>idlewrite;frame latch</td>
</tr>
<tr>
<td>0x064</td>
<td>BLUE_THRESH</td>
<td>RW</td>
<td>0x00A06464</td>
<td>Bmin23:16,Gmax15:8,Rmax7:0</td>
<td>idlewrite;frame latch</td>
</tr>
<tr>
<td>0x068</td>
<td>THUMB_XY</td>
<td>R</td>
<td>0x00000000</td>
<td>found31,y20:11,x10:0</td>
<td>reserved30:21=0</td>
</tr>
<tr>
<td>0x06C</td>
<td>INDEX_XY</td>
<td>R</td>
<td>0x00000000</td>
<td>found31,y20:11,x10:0</td>
<td>reserved30:21=0</td>
</tr>
<tr>
<td>0x070</td>
<td>FRAME_ID</td>
<td>RW</td>
<td>0x00000000</td>
<td>\[31:0\]</td>
<td>PS sourceframeid;snapshotSTART</td>
</tr>
<tr>
<td>0x074</td>
<td>RESULT_SEQ</td>
<td>R</td>
<td>0x00000000</td>
<td>\[31:0\]</td>
<td>publish 마다+1 wrap32</td>
</tr>
<tr>
<td>0x078</td>
<td>ERROR_STATUS</td>
<td>RW1C</td>
<td>0x00000000</td>
<td>\[31:0\]</td>
<td>firsterrorbit0protocol,1DMAresp,2DMAstatus,3timeout,4param;clear 은복구후</td>
</tr>
<tr>
<td>0x07C</td>
<td>IRQ_ENABLE</td>
<td>RW</td>
<td>0x00000000</td>
<td>\[31:0\]</td>
<td>bit0 전체 levelIRQenable</td>
</tr>
<tr>
<td>0x080</td>
<td>CYCLE_COUNT</td>
<td>R</td>
<td>0x00000000</td>
<td>\[31:0\]</td>
<td>lastframe start-publish cycles</td>
</tr>
<tr>
<td>0x084</td>
<td>RESULT_FRAME_ID</td>
<td>R</td>
<td>0x00000000</td>
<td>\[31:0\]</td>
<td>결과 sourceid</td>
</tr>
<tr>
<td>0x088</td>
<td>MIN_COUNT</td>
<td>RW</td>
<td>0x00000008</td>
<td>\[31:0\]</td>
<td>1..184320</td>
</tr>
<tr>
<td>0x08C</td>
<td>WGT_BASE</td>
<td>RW</td>
<td>0x10000000</td>
<td>\[31:0\]</td>
<td>packedweights64aligned</td>
</tr>
<tr>
<td>0x090</td>
<td>FM_A_BASE</td>
<td>RW</td>
<td>0x11000000</td>
<td>\[31:0\]</td>
<td>1MiB 영역 64aligned</td>
</tr>
<tr>
<td>0x094</td>
<td>FM_B_BASE</td>
<td>RW</td>
<td>0x11100000</td>
<td>\[31:0\]</td>
<td>1MiB 영역 64aligned,src/dstoverlap 금지</td>
</tr>
<tr>
<td>0x098</td>
<td>SG_DESC_BASE</td>
<td>RW</td>
<td>0x11200000</td>
<td>\[31:0\]</td>
<td>144x64BD,64aligned</td>
</tr>
<tr>
<td>0x09C</td>
<td>FRAME_BASE</td>
<td>RW</td>
<td>0x0A000000</td>
<td>\[31:0\]</td>
<td>PS 검증/디버그용;실제 SGbuffer 주소와일치</td>
</tr>
<tr>
<td>0x0A0</td>
<td>TIMEOUT_CYCLES</td>
<td>RW</td>
<td>0x05F5E100</td>
<td>\[31:0\]</td>
<td>no-progresswatchdog;0 금지</td>
</tr>
<tr>
<td>0x0A4</td>
<td>VERSION</td>
<td>R</td>
<td>0x00040000</td>
<td>\[31:0\]</td>
<td>major4 minor0</td>
</tr>
<tr>
<td>0x0A8</td>
<td>CAPS</td>
<td>R</td>
<td>0x00000007</td>
<td>\[31:0\]</td>
<td>bit0offset16,bit1dilation2,bit2neutralconv0pad</td>
</tr>
<tr>
<td>0x0AC</td>
<td>MODEL_TAG</td>
<td>R</td>
<td>0xC9854BB2</td>
<td>\[31:0\]</td>
<td>packedSHA 상위 8hex 의 32bit 값</td>
</tr>
</table>
# 타이밍 요구사항
상태 순서는 IDLE→SNAPSHOT→ROM_FETCH→LOAD_W→CONFIG→ARM_DEST→ARM_SOURCE→RUN→WAIT_COMMIT→NEXT_STAGE→POSTPROCESS→PUBLISH→IDLE다. S2MM을 먼저 arm하고 MM2S/PE를 시작한다. frame 전체에 대한 고정 cycle latency는 없으며 DMA/stream stall을 포함한다.
ImageSG는 144개 64byte-aligned BD 유한 chain이다. DMA halted에서 CURDESC를 설정하고 RS1 뒤 마지막 BD를 TAILDESC에 쓴다. 552960byte, 144 rowlast, DMA IDLE과 stage0 downstream 완료를 확인한 뒤 IMAGE RS=0을 쓰고 Halted=1을 기다린다. 이때 IMAGE_READ_DONE을 공개하여 PS가 source frame을 release할 수 있다.
기본 timeout은 100000000cycle의 무진행 시간이다. payload accept, MAC issue 또는 실제 FSM 전진이면 누산을 0으로 되돌리며 동일 DMASR 반복 polling은 progress로 세지 않는다. abort에서 출력 valid를 즉시 취소하지 않고 오류로 격리한다. IRQ는 level이며 W1C clear와 새 event가 겹치면 새 event가 우선한다. 결과는 다음 publish까지 유지한다.
reset은 공용 100MHz 상승 에지 기준 동기 active-low이며 rst_n을 4cycle 이상 assert한다. RAM 전체 clear는 하지 않는다. valid/ready 포트는 stall 동안 payload를 유지하고 accept를 기준으로 진행한다. cfg 포트가 있는 모듈은 idle에서 cfg를 수락한다. 포트가 없는 별도 frame_start/reset/ready를 추가하지 않는다.
## 필수 파형 시나리오
1. START→snapshot→ROM/DW·PW load→cfg의 순서를 관찰하고 busy 중 재START/config write의 SLVERR를 확인한다.
2. AXI-Lite AW/W/AR/R/B에 서로 다른 stall을 넣어 독립 handshake, 한 개 transaction 및 S2MM 선행 arm을 확인한다.
3. stage0의 144 rowlast·bytecount·SG Idle·downstream 완료를 서로 다른 시점에 발생시켜 RS0/Halted 이후에만 IMAGE_READ_DONE이 올라오는지 본다.
4. heatmap→offset 및 body DDR write의 완료 pulse 순서를 바꾸어 stage 전진과 shadow17joint·색상 동시 publish/RESULT_SEQ를 확인한다.
5. 동일 DMASR polling만 반복하거나 fault/abort를 넣어 watchdog, sticky error, IRQ event 우선 및 PS DMA reset→SOFT_RESET 복구를 확인한다.
# 체크리스트
### 기능 정확성
- [ ] DW/PW 설정·가중치를 모두 준비한 뒤 body stage를 시작한다.
- [ ] 사용 경로의 stream/DMA/DDR 완료를 모두 모아 stage를 전진시킨다.
- [ ] 결과17joint·색상·frame_id를 같은 publish에 commit한다.
### 인터페이스 프로토콜
- [ ] AW/W 독립 수락과 B, AR/R 응답 및 RESP 오류를 검사한다.
- [ ] S2MM arm을 MM2S/PE보다 앞세우고 DMA 제어 ownership을 지킨다.
### 경계조건
- [ ] busy/error START, DMA 오류, timeout 및 abort를 시험한다.
- [ ] 같은 status polling을 progress로 세지 않고 새 IRQ event를 W1C보다 우선한다.
### 통합 검증
- [ ] IMAGE_READ_DONE은 source 읽기와 stage0 완료 후 RS0/Halted까지 확인한다.
- [ ] 원문 레지스터 표와 wrapper/PS 계약을 대조한다.
- [ ] argmax와 FM의 완료 표현 확인 요청을 해결한 뒤 통합 종료 조건을 확정한다.
- [ ] reset·sticky fault·새 작업 차단 및 복구가 공용 계약과 일치한다.
# AI 에이전트용 프롬프트
아래 블록 전체를 복사한다. 공용 규칙·모듈 세부 계약·전체 포트·검증 시나리오를 포함한다.
```plain text
CNN_3D Hand Interaction Platform의 top_level_fsm 모듈을 CNN-v4.0 계약에 맞게 구현하라.
담당: 이영현. 대상 Zybo Z7-20 / XC7Z020. 언어는 Verilog-2001 .v만 허용한다.
SystemVerilog의 logic, always_ff, always_comb, typedef, enum, interface, package 등 문법은 금지한다.
아래 내용으로 RTL과 self-checking Verilog-2001 테스트벤치를 작성하라. 다른 Notion 페이지를 읽어야만 알 수 있는 계약을 가정하지 마라.
신호명·방향·폭·필드 위치·산술·DSP/BRAM 숫자를 바꾸거나 포트를 추가하지 마라.
데이터/가중치 및 실제 checkpoint 검증에는 원본 파일이 필요하다. 제공되지 않은 자료나 검증 결과를 만들어내지 마라.

[역할과 설계 배경]
PS가 확보한 완성 VDMA frame을 한 번 처리하도록 전체 CNN 파이프라인과 DMA를 지휘한다. PS는 FRAME_BASE/SG_DESC_BASE/FRAME_ID 및 설정을 준비한 뒤 CTRL.START를 쓴다. wrapper의 write 수락이 내부 1cycle start pulse가 된다. busy 중 start/config write는 SLVERR이며 실행 중 snapshot은 변하지 않는다. 10fps의 100ms 간격은 PS가 관리하고 완료 직후 자동 재시작하지 않는다.

stage0은 imageSG→downsample→conv0→FM_A다. stage1..13은 해당 DW/PW descriptor와 가중치를 모두 확보한 뒤 DW→PW를 실행하고 A/B를 교대로 사용한다. stage13 결과 B를 stage14 heatmap과 stage15 offset이 각각 읽는다. heatmap best를 보존한 상태로 offset을 처리한 뒤 17개 joint 결과를 수집한다.

각 모듈 done과 FM read/write 완료, DMA idle, weight load 완료를 기억하여 일찍 도착한 pulse를 잃지 않는다. 사용하지 않는 경로의 완료 조건은 미리 1로 처리한다. stage 결과의 DDR 쓰기까지 끝나야 다음 weight load와 FM swap을 진행한다. row/tap/batch/group 계수는 각 datapath 모듈이 소유한다.

내부 DMA sub-FSM은 IMAGE=0x40400000, WEIGHT=0x40410000, FEATURE=0x40420000에 32bit AXI-Lite master로 접근한다. AW와 W는 각각 독립 수락을 확인하고 B 응답을 기다리며, read는 AR 이후 R을 확인한다. register transaction은 한 번에 1개, WSTRB=F, PROT=0이다. DMA error mask는 0x770이며 IOC 하나만으로 완료를 판단하지 않는다. 상세 레지스터 순서는 아래 프롬프트와 레지스터 표의 기존 계약을 유지한다.

오류와 timeout은 sticky 상태로 남기고 새 start를 차단한다. PS가 세 DMA를 reset한 뒤 core SOFT_RESET으로 복구한다. 결과는 17joint, 색상2word, frame_id를 shadow에서 한 번에 commit하고 RESULT_SEQ를 증가시킨다. PS는 SEQ→결과→SEQ를 읽어 일관성을 확인한다. DSP0이며 정상 frame의 DMA register write 소유자는 PL, boot/error 복구 시 소유자는 PS다.

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
op_id	4:0	operation 번호 0..28	ROM 응답을 래치하여 stage 제어/대상 모듈 설정·DMA에 전달
kind	6:5	연산 종류: 0=conv0, 1=DW, 2=PW	ROM 응답을 래치하여 stage 제어/대상 모듈 설정·DMA에 전달
mode	8:7	출력 mode: 0=body, 1=heatmap, 2=offset	ROM 응답을 래치하여 stage 제어/대상 모듈 설정·DMA에 전달
hin	17:9	입력 tensor 높이	ROM 응답을 래치하여 stage 제어/대상 모듈 설정·DMA에 전달
win	26:18	입력 tensor 너비	ROM 응답을 래치하여 stage 제어/대상 모듈 설정·DMA에 전달
hout	35:27	출력 tensor 높이	ROM 응답을 래치하여 stage 제어/대상 모듈 설정·DMA에 전달
wout	44:36	출력 tensor 너비	ROM 응답을 래치하여 stage 제어/대상 모듈 설정·DMA에 전달
cin	53:45	입력 채널 수	ROM 응답을 래치하여 stage 제어/대상 모듈 설정·DMA에 전달
cout	62:54	출력 채널 수	ROM 응답을 래치하여 stage 제어/대상 모듈 설정·DMA에 전달
stride	64:63	convolution stride	ROM 응답을 래치하여 stage 제어/대상 모듈 설정·DMA에 전달
dilation	66:65	convolution dilation	ROM 응답을 래치하여 stage 제어/대상 모듈 설정·DMA에 전달
pad	68:67	convolution padding	ROM 응답을 래치하여 stage 제어/대상 모듈 설정·DMA에 전달
shift	74:69	재양자화 right shift	ROM 응답을 래치하여 stage 제어/대상 모듈 설정·DMA에 전달
weight_offset	106:75	packed weights 시작 byte offset	ROM 응답을 래치하여 stage 제어/대상 모듈 설정·DMA에 전달
param_offset	124:107	operation payload 내 param 시작 byte offset	ROM 응답을 래치하여 stage 제어/대상 모듈 설정·DMA에 전달
dma_bytes	144:125	정렬을 포함한 operation DMA byte 수	ROM 응답을 래치하여 stage 제어/대상 모듈 설정·DMA에 전달
stage_id	148:145	stage 번호 0..15	ROM 응답을 래치하여 stage 제어/대상 모듈 설정·DMA에 전달
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
input_cfg_valid	out	1	input_conv_pe	새 operation 설정 새 설정 payload가 유효함을 표시하고 ready와 함께 수락될 때까지 설정을 유지한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
input_cfg_ready	in	1	input_conv_pe	idle 일 때만 1 idle 상태에서 설정을 받을 수 있음을 표시하며 valid와 동시에 1일 때만 수락한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
input_cfg_desc	out	256	input_conv_pe	공용 descriptor; 수락 후 완료까지 고정 대응하는 설정 handshake에서 256bit를 래치하고 해당 작업이 끝날 때까지 유지한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
input_done	in	1	input_conv_pe	마지막 출력 accept 후 다음 cycle 1cycle pulse 이 완료 이벤트는 수신 제어기가 기억하며 구체적인 mode별 완료 조건은 타이밍 요구사항을 따른다.	마지막 출력 accept 기준	0
input_fault	in	1	input_conv_pe	protocol/range/count 위반 sticky; reset 으로 clear 위반을 발견하면 상태를 유지하여 새 작업을 차단하고 reset으로 해제한다.	오류 검출 다음 cycle	0
line_cfg_valid	out	1	line_buffer	새 operation 설정 새 설정 payload가 유효함을 표시하고 ready와 함께 수락될 때까지 설정을 유지한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
line_cfg_ready	in	1	line_buffer	idle 일 때만 1 idle 상태에서 설정을 받을 수 있음을 표시하며 valid와 동시에 1일 때만 수락한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
line_cfg_desc	out	256	line_buffer	공용 descriptor; 수락 후 완료까지 고정 대응하는 설정 handshake에서 256bit를 래치하고 해당 작업이 끝날 때까지 유지한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
line_done	in	1	line_buffer	마지막 출력 accept 후 다음 cycle 1cycle pulse 이 완료 이벤트는 수신 제어기가 기억하며 구체적인 mode별 완료 조건은 타이밍 요구사항을 따른다.	마지막 출력 accept 기준	0
line_fault	in	1	line_buffer	protocol/range/count 위반 sticky; reset 으로 clear 위반을 발견하면 상태를 유지하여 새 작업을 차단하고 reset으로 해제한다.	오류 검출 다음 cycle	0
dw_cfg_valid	out	1	depthwise_conv_pe	새 operation 설정 새 설정 payload가 유효함을 표시하고 ready와 함께 수락될 때까지 설정을 유지한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
dw_cfg_ready	in	1	depthwise_conv_pe	idle 일 때만 1 idle 상태에서 설정을 받을 수 있음을 표시하며 valid와 동시에 1일 때만 수락한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
dw_cfg_desc	out	256	depthwise_conv_pe	공용 descriptor; 수락 후 완료까지 고정 대응하는 설정 handshake에서 256bit를 래치하고 해당 작업이 끝날 때까지 유지한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
dw_done	in	1	depthwise_conv_pe	마지막 출력 accept 후 다음 cycle 1cycle pulse 이 완료 이벤트는 수신 제어기가 기억하며 구체적인 mode별 완료 조건은 타이밍 요구사항을 따른다.	마지막 출력 accept 기준	0
dw_fault	in	1	depthwise_conv_pe	protocol/range/count 위반 sticky; reset 으로 clear 위반을 발견하면 상태를 유지하여 새 작업을 차단하고 reset으로 해제한다.	오류 검출 다음 cycle	0
pw_cfg_valid	out	1	pointwise_conv_pe	새 operation 설정 새 설정 payload가 유효함을 표시하고 ready와 함께 수락될 때까지 설정을 유지한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
pw_cfg_ready	in	1	pointwise_conv_pe	idle 일 때만 1 idle 상태에서 설정을 받을 수 있음을 표시하며 valid와 동시에 1일 때만 수락한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
pw_cfg_desc	out	256	pointwise_conv_pe	공용 descriptor; 수락 후 완료까지 고정 대응하는 설정 handshake에서 256bit를 래치하고 해당 작업이 끝날 때까지 유지한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
pw_done	in	1	pointwise_conv_pe	마지막 출력 accept 후 다음 cycle 1cycle pulse 이 완료 이벤트는 수신 제어기가 기억하며 구체적인 mode별 완료 조건은 타이밍 요구사항을 따른다.	마지막 출력 accept 기준	0
pw_fault	in	1	pointwise_conv_pe	protocol/range/count 위반 sticky; reset 으로 clear 위반을 발견하면 상태를 유지하여 새 작업을 차단하고 reset으로 해제한다.	오류 검출 다음 cycle	0
fm_cfg_valid	out	1	feature_map_io	새 operation 설정 새 설정 payload가 유효함을 표시하고 ready와 함께 수락될 때까지 설정을 유지한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
fm_cfg_ready	in	1	feature_map_io	idle 일 때만 1 idle 상태에서 설정을 받을 수 있음을 표시하며 valid와 동시에 1일 때만 수락한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
fm_cfg_desc	out	256	feature_map_io	공용 descriptor; 수락 후 완료까지 고정 대응하는 설정 handshake에서 256bit를 래치하고 해당 작업이 끝날 때까지 유지한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
fm_done	in	1	feature_map_io	마지막 출력 accept 후 다음 cycle 1cycle pulse 이 완료 이벤트는 수신 제어기가 기억하며 구체적인 mode별 완료 조건은 타이밍 요구사항을 따른다.	마지막 출력 accept 기준	0
fm_fault	in	1	feature_map_io	protocol/range/count 위반 sticky; reset 으로 clear 위반을 발견하면 상태를 유지하여 새 작업을 차단하고 reset으로 해제한다.	오류 검출 다음 cycle	0
fm_src_addr	out	32	feature_map_io	cfg 와함께래치 cfg handshake에서 다른 전송 설정과 함께 래치하여 실행 중 변경하지 않는다.	valid && ready 에서 수락; stall 동안 payload 유지	0
fm_dst_addr	out	32	feature_map_io	cfg 와함께래치 cfg handshake에서 다른 전송 설정과 함께 래치하여 실행 중 변경하지 않는다.	valid && ready 에서 수락; stall 동안 payload 유지	0
fm_src_bytes	out	20	feature_map_io	cfg 와함께래치 cfg handshake에서 다른 전송 설정과 함께 래치하여 실행 중 변경하지 않는다.	valid && ready 에서 수락; stall 동안 payload 유지	0
fm_dst_bytes	out	20	feature_map_io	cfg 와함께래치 cfg handshake에서 다른 전송 설정과 함께 래치하여 실행 중 변경하지 않는다.	valid && ready 에서 수락; stall 동안 payload 유지	0
fm_read_en	out	1	feature_map_io	cfg 와함께래치 cfg handshake에서 다른 전송 설정과 함께 래치하여 실행 중 변경하지 않는다.	valid && ready 에서 수락; stall 동안 payload 유지	0
fm_write_en	out	1	feature_map_io	cfg 와함께래치 cfg handshake에서 다른 전송 설정과 함께 래치하여 실행 중 변경하지 않는다.	valid && ready 에서 수락; stall 동안 payload 유지	0
fm_read_dma_done	out	1	feature_map_io	DMAsticky 완료/error 이 완료 이벤트는 수신 제어기가 기억하며 구체적인 mode별 완료 조건은 타이밍 요구사항을 따른다.	valid && ready 에서 수락; stall 동안 payload 유지	0
fm_write_dma_done	out	1	feature_map_io	DMAsticky 완료/error 이 완료 이벤트는 수신 제어기가 기억하며 구체적인 mode별 완료 조건은 타이밍 요구사항을 따른다.	valid && ready 에서 수락; stall 동안 payload 유지	0
fm_dma_error	out	1	feature_map_io	DMAsticky 완료/error top의 DMA 제어 결과를 받아 stream 수락 상태와 함께 전송 완료 또는 오류를 판정한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
fm_read_done	in	1	feature_map_io	마지막 stream accept 및 DMA 완료 feature_map_io가 최종 stream 수락과 DMA 읽기 완료를 함께 확인한 상태다.	다음 cfg 까지 hold	0
fm_write_done	in	1	feature_map_io	마지막 stream accept 및 DMA 완료 feature_map_io가 최종 stream 수락과 DMA 쓰기 완료를 함께 확인한 상태다.	다음 cfg 까지 hold	0
down_cfg_valid	out	1	downsample_module	새 operation 설정 새 설정 payload가 유효함을 표시하고 ready와 함께 수락될 때까지 설정을 유지한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
down_cfg_ready	in	1	downsample_module	idle 일 때만 1 idle 상태에서 설정을 받을 수 있음을 표시하며 valid와 동시에 1일 때만 수락한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
down_cfg_desc	out	256	downsample_module	공용 descriptor; 수락 후 완료까지 고정 대응하는 설정 handshake에서 256bit를 래치하고 해당 작업이 끝날 때까지 유지한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
down_done	in	1	downsample_module	마지막 출력 accept 후 다음 cycle 1cycle pulse 이 완료 이벤트는 수신 제어기가 기억하며 구체적인 mode별 완료 조건은 타이밍 요구사항을 따른다.	마지막 출력 accept 기준	0
down_fault	in	1	downsample_module	protocol/range/count 위반 sticky; reset 으로 clear 위반을 발견하면 상태를 유지하여 새 작업을 차단하고 reset으로 해제한다.	오류 검출 다음 cycle	0
arg_cfg_valid	out	1	argmax_threshold	새 operation 설정 새 설정 payload가 유효함을 표시하고 ready와 함께 수락될 때까지 설정을 유지한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
arg_cfg_ready	in	1	argmax_threshold	idle 일 때만 1 idle 상태에서 설정을 받을 수 있음을 표시하며 valid와 동시에 1일 때만 수락한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
arg_cfg_desc	out	256	argmax_threshold	공용 descriptor; 수락 후 완료까지 고정 대응하는 설정 handshake에서 256bit를 래치하고 해당 작업이 끝날 때까지 유지한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
arg_done	in	1	argmax_threshold	마지막 출력 accept 후 다음 cycle 1cycle pulse 이 완료 이벤트는 수신 제어기가 기억하며 구체적인 mode별 완료 조건은 타이밍 요구사항을 따른다.	마지막 출력 accept 기준	0
arg_fault	in	1	argmax_threshold	protocol/range/count 위반 sticky; reset 으로 clear 위반을 발견하면 상태를 유지하여 새 작업을 차단하고 reset으로 해제한다.	오류 검출 다음 cycle	0
coord_cfg_valid	out	1	coord_restore	새 operation 설정 새 설정 payload가 유효함을 표시하고 ready와 함께 수락될 때까지 설정을 유지한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
coord_cfg_ready	in	1	coord_restore	idle 일 때만 1 idle 상태에서 설정을 받을 수 있음을 표시하며 valid와 동시에 1일 때만 수락한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
coord_cfg_desc	out	256	coord_restore	공용 descriptor; 수락 후 완료까지 고정 대응하는 설정 handshake에서 256bit를 래치하고 해당 작업이 끝날 때까지 유지한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
coord_done	in	1	coord_restore	마지막 출력 accept 후 다음 cycle 1cycle pulse 이 완료 이벤트는 수신 제어기가 기억하며 구체적인 mode별 완료 조건은 타이밍 요구사항을 따른다.	마지막 출력 accept 기준	0
coord_fault	in	1	coord_restore	protocol/range/count 위반 sticky; reset 으로 clear 위반을 발견하면 상태를 유지하여 새 작업을 차단하고 reset으로 해제한다.	오류 검출 다음 cycle	0
coord_m_joint_data	in	32	coord_restore	packed JOINT register 해당 유효 신호와 같은 cycle에 전달하며 명세의 byte 순서·부호·lane 배치를 보존한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
coord_m_joint_valid	in	1	coord_restore	payload 유효; ready 와 무관하게 assert 가능 해당 포트의 원래 pulse/handshake 타이밍에 맞춰 수신 측이 유효 이벤트를 인식한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
coord_m_joint_ready	out	1	coord_restore	수신 여유 대응 valid와 동시에 1인 상승 에지에서만 payload를 수락한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
coord_m_joint_last	in	1	coord_restore	packet 마지막 beat 마지막 표시도 valid와 함께 수락해야 하며 stall 동안 유지한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
coord_m_joint_index	in	5	coord_restore	joint0..16 현재 결과 payload가 어느 관절0..16에 해당하는지 같은 handshake로 전달한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
coord_m_joint_good	in	1	coord_restore	score && image bounds valid 현재 관절의 score와 원본 영상 bounds를 모두 만족한 결과임을 나타낸다.	valid && ready 에서 수락; stall 동안 payload 유지	0
coord_threshold	out	8	coord_restore	signed8 snapshot frame 시작에 snapshot한 signed8 임계값을 coord_restore에 전달한다.	valid && ready 에서 수락; stall 동안 payload 유지	0xD2
swap_fault	in	1	weight_bram_swap_fsm	protocol/range/count 위반 sticky; reset 으로 clear 위반을 발견하면 상태를 유지하여 새 작업을 차단하고 reset으로 해제한다.	오류 검출 다음 cycle	0
swap_load_valid	out	1	weight_bram_swap_fsm	단일 operation 로드계약 대응 payload가 유효함을 표시한다. 수신 ready를 기다리는 동안 payload를 바꾸지 않는다.	valid && ready 에서 수락; stall 동안 payload 유지	0
swap_load_ready	in	1	weight_bram_swap_fsm	단일 operation 로드계약 대응 valid와 동시에 1인 상승 에지에서만 payload를 수락한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
swap_load_desc	out	256	weight_bram_swap_fsm	단일 operation 로드계약	valid && ready 에서 수락; stall 동안 payload 유지	0
swap_load_done	in	1	weight_bram_swap_fsm	단일 operation 로드계약 가중치 모듈의 모든 payload 검사와 최종 BRAM write가 완료된 1cycle pulse를 기억한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
color_fault	in	1	color_marker_detect	protocol/range/count 위반 sticky; reset 으로 clear 위반을 발견하면 상태를 유지하여 새 작업을 차단하고 reset으로 해제한다.	오류 검출 다음 cycle	0
color_frame_start	out	1	color_marker_detect	새 frame reset accum color_marker_detect의 새 frame 누산 초기화와 설정 snapshot을 시작한다.	frame_start 에서래치	0
color_red_cfg	out	24	color_marker_detect	Bmax/Gmax/Rmin 현재 frame에 사용할 설정값을 snapshot하여 실행 도중 값이 바뀌지 않게 전달한다.	frame_start 에서래치	0x6464A0
color_blue_cfg	out	24	color_marker_detect	Bmin/Gmax/Rmax 현재 frame에 사용할 설정값을 snapshot하여 실행 도중 값이 바뀌지 않게 전달한다.	frame_start 에서래치	0xA06464
color_min_count	out	18	color_marker_detect	최소색상 pixel 수	frame_start 에서래치	8
color_red_word	in	32	color_marker_detect	found/y/x 해당 색상의 found와 원본 y/x를 지정된 32bit 배치로 전달한다.	완료 후 hold	0
color_blue_word	in	32	color_marker_detect	found/y/x 해당 색상의 found와 원본 y/x를 지정된 32bit 배치로 전달한다.	완료 후 hold	0
color_results_valid	in	1	color_marker_detect	전체 divider 완료 pulse 색상 divider 결과 등록 완료 pulse를 받아 같은 frame의 CNN 결과와 함께 publish한다.	완료 후 hold	0
rom_fault	in	1	layer_param_rom	protocol/range/count 위반 sticky; reset 으로 clear 위반을 발견하면 상태를 유지하여 새 작업을 차단하고 reset으로 해제한다.	오류 검출 다음 cycle	0
rom_req_valid	out	1	layer_param_rom	descriptor read1cycle 대응 payload가 유효함을 표시한다. 수신 ready를 기다리는 동안 payload를 바꾸지 않는다.	valid && ready 에서 수락; stall 동안 payload 유지	0
rom_req_ready	in	1	layer_param_rom	descriptor read1cycle 대응 valid와 동시에 1인 상승 에지에서만 payload를 수락한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
rom_req_op	out	5	layer_param_rom	descriptor read1cycle ROM 요청 handshake에서 조회할 operation 번호를 전달한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
rom_rsp_valid	in	1	layer_param_rom	descriptor read1cycle 대응 payload가 유효함을 표시한다. 수신 ready를 기다리는 동안 payload를 바꾸지 않는다.	valid && ready 에서 수락; stall 동안 payload 유지	0
rom_rsp_ready	out	1	layer_param_rom	descriptor read1cycle 대응 valid와 동시에 1인 상승 에지에서만 payload를 수락한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
rom_rsp_desc	in	256	layer_param_rom	descriptor read1cycle 요청한 operation의 cfg_desc256 응답을 받아 현재 DW/PW 설정에 맞게 보관한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
threshold_cfg	in	8	cnn_accelerator_top	signed8 register snapshot 현재 frame에 사용할 설정값을 snapshot하여 실행 도중 값이 바뀌지 않게 전달한다.	frame snapshot / status hold	0
red_thresh_cfg	in	24	cnn_accelerator_top	RED_THRESH register snapshot 현재 frame에 사용할 설정값을 snapshot하여 실행 도중 값이 바뀌지 않게 전달한다.	frame snapshot / status hold	0
blue_thresh_cfg	in	24	cnn_accelerator_top	BLUE_THRESH register snapshot 현재 frame에 사용할 설정값을 snapshot하여 실행 도중 값이 바뀌지 않게 전달한다.	frame snapshot / status hold	0
min_count_cfg	in	18	cnn_accelerator_top	MIN_COUNT register snapshot 현재 frame에 사용할 설정값을 snapshot하여 실행 도중 값이 바뀌지 않게 전달한다.	frame snapshot / status hold	0
datapath_progress	in	1	cnn_accelerator_top	wrapper OR of accepted transfers and MAC issues wrapper가 내부 stream accept와 MAC issue를 OR한 값으로, 무진행 watchdog을 갱신한다.	frame snapshot / status hold	0
start	in	1	cnn_accelerator_top	wrapperCTRLwritepulse wrapper가 CTRL.START write를 수락한 1cycle pulse로 한 frame의 처리를 시작한다.	frame snapshot / status hold	0
clear_done	in	1	cnn_accelerator_top	CTRLbit1W1P CTRL의 DONE_CLEAR 쓰기로 done pending을 지우며 같은 cycle의 새 event가 우선한다.	frame snapshot / status hold	0
clear_error	in	1	cnn_accelerator_top	ERRORW1C ERROR W1C 동작을 전달하며 같은 cycle의 새 error event가 우선한다.	frame snapshot / status hold	0
frame_id	in	32	cnn_accelerator_top	PSassignedsourceid PS가 지정한 source frame 식별자를 snapshot하여 해당 결과와 함께 공개한다.	frame snapshot / status hold	0
sg_desc_base	in	32	cnn_accelerator_top	144BDbase PS가 준비한 144개 64byte-aligned 유한 SG descriptor chain의 시작 주소다.	frame snapshot / status hold	0
wgt_base	in	32	cnn_accelerator_top	packedweightsbase generated/weights_v4.bin을 배치한 DDR 기준 주소로 operation offset과 함께 DMA에 사용한다.	frame snapshot / status hold	0
fm_a_base	in	32	cnn_accelerator_top	FM_A FM_A의 DDR 기준 주소이며 기본 배치는 0x11000000이다.	frame snapshot / status hold	0
fm_b_base	in	32	cnn_accelerator_top	FM_B FM_B의 DDR 기준 주소이며 기본 배치는 0x11100000이다.	frame snapshot / status hold	0
timeout_cycles	in	32	cnn_accelerator_top	no-progress timeout 진행 없는 cycle의 상한이며 기본값은 100000000이다.	frame snapshot / status hold	0
busy	out	1	cnn_accelerator_top	inflightframe 한 frame을 처리하는 동안 활성 상태를 표시하여 중복 START와 설정 변경을 차단한다.	frame snapshot / status hold	0
done_pending	out	1	cnn_accelerator_top	stickycompletion 해당 이벤트를 sticky로 보관하며 clear와 새 이벤트가 겹치면 새 이벤트를 유지한다.	frame snapshot / status hold	0
error_pending	out	1	cnn_accelerator_top	stickyerror 해당 이벤트를 sticky로 보관하며 clear와 새 이벤트가 겹치면 새 이벤트를 유지한다.	frame snapshot / status hold	0
error_code	out	32	cnn_accelerator_top	firstfaultsource 최초 fault의 원인을 보존하여 PS가 복구 원인을 확인할 수 있게 한다.	frame snapshot / status hold	0
result_seq	out	32	cnn_accelerator_top	publishsequence shadow 결과를 한 번에 publish할 때 증가하며 PS가 결과 일관성을 확인하는 데 사용한다.	frame snapshot / status hold	0
result_frame_id	out	32	cnn_accelerator_top	publishedframeid 현재 공개된 joint 및 색상 결과에 대응하는 source frame 식별자다.	frame snapshot / status hold	0
joint_words	out	544	cnn_accelerator_top	17packed32bitresults 관절0..16의 32bit packed 결과를 공개하는 17word 버스다.	frame snapshot / status hold	0
joint_flags	out	17	cnn_accelerator_top	publishedgoodflags 공개된 관절0..16의 score/bounds 유효 판정을 각각 표시한다.	frame snapshot / status hold	0
red_word	out	32	cnn_accelerator_top	publishedmarker 해당 색상의 found와 원본 y/x를 지정된 32bit 배치로 전달한다.	frame snapshot / status hold	0
blue_word	out	32	cnn_accelerator_top	publishedmarker 해당 색상의 found와 원본 y/x를 지정된 32bit 배치로 전달한다.	frame snapshot / status hold	0
cycle_count	out	32	cnn_accelerator_top	starttopublishcycles START부터 publish까지의 frame 처리 cycle 수를 전달한다.	frame snapshot / status hold	0
image_read_done	out	1	cnn_accelerator_top	sourcebufferreleaseallowed stage0 및 이미지 SG 종료 후 IMAGE RS=0과 Halted=1까지 확인하여 PS의 원본 frame 해제를 허용한다.	frame snapshot / status hold	0
m_axil_awaddr	out	32	DMA Interconnect	AXI4-Lite awaddr DMA register write 주소를 AW handshake까지 유지한다.	각 channel 독립 valid/ready;singleoutstanding	0
m_axil_awvalid	out	1	DMA Interconnect	AXI4-Lite awvalid write 주소의 유효성을 표시하며 AWREADY와 독립적으로 발생한다.	각 channel 독립 valid/ready;singleoutstanding	0
m_axil_awready	in	1	DMA Interconnect	AXI4-Lite awready 주소 수락을 표시한다. W 채널 수락과 별도로 기억한다.	각 channel 독립 valid/ready;singleoutstanding	0
m_axil_awprot	out	3	DMA Interconnect	AXI4-Lite awprot 고정값 0으로 DMA register 접근 속성을 전달한다.	각 channel 독립 valid/ready;singleoutstanding	0
m_axil_wdata	out	32	DMA Interconnect	AXI4-Lite wdata DMA register write 값을 W handshake까지 유지한다.	각 channel 독립 valid/ready;singleoutstanding	0
m_axil_wstrb	out	4	DMA Interconnect	AXI4-Lite wstrb 32bit register 전체 쓰기에 고정 F를 사용한다.	각 channel 독립 valid/ready;singleoutstanding	0
m_axil_wvalid	out	1	DMA Interconnect	AXI4-Lite wvalid write data의 유효성을 표시하며 AW 수락과 독립적으로 진행한다.	각 channel 독립 valid/ready;singleoutstanding	0
m_axil_wready	in	1	DMA Interconnect	AXI4-Lite wready write data 수락을 표시하며 AW 수락과 별도로 기억한다.	각 channel 독립 valid/ready;singleoutstanding	0
m_axil_bresp	in	2	DMA Interconnect	AXI4-Lite bresp AW와 W 수락 후 write 응답을 확인하고 OKAY가 아니면 fault로 처리한다.	각 channel 독립 valid/ready;singleoutstanding	0
m_axil_bvalid	in	1	DMA Interconnect	AXI4-Lite bvalid write 응답이 유효함을 표시한다.	각 channel 독립 valid/ready;singleoutstanding	0
m_axil_bready	out	1	DMA Interconnect	AXI4-Lite bready 진행 중 write transaction의 B 응답을 수락한다.	각 channel 독립 valid/ready;singleoutstanding	0
m_axil_araddr	out	32	DMA Interconnect	AXI4-Lite araddr DMA register read 주소를 AR handshake까지 유지한다.	각 channel 독립 valid/ready;singleoutstanding	0
m_axil_arvalid	out	1	DMA Interconnect	AXI4-Lite arvalid read 주소의 유효성을 표시한다.	각 channel 독립 valid/ready;singleoutstanding	0
m_axil_arready	in	1	DMA Interconnect	AXI4-Lite arready read 주소의 수락을 표시한다.	각 channel 독립 valid/ready;singleoutstanding	0
m_axil_arprot	out	3	DMA Interconnect	AXI4-Lite arprot 고정값 0으로 DMA register read 속성을 전달한다.	각 channel 독립 valid/ready;singleoutstanding	0
m_axil_rdata	in	32	DMA Interconnect	AXI4-Lite rdata AR 수락에 대응하는 DMA register 값을 전달한다.	각 channel 독립 valid/ready;singleoutstanding	0
m_axil_rresp	in	2	DMA Interconnect	AXI4-Lite rresp read 응답이 OKAY가 아니면 fault로 처리한다.	각 channel 독립 valid/ready;singleoutstanding	0
m_axil_rvalid	in	1	DMA Interconnect	AXI4-Lite rvalid read 응답 data/resp의 유효성을 표시한다.	각 channel 독립 valid/ready;singleoutstanding	0
m_axil_rready	out	1	DMA Interconnect	AXI4-Lite rready 진행 중 read transaction의 R 응답을 수락한다.	각 channel 독립 valid/ready;singleoutstanding	0

[모듈 세부 계약 전문: 기존 CNN-v4.0 기술 조건 보존]
## frame 제어
PS 가완성된 VDMA frame 을확보하고 FRAME_BASE/SG_DESC_BASE/FRAME_ID 와설정값을쓴후 CTRL.START 를쓴다. IP 가 write 수락으로내부 1cyclepulse 를만든다. busy 중 start/configwrite 는 SLVERR,실행중설정불변.10fpspacing 은 PS 가 100ms 주기로수행한다. 완료직후무조건자동재시작하지않는다. IMAGE_READ_DONE 이후 PS 는 sourceframe 을 release 가능하다.
## stage FSM
IDLE→SNAPSHOT→ROM_FETCH→LOAD_W→CONFIG→ARM_DEST→ARM_SOURCE→RUN→WAIT_COMMIT→NEXT_STAGE→POSTPROCESS→PUBLISH→IDLE. body(stage1..13)는 DW/PW 파라미터둘을조회/로드후같은 stage 에서 DW→PW 동작. stage0 은 imageSG→downsample→conv0→FM_A. stage1A→B,stage2B→A...stage13A→B. stage14/15 는 B 에서 PW 로각각읽고 argmax 로출력. stage14 에서는 heatmapstate 보존,stage15 후 argmax→coord17joint 를수집한다.
현재 stage 의 PE done,FM read_done/write_done,DMA idle,weight load_done 을 sticky 로기억한다. 데이터없는경로의완료조건은미리 1. count/byte/tag 로최종 transfer 를확인한다. stage 가끝나고 DDR 쓰기까지완료돼야 WGT reload 와 FM swap. row/tap/batch/group 계수는각데이터패스모듈소유이며 top 이 valid 없는 last 만보고증가시키지않는다.
## DMA controller(본 모듈 내부 sub-FSM)
외부 DMA 주소 IMAGE=0x40400000,WEIGHT=0x40410000,FEATURE=0x40420000 을 BD 와고정한다.32bit AXI-Lite master 완전한 AW/W/B 및 AR/R 를사용한다. write transaction 은 AW 와 W 각각독립적으로수락확인 후 B 응답을기다린다. WSTRB=F,PROT=0. read 도 AR 수락후 R 을받고 RESP!=OKAY 면 fault. 한번에 1 개 registertransaction 만발행한다.
SimpleMM2S(weight,feature read): DMASR0x04 의기존 IRQ pending[14:12]만 W1C 처리; error status[10:8,6:4]는 RO 이므로 set 이면 DMA reset 필요,DMACR0x00.RS=1,SA0x18, LENGTH0x28 순서. SimpleS2MM: DMASR0x34 확인/clear,DMACR0x30.RS=1,DA0x48,LENGTH0x58 을마지막으로써서 arm. S2MM 을먼저준비하고 MM2S/PE 를시작한다. DONE 은해당 stream 의마지막 bytecount 와 DMASR.idlebit1&&errorbits 없음을함께확인;IOCbit12 를보조사용. error mask=0x770. IOC 를단독으로전체 frame 끝으로사용하지않는다.
ImageSG: PS 가 idle 때 64byte aligned144BD ring 이아닌유한 chain 을구성한다. CURDESC0x08 은 DMAhalted 때만설정,RS1 후 TAILDESC0x10=base+143*64. 각 BDbuffer=FRAME_BASE+5*k*3840,length3840,TXSOF/TXEOF 둘다 1,next=base+(k+1)*64(마지막은 base 로연결해도 tail 에서정지). reserved/status 초기화,descriptorflush 후 START. 이미지 552960bytes 수신과 144rowlast 및 DMASR.IDLE 을모두기다린다. stage0 의 downsample/conv0/FM 완료와 SG Idle 을 확인한 후 PL controller 가 IMAGE DMACR.RS=0 을 쓰고 Halted=1 을 기다린다. 이때 IMAGE_READ_DONE=1 로 공개하고 다음 stage 로 진행한다. 다음 frame 에서 PL 이 halted 상태의 CURDESC 를 다시 설정한다. PS 는 다음 START 전 완료된 BD 의 status 를 0 으로 초기화하고 flush 하며, 정상 frame 사이에도 DMA register 제어는 PL 이 소유한다.
## 오류와 통지
TIMEOUT(default100000000cycle)동안진행없거나 DMARESP/protocolfault 발생 시 ERROR sticky,busy0,newstart 차단. PS 가세 DMA 를 reset 하고 coreSOFT_RESET 후재시작한다. timeout 누산은 payload accept, MAC issue 또는 실제 FSM 단계 전진에서 0 으로 복귀한다. 같은 DMASR 상태를 반복 읽는 polling 응답은 progress 가 아니다,wrapper 가 모든 내부 stream accept 와 MAC issue 를 OR 한 datapath_progress 입력도 포함한다. abort 는즉시출력 valid 를취소하지않고 core 를 error 로격리하며 DMAreset 을 PS 에요청한다. abortedFM 결과는 publish 하지않는다.
IRQ=(DONE_PENDING||ERROR_PENDING)&&IRQ_ENABLE,levelhigh;W1Cclear,동시새 event 우선. CortexGIClevel 설정. 결과 17joint 와색상 2word,frame_id 를 shadow 에서한번에 commit 한다. RESULT_SEQ 를 commit 때증가. PS 는 SEQ→전체결과→SEQ 순으로같은값인지확인한다. normaldone 후 PS 가 pending 을 clear 하고다음 START. 결과는다음 publish 전까지유지.
## 리소스
DSP0. 모든 adder/count/address 상수식 use_dsp=no. DMA/FIFO 는 wrapper 전체예산에포함한다. PS 와 PLmaster 동시 write 를금지하는 ownership 규칙:PS 는 boot/error 복구때만 DMA 제어,normal frame 진행 중 PL 단독소유. status read 는허용.

[기존 레지스터 계약 전체]
Offset	Register	Access	Reset	Bits	동작
0x000	CTRL	W1P	0x00000000	0 START;1 DONE_CLEAR;2 SOFT_RESET	START busy/error 시 SLVERR. SOFT_RESET 은 DMA halted 확인 후만,모든 snapshot/validreset
0x004	STATUS	R	0x00000000	0 DONE;1 BUSY;2 ERROR;3 IMAGE_READ_DONE	DONE/ERRORsticky,IMAGE_READ_DONE 다음 STARTclear
0x008	THRESHOLD	RW	0x000000D2	signed8 \[7:0\]	-46 raw;나머지 0;idlewriteonly
0x00C	STRIDE	R	0x00000005	\[31:0\]	V4 fixed5; 모든 write SLVERR
0x010	PAD_TOP	R	0x00000118	\[31:0\]	fixed280
0x014	PAD_LEFT	R	0x00000000	\[31:0\]	fixed0
0x018	JOINT_0	R	0x00000000	score_raw\[31:24\],y\[23:12\],x\[11:0\]	invalid xy0,score signed8,atomicpublish
0x01C	JOINT_1	R	0x00000000	score_raw\[31:24\],y\[23:12\],x\[11:0\]	invalid xy0,score signed8,atomicpublish
0x020	JOINT_2	R	0x00000000	score_raw\[31:24\],y\[23:12\],x\[11:0\]	invalid xy0,score signed8,atomicpublish
0x024	JOINT_3	R	0x00000000	score_raw\[31:24\],y\[23:12\],x\[11:0\]	invalid xy0,score signed8,atomicpublish
0x028	JOINT_4	R	0x00000000	score_raw\[31:24\],y\[23:12\],x\[11:0\]	invalid xy0,score signed8,atomicpublish
0x02C	JOINT_5	R	0x00000000	score_raw\[31:24\],y\[23:12\],x\[11:0\]	invalid xy0,score signed8,atomicpublish
0x030	JOINT_6	R	0x00000000	score_raw\[31:24\],y\[23:12\],x\[11:0\]	invalid xy0,score signed8,atomicpublish
0x034	JOINT_7	R	0x00000000	score_raw\[31:24\],y\[23:12\],x\[11:0\]	invalid xy0,score signed8,atomicpublish
0x038	JOINT_8	R	0x00000000	score_raw\[31:24\],y\[23:12\],x\[11:0\]	invalid xy0,score signed8,atomicpublish
0x03C	JOINT_9	R	0x00000000	score_raw\[31:24\],y\[23:12\],x\[11:0\]	invalid xy0,score signed8,atomicpublish
0x040	JOINT_10	R	0x00000000	score_raw\[31:24\],y\[23:12\],x\[11:0\]	invalid xy0,score signed8,atomicpublish
0x044	JOINT_11	R	0x00000000	score_raw\[31:24\],y\[23:12\],x\[11:0\]	invalid xy0,score signed8,atomicpublish
0x048	JOINT_12	R	0x00000000	score_raw\[31:24\],y\[23:12\],x\[11:0\]	invalid xy0,score signed8,atomicpublish
0x04C	JOINT_13	R	0x00000000	score_raw\[31:24\],y\[23:12\],x\[11:0\]	invalid xy0,score signed8,atomicpublish
0x050	JOINT_14	R	0x00000000	score_raw\[31:24\],y\[23:12\],x\[11:0\]	invalid xy0,score signed8,atomicpublish
0x054	JOINT_15	R	0x00000000	score_raw\[31:24\],y\[23:12\],x\[11:0\]	invalid xy0,score signed8,atomicpublish
0x058	JOINT_16	R	0x00000000	score_raw\[31:24\],y\[23:12\],x\[11:0\]	invalid xy0,score signed8,atomicpublish
0x05C	VALID_FLAGS	R	0x00000000	\[16:0\]	score && bounds
0x060	RED_THRESH	RW	0x006464A0	Bmax23:16,Gmax15:8,Rmin7:0	idlewrite;frame latch
0x064	BLUE_THRESH	RW	0x00A06464	Bmin23:16,Gmax15:8,Rmax7:0	idlewrite;frame latch
0x068	THUMB_XY	R	0x00000000	found31,y20:11,x10:0	reserved30:21=0
0x06C	INDEX_XY	R	0x00000000	found31,y20:11,x10:0	reserved30:21=0
0x070	FRAME_ID	RW	0x00000000	\[31:0\]	PS sourceframeid;snapshotSTART
0x074	RESULT_SEQ	R	0x00000000	\[31:0\]	publish 마다+1 wrap32
0x078	ERROR_STATUS	RW1C	0x00000000	\[31:0\]	firsterrorbit0protocol,1DMAresp,2DMAstatus,3timeout,4param;clear 은복구후
0x07C	IRQ_ENABLE	RW	0x00000000	\[31:0\]	bit0 전체 levelIRQenable
0x080	CYCLE_COUNT	R	0x00000000	\[31:0\]	lastframe start-publish cycles
0x084	RESULT_FRAME_ID	R	0x00000000	\[31:0\]	결과 sourceid
0x088	MIN_COUNT	RW	0x00000008	\[31:0\]	1..184320
0x08C	WGT_BASE	RW	0x10000000	\[31:0\]	packedweights64aligned
0x090	FM_A_BASE	RW	0x11000000	\[31:0\]	1MiB 영역 64aligned
0x094	FM_B_BASE	RW	0x11100000	\[31:0\]	1MiB 영역 64aligned,src/dstoverlap 금지
0x098	SG_DESC_BASE	RW	0x11200000	\[31:0\]	144x64BD,64aligned
0x09C	FRAME_BASE	RW	0x0A000000	\[31:0\]	PS 검증/디버그용;실제 SGbuffer 주소와일치
0x0A0	TIMEOUT_CYCLES	RW	0x05F5E100	\[31:0\]	no-progresswatchdog;0 금지
0x0A4	VERSION	R	0x00040000	\[31:0\]	major4 minor0
0x0A8	CAPS	R	0x00000007	\[31:0\]	bit0offset16,bit1dilation2,bit2neutralconv0pad
0x0AC	MODEL_TAG	R	0xC9854BB2	\[31:0\]	packedSHA 상위 8hex 의 32bit 값

[레이턴시·reset·stall·종료 요구사항]
상태 순서는 IDLE→SNAPSHOT→ROM_FETCH→LOAD_W→CONFIG→ARM_DEST→ARM_SOURCE→RUN→WAIT_COMMIT→NEXT_STAGE→POSTPROCESS→PUBLISH→IDLE다. S2MM을 먼저 arm하고 MM2S/PE를 시작한다. frame 전체에 대한 고정 cycle latency는 없으며 DMA/stream stall을 포함한다.

ImageSG는 144개 64byte-aligned BD 유한 chain이다. DMA halted에서 CURDESC를 설정하고 RS1 뒤 마지막 BD를 TAILDESC에 쓴다. 552960byte, 144 rowlast, DMA IDLE과 stage0 downstream 완료를 확인한 뒤 IMAGE RS=0을 쓰고 Halted=1을 기다린다. 이때 IMAGE_READ_DONE을 공개하여 PS가 source frame을 release할 수 있다.

기본 timeout은 100000000cycle의 무진행 시간이다. payload accept, MAC issue 또는 실제 FSM 전진이면 누산을 0으로 되돌리며 동일 DMASR 반복 polling은 progress로 세지 않는다. abort에서 출력 valid를 즉시 취소하지 않고 오류로 격리한다. IRQ는 level이며 W1C clear와 새 event가 겹치면 새 event가 우선한다. 결과는 다음 publish까지 유지한다.

[반드시 그려 검증할 파형 시나리오]
1. START→snapshot→ROM/DW·PW load→cfg의 순서를 관찰하고 busy 중 재START/config write의 SLVERR를 확인한다.
2. AXI-Lite AW/W/AR/R/B에 서로 다른 stall을 넣어 독립 handshake, 한 개 transaction 및 S2MM 선행 arm을 확인한다.
3. stage0의 144 rowlast·bytecount·SG Idle·downstream 완료를 서로 다른 시점에 발생시켜 RS0/Halted 이후에만 IMAGE_READ_DONE이 올라오는지 본다.
4. heatmap→offset 및 body DDR write의 완료 pulse 순서를 바꾸어 stage 전진과 shadow17joint·색상 동시 publish/RESULT_SEQ를 확인한다.
5. 동일 DMASR polling만 반복하거나 fault/abort를 넣어 watchdog, sticky error, IRQ event 우선 및 PS DMA reset→SOFT_RESET 복구를 확인한다.

[수락 체크리스트]
- DW/PW 설정·가중치를 모두 준비한 뒤 body stage를 시작한다.
- 사용 경로의 stream/DMA/DDR 완료를 모두 모아 stage를 전진시킨다.
- 결과17joint·색상·frame_id를 같은 publish에 commit한다.
- AW/W 독립 수락과 B, AR/R 응답 및 RESP 오류를 검사한다.
- S2MM arm을 MM2S/PE보다 앞세우고 DMA 제어 ownership을 지킨다.
- busy/error START, DMA 오류, timeout 및 abort를 시험한다.
- 같은 status polling을 progress로 세지 않고 새 IRQ event를 W1C보다 우선한다.
- IMAGE_READ_DONE은 source 읽기와 stage0 완료 후 RS0/Halted까지 확인한다.
- 원문 레지스터 표와 wrapper/PS 계약을 대조한다.
- argmax와 FM의 완료 표현 확인 요청을 해결한 뒤 통합 종료 조건을 확정한다.
ARG-01(heatmap done 예외)과 FM-01(generic done/DMA flag 관계)은 연동 확인 대상으로 유지한다. 중앙 확정 전에 임의의 통합 종료 규칙으로 덮어쓰지 않는다.


[산출물]
1. top_level_fsm.v: 위 포트와 고정 파라미터를 사용한 합성 가능한 Verilog-2001 RTL.
2. tb_top_level_fsm.v: 위 기능·프로토콜·경계조건·통합 연결을 검사하는 self-checking Verilog-2001 테스트벤치.
3. 위 5개 시나리오 각각의 VCD 파형, 사람이 읽을 수 있는 타이밍 그림, 실행 명령과 가공하지 않은 전체 로그.
4. 검사 결과표: 예상값/실제값/판정, no-stall latency와 stall 복구, 포트 일치 여부. 산술 모듈은 정수 reference와 bit 단위 비교. 비산술 모듈은 주소·byte·태그·순서·완료 조건을 비교.
5. 합성 도구를 사용할 수 있으면 DSP/RAMB36/LUT 및 timing 결과. 도구·원본 모델이 없으면 해당 항목을 미검증으로 표시하고 측정값을 추정으로 대신하지 않는다.
완료 기준은 테스트 통과와 명세 일치다. 아래 템플릿의 TODO를 남겨 둔 상태를 구현 완료라고 보고하지 마라.

[확인 요청 처리]
외부 포트/타이밍 충돌, 원본 실행 대조 오류, 전체 설계에 영향을 주는 모호함, 공용 자원 예산 변경은 기초 설계 AI에 확인 요청한다.
영향 없는 사소한 내부 구현 선택은 담당자가 결정할 수 있다. 중앙 확정 전 문제 부분을 보류하거나 "미확정" 잠정 구현으로 분리한다.
다음 형식으로 기초 설계 AI에게 전달할 확인 요청 보고서를 작성하라.
주장과 확인된 사실을 구분하고, 증거가 없으면 "미확인"으로 표시하라.
정해지지 않은 사양을 확정값으로 구현하거나 다른 페이지를 자동 수정하지 마라.

🚨 이슈 [번호] — top_level_fsm: [구체적 제목]
1. 요청 정보
   - 모듈 / 담당 / 기준 버전: top_level_fsm / [담당자] / CNN-v4.0
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
module top_level_fsm (
    input wire clk,
    input wire rst_n,
    output wire input_cfg_valid,
    input wire input_cfg_ready,
    output wire [255:0] input_cfg_desc,
    input wire input_done,
    input wire input_fault,
    output wire line_cfg_valid,
    input wire line_cfg_ready,
    output wire [255:0] line_cfg_desc,
    input wire line_done,
    input wire line_fault,
    output wire dw_cfg_valid,
    input wire dw_cfg_ready,
    output wire [255:0] dw_cfg_desc,
    input wire dw_done,
    input wire dw_fault,
    output wire pw_cfg_valid,
    input wire pw_cfg_ready,
    output wire [255:0] pw_cfg_desc,
    input wire pw_done,
    input wire pw_fault,
    output wire fm_cfg_valid,
    input wire fm_cfg_ready,
    output wire [255:0] fm_cfg_desc,
    input wire fm_done,
    input wire fm_fault,
    output wire [31:0] fm_src_addr,
    output wire [31:0] fm_dst_addr,
    output wire [19:0] fm_src_bytes,
    output wire [19:0] fm_dst_bytes,
    output wire fm_read_en,
    output wire fm_write_en,
    output wire fm_read_dma_done,
    output wire fm_write_dma_done,
    output wire fm_dma_error,
    input wire fm_read_done,
    input wire fm_write_done,
    output wire down_cfg_valid,
    input wire down_cfg_ready,
    output wire [255:0] down_cfg_desc,
    input wire down_done,
    input wire down_fault,
    output wire arg_cfg_valid,
    input wire arg_cfg_ready,
    output wire [255:0] arg_cfg_desc,
    input wire arg_done,
    input wire arg_fault,
    output wire coord_cfg_valid,
    input wire coord_cfg_ready,
    output wire [255:0] coord_cfg_desc,
    input wire coord_done,
    input wire coord_fault,
    input wire [31:0] coord_m_joint_data,
    input wire coord_m_joint_valid,
    output wire coord_m_joint_ready,
    input wire coord_m_joint_last,
    input wire [4:0] coord_m_joint_index,
    input wire coord_m_joint_good,
    output wire [7:0] coord_threshold,
    input wire swap_fault,
    output wire swap_load_valid,
    input wire swap_load_ready,
    output wire [255:0] swap_load_desc,
    input wire swap_load_done,
    input wire color_fault,
    output wire color_frame_start,
    output wire [23:0] color_red_cfg,
    output wire [23:0] color_blue_cfg,
    output wire [17:0] color_min_count,
    input wire [31:0] color_red_word,
    input wire [31:0] color_blue_word,
    input wire color_results_valid,
    input wire rom_fault,
    output wire rom_req_valid,
    input wire rom_req_ready,
    output wire [4:0] rom_req_op,
    input wire rom_rsp_valid,
    output wire rom_rsp_ready,
    input wire [255:0] rom_rsp_desc,
    input wire [7:0] threshold_cfg,
    input wire [23:0] red_thresh_cfg,
    input wire [23:0] blue_thresh_cfg,
    input wire [17:0] min_count_cfg,
    input wire datapath_progress,
    input wire start,
    input wire clear_done,
    input wire clear_error,
    input wire [31:0] frame_id,
    input wire [31:0] sg_desc_base,
    input wire [31:0] wgt_base,
    input wire [31:0] fm_a_base,
    input wire [31:0] fm_b_base,
    input wire [31:0] timeout_cycles,
    output wire busy,
    output wire done_pending,
    output wire error_pending,
    output wire [31:0] error_code,
    output wire [31:0] result_seq,
    output wire [31:0] result_frame_id,
    output wire [543:0] joint_words,
    output wire [16:0] joint_flags,
    output wire [31:0] red_word,
    output wire [31:0] blue_word,
    output wire [31:0] cycle_count,
    output wire image_read_done,
    output wire [31:0] m_axil_awaddr,
    output wire m_axil_awvalid,
    input wire m_axil_awready,
    output wire [2:0] m_axil_awprot,
    output wire [31:0] m_axil_wdata,
    output wire [3:0] m_axil_wstrb,
    output wire m_axil_wvalid,
    input wire m_axil_wready,
    input wire [1:0] m_axil_bresp,
    input wire m_axil_bvalid,
    output wire m_axil_bready,
    output wire [31:0] m_axil_araddr,
    output wire m_axil_arvalid,
    input wire m_axil_arready,
    output wire [2:0] m_axil_arprot,
    input wire [31:0] m_axil_rdata,
    input wire [1:0] m_axil_rresp,
    input wire m_axil_rvalid,
    output wire m_axil_rready
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

🚨 이슈 [번호] — top_level_fsm: [구체적 제목]
1. 요청 정보
   - 모듈 / 담당 / 기준 버전: top_level_fsm / [담당자] / CNN-v4.0
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
- 파일명에 모듈과 이슈를 명시한다. 예: `top_level_fsm_ISSUE01_raw.log`, `top_level_fsm_ISSUE01_full_source.py`, `top_level_fsm_ISSUE01_proposal.md`.
## 중앙 처리와 구현 상태
중앙은 근거 재검증→전체 영향 확인→방안 확정→관련 페이지·XLSX·공용 규칙 동시 반영을 수행한다. 상세 절차: <mention-page url="https://app.notion.com/p/3d9e5946183b81d7be5cfb25ec79657c"/>.
처리가 끝나기 전 해당 부분은 보류하거나 "미확정"으로 표시한 잠정 구현으로 분리한다. 영향 없는 부분은 계속 진행할 수 있다. 로컬 FSM 인코딩처럼 외부 계약에 영향 없는 선택은 담당자가 결정하고 기록한다.
