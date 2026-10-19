공용 기준: <mention-page url="https://app.notion.com/p/3d9e5946183b81d7be5cfb25ec79657c"/> · 통합/레지스터 계약: <mention-page url="https://app.notion.com/p/3d9e5946183b81d59925ed678e8fb87a"/>
담당: **유지민**. 버전 **CNN-v4.0**. 기존 문서의 상충하는 고정 latency/폭/연결 규칙을 이 페이지로 대체한다. 실제 RTL 작성·합성·실기검증 완료 상태를 뜻하지 않는다.
# 기능
`downsample_module`이 만든 RGB888 스트림에 첫 번째 3×3 convolution을 적용하여 128×128×24 body INT8을 생성한다. 결과는 `feature_map_io`를 거쳐 DDR에 저장한다. 다음 레이어와 곧바로 연결할 때 생기는 가중치 준비 교착을 피하기 위한 단계 분리다.
모듈 내부의 전용 RGB 윈도우 생성기는 두 개의 256×24 RAMB18 행 지연기를 사용한다. 두 RAM은 공통 클럭 READ_FIRST로 동작하며, 이전 행 출력과 현재 픽셀·태그를 지연시켜 세 행을 정렬한다. 행마다 3열 shift register는 실제 pixel accept에서만 이동한다. BRAM 예산은 RAMB36 환산 1개다.
이미지 내부 letterbox 검정은 raw 0이고, convolution의 이미지 바깥 패딩은 중립값 127.5에 해당한다. 이를 정수로 처리하기 위해 정상 탭에는 `2*pixel`, 바깥 탭에는 255를 사용한다. signed10×signed8 곱 27개에 `bias_accum*2`를 더하고 signed25 누산값을 재양자화한다. M은 unsigned17을 signed18로 zero extend하며, RNE shift17 후 \[0,127\]로 포화시킨다.
출력 중심은 입력의 `(2*oy,2*ox)`다. 출력에 필요한 미래 행·열을 확보한 뒤 해당 위치의 24개 채널을 계산한다. 채널별 가중치는 32byte 레코드의 앞 27byte이며, `byte(3*(3*ky+kx)+rgb)` 순서다. 결과 24byte를 보관한 뒤 4채널씩 6beat로 내보낸다. MAC 27개와 requant 1개에 총 28 DSP를 배정한다.
<callout icon="🔄" color="blue_bg">
	**결정**: conv1 직결과 1728bit 전체 가중치 입력을 폐기하고, DDR 단계 분리와 채널별 가중치 요청 구조를 유지한다.
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
<td>s_rgb_data</td>
<td>in</td>
<td>24</td>
<td>downsample</td>
<td>RGB R\[7:0\],G\[15:8\],B\[23:16\] 해당 유효 신호와 같은 cycle에 전달하며 명세의 byte 순서·부호·lane 배치를 보존한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>s_rgb_valid</td>
<td>in</td>
<td>1</td>
<td>downsample</td>
<td>payload 유효; ready 와 무관하게 assert 가능 대응 payload가 유효함을 표시한다. 수신 ready를 기다리는 동안 payload를 바꾸지 않는다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>s_rgb_ready</td>
<td>out</td>
<td>1</td>
<td>downsample</td>
<td>수신 여유 대응 valid와 동시에 1인 상승 에지에서만 payload를 수락한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>s_rgb_tag</td>
<td>in</td>
<td>64</td>
<td>downsample</td>
<td>공용 TAG64; 데이터와 같은 cycle 에 이동 현재 payload의 좌표·batch/group·종료 정보를 같은 handshake로 전달한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>m_body_data</td>
<td>out</td>
<td>64</td>
<td>feature_map_io</td>
<td>4lane zero-extended INT16 body 해당 유효 신호와 같은 cycle에 전달하며 명세의 byte 순서·부호·lane 배치를 보존한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>m_body_valid</td>
<td>out</td>
<td>1</td>
<td>feature_map_io</td>
<td>payload 유효; ready 와 무관하게 assert 가능 대응 payload가 유효함을 표시한다. 수신 ready를 기다리는 동안 payload를 바꾸지 않는다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>m_body_ready</td>
<td>in</td>
<td>1</td>
<td>feature_map_io</td>
<td>수신 여유 대응 valid와 동시에 1인 상승 에지에서만 payload를 수락한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>m_body_mask</td>
<td>out</td>
<td>4</td>
<td>feature_map_io</td>
<td>lane 별 유효. bit0=lane0. 미사용 data=0 현재 beat의 실제 채널만 처리하며 mask가 0인 lane을 누산하거나 저장하지 않는다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>m_body_tag</td>
<td>out</td>
<td>64</td>
<td>feature_map_io</td>
<td>공용 TAG64; 데이터와 같은 cycle 에 이동 현재 payload의 좌표·batch/group·종료 정보를 같은 handshake로 전달한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>conv0_req_valid</td>
<td>out</td>
<td>1</td>
<td>weight_bram_swap_fsm</td>
<td>읽기요청 대응 payload가 유효함을 표시한다. 수신 ready를 기다리는 동안 payload를 바꾸지 않는다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>conv0_req_ready</td>
<td>in</td>
<td>1</td>
<td>weight_bram_swap_fsm</td>
<td>요청수락 대응 valid와 동시에 1인 상승 에지에서만 payload를 수락한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>conv0_req_addr</td>
<td>out</td>
<td>5</td>
<td>weight_bram_swap_fsm</td>
<td>record index 출력채널 0..23의 32byte weight record를 지정한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>conv0_rsp_valid</td>
<td>in</td>
<td>1</td>
<td>weight_bram_swap_fsm</td>
<td>1cycle 후 response 대응 payload가 유효함을 표시한다. 수신 ready를 기다리는 동안 payload를 바꾸지 않는다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>conv0_rsp_ready</td>
<td>out</td>
<td>1</td>
<td>weight_bram_swap_fsm</td>
<td>response 수락 대응 valid와 동시에 1인 상승 에지에서만 payload를 수락한다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>conv0_rsp_data</td>
<td>in</td>
<td>216</td>
<td>weight_bram_swap_fsm</td>
<td>weight 또는 param 해당 출력채널의 RGB 3×3 weight 27byte를 반환하며 저장 레코드의 padding 5byte는 포트에 포함하지 않는다.</td>
<td>valid &amp;&amp; ready 에서 수락; stall 동안 payload 유지</td>
<td>0</td>
</tr>
<tr>
<td>conv0_rsp_params</td>
<td>in</td>
<td>64</td>
<td>weight_bram_swap_fsm</td>
<td>bias/M64bit peroutput 해당 출력채널의 LE64 bias_accum/M 레코드를 weight와 함께 전달한다.</td>
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
<td>출력 TAG의 op_id에 대응</td>
</tr>
<tr>
<td>kind</td>
<td>6:5</td>
<td>연산 종류: 0=conv0, 1=DW, 2=PW</td>
<td>conv0 연산 설정</td>
</tr>
<tr>
<td>mode</td>
<td>8:7</td>
<td>출력 mode: 0=body, 1=heatmap, 2=offset</td>
<td>body 출력 설정</td>
</tr>
<tr>
<td>hin</td>
<td>17:9</td>
<td>입력 tensor 높이</td>
<td>256행 입력 윈도우 경계</td>
</tr>
<tr>
<td>win</td>
<td>26:18</td>
<td>입력 tensor 너비</td>
<td>256열 입력 윈도우 경계</td>
</tr>
<tr>
<td>hout</td>
<td>35:27</td>
<td>출력 tensor 높이</td>
<td>128행 출력 진행/종료</td>
</tr>
<tr>
<td>wout</td>
<td>44:36</td>
<td>출력 tensor 너비</td>
<td>128열 출력 진행/종료</td>
</tr>
<tr>
<td>cin</td>
<td>53:45</td>
<td>입력 채널 수</td>
<td>RGB 3채널 고정 입력</td>
</tr>
<tr>
<td>cout</td>
<td>62:54</td>
<td>출력 채널 수</td>
<td>24채널 계산 및 4lane×6group 출력</td>
</tr>
<tr>
<td>stride</td>
<td>64:63</td>
<td>convolution stride</td>
<td>conv0 고정 geometry와 대응: stride2/dilation1/pad1</td>
</tr>
<tr>
<td>dilation</td>
<td>66:65</td>
<td>convolution dilation</td>
<td>conv0 고정 geometry와 대응: stride2/dilation1/pad1</td>
</tr>
<tr>
<td>pad</td>
<td>68:67</td>
<td>convolution padding</td>
<td>conv0 고정 geometry와 대응: stride2/dilation1/pad1</td>
</tr>
<tr>
<td>shift</td>
<td>74:69</td>
<td>재양자화 right shift</td>
<td>conv0 doubled 연산의 shift17</td>
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
<td>이 모듈 본문에 직접 사용하는 동작 없음; 소유 모듈에서 처리</td>
</tr>
<tr>
<td>reserved</td>
<td>255:149</td>
<td>항상 0</td>
<td>재정의 금지</td>
</tr>
</table>
# 타이밍 요구사항
상태 순서는 IDLE→FILL→WINDOW→ISSUE_CH→DRAIN→EMIT_GROUP→ADVANCE→DONE이다. 메모리 응답 1cycle 뒤 계산 10stage(곱1+adder tree5+bias1+M 곱1+RNE1+clip1)를 거친다. 출력채널 issue II=1이며, 준비된 출력 위치당 보수적 상한은 48 enabled cycles다. 메모리·출력 stall 시간은 이 상한에서 제외한다. output stall에서는 그룹과 윈도우를 보존하고 입력 ready를 내린다. 마지막 위치·그룹은 TAG64의 frame_end로 표시하고 마지막 출력 accept를 기준으로 완료한다.
reset은 공용 100MHz 상승 에지 기준 동기 active-low이며 rst_n을 4cycle 이상 assert한다. RAM 전체 clear는 하지 않는다. valid/ready 포트는 stall 동안 payload를 유지하고 accept를 기준으로 진행한다. cfg 포트가 있는 모듈은 idle에서 cfg를 수락한다. 포트가 없는 별도 frame_start/reset/ready를 추가하지 않는다.
## 필수 파형 시나리오
1. cfg 수락과 reset 해제부터 첫 윈도우가 준비될 때까지: 행 RAM 지연과 RGB/태그 정렬을 함께 관찰한다.
2. 상·하·좌·우 바깥 패딩과 내부 letterbox: 정상 탭 2\*pixel, 바깥 탭 255, 내부 검정 0이 구분되는지 확인한다.
3. 연속 출력채널 issue: 메모리 응답 1cycle, 계산 10stage, 24채널 수집과 6개 출력 그룹의 순서를 확인한다.
4. 마지막 출력 그룹에 backpressure를 걸었다 해제한다. data/mask/tag와 윈도우가 유지되고 마지막 accept 전에는 done이 발생하지 않아야 한다.
# 체크리스트
### 기능 정확성
- [ ] 바깥 패딩 255와 내부 letterbox 0을 구별한다.
- [ ] 27개 signed 곱, doubled bias, signed25, RNE shift17 및 \[0,127\] 포화를 검사한다.
### 인터페이스 프로토콜
- [ ] RGB byte 순서와 채널별 27byte 응답을 맞춘다.
- [ ] stall 중 입력/출력 그룹을 중복 소비하지 않는다.
### 경계조건
- [ ] 입력 255, 음수 weight, 모서리 및 마지막 그룹을 포함한다.
- [ ] 24byte 결과 캐시와 RAMB18 두 개의 행 정렬을 확인한다.
### 통합 검증
- [ ] conv_acc의 conv0 분기와 결과를 대조한다.
- [ ] feature_map_io로 6beat를 전달하고 28 DSP 배정과 행 RAM 구조를 확인한다.
- [ ] reset·sticky fault·새 작업 차단 및 복구가 공용 계약과 일치한다.
# AI 에이전트용 프롬프트
아래 블록 전체를 복사한다. 공용 규칙·모듈 세부 계약·전체 포트·검증 시나리오를 포함한다.
```plain text
CNN_3D Hand Interaction Platform의 input_conv_pe 모듈을 CNN-v4.0 계약에 맞게 구현하라.
담당: 유지민. 대상 Zybo Z7-20 / XC7Z020. 언어는 Verilog-2001 .v만 허용한다.
SystemVerilog의 logic, always_ff, always_comb, typedef, enum, interface, package 등 문법은 금지한다.
아래 내용으로 RTL과 self-checking Verilog-2001 테스트벤치를 작성하라. 다른 Notion 페이지를 읽어야만 알 수 있는 계약을 가정하지 마라.
신호명·방향·폭·필드 위치·산술·DSP/BRAM 숫자를 바꾸거나 포트를 추가하지 마라.
데이터/가중치 및 실제 checkpoint 검증에는 원본 파일이 필요하다. 제공되지 않은 자료나 검증 결과를 만들어내지 마라.

[역할과 설계 배경]
`downsample_module`이 만든 RGB888 스트림에 첫 번째 3×3 convolution을 적용하여 128×128×24 body INT8을 생성한다. 결과는 `feature_map_io`를 거쳐 DDR에 저장한다. 다음 레이어와 곧바로 연결할 때 생기는 가중치 준비 교착을 피하기 위한 단계 분리다.

모듈 내부의 전용 RGB 윈도우 생성기는 두 개의 256×24 RAMB18 행 지연기를 사용한다. 두 RAM은 공통 클럭 READ_FIRST로 동작하며, 이전 행 출력과 현재 픽셀·태그를 지연시켜 세 행을 정렬한다. 행마다 3열 shift register는 실제 pixel accept에서만 이동한다. BRAM 예산은 RAMB36 환산 1개다.

이미지 내부 letterbox 검정은 raw 0이고, convolution의 이미지 바깥 패딩은 중립값 127.5에 해당한다. 이를 정수로 처리하기 위해 정상 탭에는 `2*pixel`, 바깥 탭에는 255를 사용한다. signed10×signed8 곱 27개에 `bias_accum*2`를 더하고 signed25 누산값을 재양자화한다. M은 unsigned17을 signed18로 zero extend하며, RNE shift17 후 [0,127]로 포화시킨다.

출력 중심은 입력의 `(2*oy,2*ox)`다. 출력에 필요한 미래 행·열을 확보한 뒤 해당 위치의 24개 채널을 계산한다. 채널별 가중치는 32byte 레코드의 앞 27byte이며, `byte(3*(3*ky+kx)+rgb)` 순서다. 결과 24byte를 보관한 뒤 4채널씩 6beat로 내보낸다. MAC 27개와 requant 1개에 총 28 DSP를 배정한다.

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
op_id	4:0	operation 번호 0..28	출력 TAG의 op_id에 대응
kind	6:5	연산 종류: 0=conv0, 1=DW, 2=PW	conv0 연산 설정
mode	8:7	출력 mode: 0=body, 1=heatmap, 2=offset	body 출력 설정
hin	17:9	입력 tensor 높이	256행 입력 윈도우 경계
win	26:18	입력 tensor 너비	256열 입력 윈도우 경계
hout	35:27	출력 tensor 높이	128행 출력 진행/종료
wout	44:36	출력 tensor 너비	128열 출력 진행/종료
cin	53:45	입력 채널 수	RGB 3채널 고정 입력
cout	62:54	출력 채널 수	24채널 계산 및 4lane×6group 출력
stride	64:63	convolution stride	conv0 고정 geometry와 대응: stride2/dilation1/pad1
dilation	66:65	convolution dilation	conv0 고정 geometry와 대응: stride2/dilation1/pad1
pad	68:67	convolution padding	conv0 고정 geometry와 대응: stride2/dilation1/pad1
shift	74:69	재양자화 right shift	conv0 doubled 연산의 shift17
weight_offset	106:75	packed weights 시작 byte offset	이 모듈 본문에 직접 사용하는 동작 없음; 소유 모듈에서 처리
param_offset	124:107	operation payload 내 param 시작 byte offset	이 모듈 본문에 직접 사용하는 동작 없음; 소유 모듈에서 처리
dma_bytes	144:125	정렬을 포함한 operation DMA byte 수	이 모듈 본문에 직접 사용하는 동작 없음; 소유 모듈에서 처리
stage_id	148:145	stage 번호 0..15	이 모듈 본문에 직접 사용하는 동작 없음; 소유 모듈에서 처리
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
s_rgb_data	in	24	downsample	RGB R[7:0],G[15:8],B[23:16] 해당 유효 신호와 같은 cycle에 전달하며 명세의 byte 순서·부호·lane 배치를 보존한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
s_rgb_valid	in	1	downsample	payload 유효; ready 와 무관하게 assert 가능 대응 payload가 유효함을 표시한다. 수신 ready를 기다리는 동안 payload를 바꾸지 않는다.	valid && ready 에서 수락; stall 동안 payload 유지	0
s_rgb_ready	out	1	downsample	수신 여유 대응 valid와 동시에 1인 상승 에지에서만 payload를 수락한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
s_rgb_tag	in	64	downsample	공용 TAG64; 데이터와 같은 cycle 에 이동 현재 payload의 좌표·batch/group·종료 정보를 같은 handshake로 전달한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
m_body_data	out	64	feature_map_io	4lane zero-extended INT16 body 해당 유효 신호와 같은 cycle에 전달하며 명세의 byte 순서·부호·lane 배치를 보존한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
m_body_valid	out	1	feature_map_io	payload 유효; ready 와 무관하게 assert 가능 대응 payload가 유효함을 표시한다. 수신 ready를 기다리는 동안 payload를 바꾸지 않는다.	valid && ready 에서 수락; stall 동안 payload 유지	0
m_body_ready	in	1	feature_map_io	수신 여유 대응 valid와 동시에 1인 상승 에지에서만 payload를 수락한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
m_body_mask	out	4	feature_map_io	lane 별 유효. bit0=lane0. 미사용 data=0 현재 beat의 실제 채널만 처리하며 mask가 0인 lane을 누산하거나 저장하지 않는다.	valid && ready 에서 수락; stall 동안 payload 유지	0
m_body_tag	out	64	feature_map_io	공용 TAG64; 데이터와 같은 cycle 에 이동 현재 payload의 좌표·batch/group·종료 정보를 같은 handshake로 전달한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
conv0_req_valid	out	1	weight_bram_swap_fsm	읽기요청 대응 payload가 유효함을 표시한다. 수신 ready를 기다리는 동안 payload를 바꾸지 않는다.	valid && ready 에서 수락; stall 동안 payload 유지	0
conv0_req_ready	in	1	weight_bram_swap_fsm	요청수락 대응 valid와 동시에 1인 상승 에지에서만 payload를 수락한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
conv0_req_addr	out	5	weight_bram_swap_fsm	record index 출력채널 0..23의 32byte weight record를 지정한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
conv0_rsp_valid	in	1	weight_bram_swap_fsm	1cycle 후 response 대응 payload가 유효함을 표시한다. 수신 ready를 기다리는 동안 payload를 바꾸지 않는다.	valid && ready 에서 수락; stall 동안 payload 유지	0
conv0_rsp_ready	out	1	weight_bram_swap_fsm	response 수락 대응 valid와 동시에 1인 상승 에지에서만 payload를 수락한다.	valid && ready 에서 수락; stall 동안 payload 유지	0
conv0_rsp_data	in	216	weight_bram_swap_fsm	weight 또는 param 해당 출력채널의 RGB 3×3 weight 27byte를 반환하며 저장 레코드의 padding 5byte는 포트에 포함하지 않는다.	valid && ready 에서 수락; stall 동안 payload 유지	0
conv0_rsp_params	in	64	weight_bram_swap_fsm	bias/M64bit peroutput 해당 출력채널의 LE64 bias_accum/M 레코드를 weight와 함께 전달한다.	valid && ready 에서 수락; stall 동안 payload 유지	0

[모듈 세부 계약 전문: 기존 CNN-v4.0 기술 조건 보존]
## 역할과 연결
downsample RGB24 를 받아 128×128×24 body INT8 를 생성한다. conv0 결과는 feature_map_io 로 DDR 에 저장한다. conv1 직결을 제거하여 가중치 준비 교착을 없앤다.
## 수치와 메모리
전용 3×3 RGB 윈도우 생성기를 포함한다. 두 개의 256×24 simple-dual-port RAMB18 을 행 지연기로 사용하며 합계 1 RAMB36 환산이다. 각 RAM 은 공통 클럭 READ_FIRST 로 읽기/쓰기를 수행한다. 첫 RAM 의 이전 행 출력을 1cycle 뒤 두 번째 RAM 에 기록하고 현재 RGB 와 태그를 같은 수만큼 지연시켜 세 행을 정렬한다. 각 행의 3 열 shift register 는 pixel accept 에서만 이동한다. 두 행을 한 RAM 에 넣고 한 cycle 에 2read+1write 를 요구하지 않는다. CNN 이미지 내부의 letterbox 검정은 raw 0 이다. 이미지 바깥 convolution pad 는 원본 127.5 와 같은 중립값을 사용한다. 각 MAC 의 입력은 정상 탭이면 2*pixel, 바깥 탭이면 255 이다. signed10×signed8 곱 27 개를 사용하고 bias_accum*2 를 더한다. 누적은 signed25, M 은 unsigned17 을 signed18 로 zero extend 한다. RNE shift17 뒤 [0,127] saturation. 이것이 cnn_model.conv_acc 의 conv0 분기다.
가중치 한 출력채널=27bytes; req_ch=0..23 로 읽는다. w_rsp_data byte(3*(3*ky+kx)+rgb)에 대응한다. 실제 저장 레코드는 32bytes 이며 뒤 5bytes 는 0. 프레임 전체 가중치를 1728bit 로 받는 구 인터페이스 폐기.
## 상태와 타이밍
IDLE→FILL→WINDOW→ISSUE_CH→DRAIN→EMIT_GROUP→ADVANCE→DONE. 유효 출력 위치의 입력 중심은 (2*oy,2*ox)다. 필요한 미래 행/열이 들어올 때까지 기다린다. 출력 위치에서만 24 개 채널을 계산하고 나머지 입력은 윈도우 저장에만 쓴다. 27 곱 등록 1+27 항 adder tree5+bias1+M 곱 1+RNE1+clip1=10 개 계산 stage, 메모리 응답 1 을 앞에 둔다. 한 출력채널 issue II=1. 24 개 결과를 수집한 뒤 4 채널 그룹 6beat 를 출력한다. output stall 이면 그룹/윈도우를 보존하고 입력 ready 를 내린다. 보수적 상한은 준비된 출력 위치당 48 enabled cycles(메모리/출력 stall 제외)로 잡는다. 단일 입력 pixel accept→항상 24cycle output 이라는 구 규칙은 폐기.
## 리소스 상한과 시험
MAC27+requant1=28 DSP. output cache24bytes 는 레지스터. 윈도우의 상/하/좌/우 패딩, 입력 255, negative weight, RGB 순서, 마지막 output group stall 을 테스트한다. output tag row/col=출력좌표, group=0..5, 마지막 위치/group 에 frame_end=1. 출력 group 완료는 ready handshake 기준이다.

[레이턴시·reset·stall·종료 요구사항]
상태 순서는 IDLE→FILL→WINDOW→ISSUE_CH→DRAIN→EMIT_GROUP→ADVANCE→DONE이다. 메모리 응답 1cycle 뒤 계산 10stage(곱1+adder tree5+bias1+M 곱1+RNE1+clip1)를 거친다. 출력채널 issue II=1이며, 준비된 출력 위치당 보수적 상한은 48 enabled cycles다. 메모리·출력 stall 시간은 이 상한에서 제외한다. output stall에서는 그룹과 윈도우를 보존하고 입력 ready를 내린다. 마지막 위치·그룹은 TAG64의 frame_end로 표시하고 마지막 출력 accept를 기준으로 완료한다.

[반드시 그려 검증할 파형 시나리오]
1. cfg 수락과 reset 해제부터 첫 윈도우가 준비될 때까지: 행 RAM 지연과 RGB/태그 정렬을 함께 관찰한다.
2. 상·하·좌·우 바깥 패딩과 내부 letterbox: 정상 탭 2*pixel, 바깥 탭 255, 내부 검정 0이 구분되는지 확인한다.
3. 연속 출력채널 issue: 메모리 응답 1cycle, 계산 10stage, 24채널 수집과 6개 출력 그룹의 순서를 확인한다.
4. 마지막 출력 그룹에 backpressure를 걸었다 해제한다. data/mask/tag와 윈도우가 유지되고 마지막 accept 전에는 done이 발생하지 않아야 한다.

[수락 체크리스트]
- 바깥 패딩 255와 내부 letterbox 0을 구별한다.
- 27개 signed 곱, doubled bias, signed25, RNE shift17 및 [0,127] 포화를 검사한다.
- RGB byte 순서와 채널별 27byte 응답을 맞춘다.
- stall 중 입력/출력 그룹을 중복 소비하지 않는다.
- 입력 255, 음수 weight, 모서리 및 마지막 그룹을 포함한다.
- 24byte 결과 캐시와 RAMB18 두 개의 행 정렬을 확인한다.
- conv_acc의 conv0 분기와 결과를 대조한다.
- feature_map_io로 6beat를 전달하고 28 DSP 배정과 행 RAM 구조를 확인한다.

[산출물]
1. input_conv_pe.v: 위 포트와 고정 파라미터를 사용한 합성 가능한 Verilog-2001 RTL.
2. tb_input_conv_pe.v: 위 기능·프로토콜·경계조건·통합 연결을 검사하는 self-checking Verilog-2001 테스트벤치.
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

🚨 이슈 [번호] — input_conv_pe: [구체적 제목]
1. 요청 정보
   - 모듈 / 담당 / 기준 버전: input_conv_pe / [담당자] / CNN-v4.0
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
module input_conv_pe (
    input wire clk,
    input wire rst_n,
    input wire cfg_valid,
    output wire cfg_ready,
    input wire [255:0] cfg_desc,
    output wire done,
    output wire fault,
    input wire [23:0] s_rgb_data,
    input wire s_rgb_valid,
    output wire s_rgb_ready,
    input wire [63:0] s_rgb_tag,
    output wire [63:0] m_body_data,
    output wire m_body_valid,
    input wire m_body_ready,
    output wire [3:0] m_body_mask,
    output wire [63:0] m_body_tag,
    output wire conv0_req_valid,
    input wire conv0_req_ready,
    output wire [4:0] conv0_req_addr,
    input wire conv0_rsp_valid,
    output wire conv0_rsp_ready,
    input wire [215:0] conv0_rsp_data,
    input wire [63:0] conv0_rsp_params
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

🚨 이슈 [번호] — input_conv_pe: [구체적 제목]
1. 요청 정보
   - 모듈 / 담당 / 기준 버전: input_conv_pe / [담당자] / CNN-v4.0
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
- 파일명에 모듈과 이슈를 명시한다. 예: `input_conv_pe_ISSUE01_raw.log`, `input_conv_pe_ISSUE01_full_source.py`, `input_conv_pe_ISSUE01_proposal.md`.
## 중앙 처리와 구현 상태
중앙은 근거 재검증→전체 영향 확인→방안 확정→관련 페이지·XLSX·공용 규칙 동시 반영을 수행한다. 상세 절차: <mention-page url="https://app.notion.com/p/3d9e5946183b81d7be5cfb25ec79657c"/>.
처리가 끝나기 전 해당 부분은 보류하거나 "미확정"으로 표시한 잠정 구현으로 분리한다. 영향 없는 부분은 계속 진행할 수 있다. 로컬 FSM 인코딩처럼 외부 계약에 영향 없는 선택은 담당자가 결정하고 기록한다.
