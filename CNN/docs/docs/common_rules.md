## 적용 기준 CNN-v4.0
이 페이지와 generated/interface.json, manifest.json 은 같은 버전이다. RTL 은 Verilog-2001(.v), 검증은 Python 정수 reference 를 사용한다. 클럭은 100MHz, reset 은 동기 active-low rst_n 이다. 자원 배정은 합성 전 예산이며 전체 SoC 합성과 timing 결과로 승인한다.
입력은 1280×720 RGB888 을 256×256 으로 전처리한다. body 는 0..127 을 INT8 로 저장하고 weight 는 signed INT8, heatmap 은 signed INT8, offset 은 signed INT16 이다. 출력채널별 M 은 0..131071 이다. bias_accum=RNE(bias_int8\*bias_M/65536)을 PC pack 단계에서 계산하며 signed24 범위를 검사한다. body 누산기는 signed24, conv0 의 doubled 누산기는 signed25 다. 재양자화는 signed25 × signed18 → signed43 전체 곱을 보존한 뒤 RNE ties-to-even 을 적용한다. body/heat/offset 의 saturation 범위는 각각 \[0,127\], \[-128,127\], \[-32768,32767\]이다. Verilog part-select 와 concat 의 signedness 는 명시적으로 변환한다.
RNE 구현식은 q=p\>\>\>N, r=p-(q\<\<N), h=1\<\<(N-1), result=q+((r\>h)\|\|((r==h)&&(q&1)))이다. N=0 이면 p 를 그대로 사용한다. 음수에서도 q 는 floor 방향이고 r 은 0..2\^N-1 이다. 곱의 하위 비트를 먼저 버리거나 양수용 반올림 상수를 더하지 않는다.
valid 는 ready 와 무관하게 assert 할 수 있어야 한다. valid && !ready 동안 data/mask/tag/last 를 유지한다. counter 는 accept 또는 MAC issue 에서만 증가한다. cfg 는 idle 에서 수락하고 완료까지 래치한다. done 은 모듈별 최종 transfer 수락 후 1cycle pulse 이며 top 이 기억한다. 오류는 sticky fault 로 남기고 새 작업을 차단한다. PS 가 DMA 를 reset 한 뒤 core SOFT_RESET 한다. 데이터 RAM 전체를 reset 하지 않는다.
TAG64: col\[7:0\], row\[15:8\], batch\[19:16\], group\[26:20\], tap\[30:27\], batch_last\[31\], pixel_last\[32\], op_id\[37:33\], frame_end\[38\], group_last\[39\], reserved\[63:40\]=0. row/col 은 생산자가 내보내는 tensor 의 좌표다. line 입력은 입력 좌표, line 출력은 convolution 출력 좌표다. pixel_last 는 현재 pixel 의 마지막 beat, frame_end 는 현재 operation 의 최종 pixel 최종 beat 에만 1 이다. group_last 는 마지막 출력 group 이다. 미사용 tag 필드는 0 이며 lane0 은 최하위 byte/bit 다.
입출력 tensor 는 HWC, row→col→channel 순서다. cfg_desc256 의 \[255:149\]는 0 이다. mux 선택은 top 이 cfg 전에 고정하고 선택되지 않은 입력의 ready 는 0 이다. conv0 는 dilation=1, pad=1, stride=2 로 고정한다. DW 는 dilation 1/2 와 stride 1/2 를 지원한다. 제공 manifest 의 d1 은 구현 baseline 이며 원본 체크포인트 확인값이 아니다. export_[geometry.py](http://geometry.py) 로 실제 모델의 geometry 를 확인해야 한다.
## Descriptor
<table header-row="true">
<tr>
<td>field</td>
<td>bits</td>
<td>meaning</td>
</tr>
<tr>
<td>op_id</td>
<td>4:0</td>
<td>op_id</td>
</tr>
<tr>
<td>kind</td>
<td>6:5</td>
<td>kind0conv0/1DW/2PW;mode0body/1heat/2offset</td>
</tr>
<tr>
<td>mode</td>
<td>8:7</td>
<td>kind0conv0/1DW/2PW;mode0body/1heat/2offset</td>
</tr>
<tr>
<td>hin</td>
<td>17:9</td>
<td>hin</td>
</tr>
<tr>
<td>win</td>
<td>26:18</td>
<td>win</td>
</tr>
<tr>
<td>hout</td>
<td>35:27</td>
<td>hout</td>
</tr>
<tr>
<td>wout</td>
<td>44:36</td>
<td>wout</td>
</tr>
<tr>
<td>cin</td>
<td>53:45</td>
<td>cin</td>
</tr>
<tr>
<td>cout</td>
<td>62:54</td>
<td>cout</td>
</tr>
<tr>
<td>stride</td>
<td>64:63</td>
<td>stride</td>
</tr>
<tr>
<td>dilation</td>
<td>66:65</td>
<td>dilation</td>
</tr>
<tr>
<td>pad</td>
<td>68:67</td>
<td>pad</td>
</tr>
<tr>
<td>shift</td>
<td>74:69</td>
<td>shift</td>
</tr>
<tr>
<td>weight_offset</td>
<td>106:75</td>
<td>weight_offset</td>
</tr>
<tr>
<td>param_offset</td>
<td>124:107</td>
<td>param_offset</td>
</tr>
<tr>
<td>dma_bytes</td>
<td>144:125</td>
<td>dma_bytes</td>
</tr>
<tr>
<td>stage_id</td>
<td>148:145</td>
<td>stage_id</td>
</tr>
</table>
## 자원 한계
<table header-row="true">
<tr>
<td>항목</td>
<td>DSP</td>
<td>RAMB36</td>
<td>근거</td>
</tr>
<tr>
<td>PW MAC+requant</td>
<td>132</td>
<td>52</td>
<td>weight RAM48+param4 포함</td>
</tr>
<tr>
<td>DW MAC+requant</td>
<td>33</td>
<td>5</td>
<td>DW weights4+params1</td>
</tr>
<tr>
<td>conv0 MAC+requant</td>
<td>28</td>
<td>6</td>
<td>W4+params1+line1</td>
</tr>
<tr>
<td>DW line ring</td>
<td>0</td>
<td>12</td>
<td>256bit×1280depth</td>
</tr>
<tr>
<td>FIFO/보조</td>
<td>0</td>
<td>8</td>
<td>FM read/write,IMAGE,skid 여유</td>
</tr>
<tr>
<td>CNN 설계합계</td>
<td>193</td>
<td>83</td>
<td>합성예측 아닌 구조별배정</td>
</tr>
<tr>
<td>CNN hard cap</td>
<td>196</td>
<td>90</td>
<td>초과시구현수정</td>
</tr>
<tr>
<td>외부 IP 예산</td>
<td>16</td>
<td>40</td>
<td>카메라/HDMI/VDMA/AXIDMA3/기타</td>
</tr>
<tr>
<td>총 물리한계</td>
<td>220</td>
<td>140</td>
<td>XC7Z020;BRAM은RAMB36환산</td>
</tr>
</table>
## 변경 절차
포트와수치는 generated/interface.json/manifest.json 을원본으로수정하고 Notion/XLSX/Python/ROM 을같이버전업한다. 검증전항목을검증완료로표시하지않는다. 중복된 구버전 본문을 V4 규칙으로 대체한다(2026-09-15 문장 정리).
## 하위 설계 페이지
<page url="https://app.notion.com/p/3dbe5946183b80008fccdea60a0e4fae">\[보관용·사용 금지\] 공용 설계 규칙 구버전</page>
# 확인 요청 중앙 처리 절차
모듈 담당자 AI가 발견한 전체 설계 영향 이슈를 기초 설계 AI가 검증·조정하는 절차다. 모듈별 7번째 섹션의 고정 보고서 양식과 근거 파일을 함께 받는다. 사소한 내부 구현 선택은 담당자가 결정하며 외부 계약이나 공용 예산에 영향을 주는 사항만 이 절차로 다룬다.
1. **근거 독립 검증**: 첨부한 raw 전체 로그와 문제가 되는 함수·모듈 전체를 실제로 다시 열어 대조한다. 실행 가능한 환경과 입력이 있으면 재현한다. 담당자 주장을 그대로 사실로 받아 반영하지 않는다. 재현할 수 없는 부분은 미확인으로 표시한다.
2. **전체 영향 확인**: 보고서에 나열된 페이지를 전부 열고 실제 영향 여부를 판단한다. 생산자·소비자 포트, cfg_desc256/TAG64, 산술·자원·레이턴시·모델/packer 의존성을 따라 나열에서 빠진 페이지와 파일도 찾는다.
3. **대안 비교와 확정**: 대안이 있으면 정확성, DSP/BRAM, 레이턴시, 연결 호환성을 비교한다. 중앙에서 하나를 확정하고 선택 이유와 배제한 대안의 비용을 기록한다. 중앙의 확정·반영 기록이 있기 전까지 담당자 제안은 미확정이다.
4. **관련 자료 일괄 갱신**: 확정된 변경을 관련 모듈 페이지, 통합 계약, 필요한 모듈 담당표, 사용자 `CNN_v4_포트명세_모듈별.xlsx`, 공용 설계 규칙의 변경 이력에 함께 반영한다. 필요하면 DSP/BRAM 예산표·레이턴시표와 `generated/interface.json`·`manifest.json`·Python 모델·packer·ROM·검증 자료도 함께 갱신한다. 사용자 XLSX의 모듈별 묶음, 시트, 열 순서와 양식은 유지한다. 포트나 계산식을 바꾸지 않은 문서 편집은 관련 값의 일치만 확인하며 수치를 임의로 고치지 않는다.
5. **반영 검증과 종결**: 변경된 페이지와 파일을 다시 열어 비트 배치·폭·계산식·완료 조건을 대조하고, 영향을 받는 테스트를 수행한다. 검증 완료와 미검증 항목을 구분하고 확정안·이유·실제 갱신 목록을 기록하여 담당자에게 전달할 종결 보고서를 만든다.
처리 완료 전 담당자는 해당 부분을 보류하거나 "미확정"으로 표시한 잠정 구현으로 분리한다. 영향 없는 작업은 계속 진행할 수 있다. 담당자가 보고서와 첨부파일을 중앙 창구에 전달하며, AI가 임의로 다른 팀원에게 메시지를 발송하는 절차는 아니다.
## 첨부 및 근거 원칙
- 실행 로그는 가공하지 않은 raw 전체 출력으로 받고, 요약·발췌를 전체 로그 대신 사용하지 않는다.
- 코드 검증은 해당 함수/모듈 전체를 받는다. 원본 파일·실행 명령·버전을 함께 기록한다.
- 표·계산식·수정 제안은 복사 가능한 Markdown 텍스트로 받고, 파일명에 모듈과 이슈를 표시한다.
- 이전 stride 실행 로그 대조와 offset/포트 폭 이슈처럼, 주장 확인→영향 확인→확정→전체 반영의 순서를 따른다. 과거 사례 자체를 현재 사양의 새 수치 근거로 사용하지 않는다.
## 문서 구성 변경 이력 — 2026-09-15
대상 12개 모듈을 기능/포트표/타이밍 요구사항/체크리스트/AI 에이전트용 프롬프트/Verilog 템플릿/확인 요청 절차로 재구성했다. 담당·CNN-v4.0·공용/통합 링크를 유지하고 사용자 XLSX와 동일한 377개 포트의 신호명·방향·폭·연결 대상·타이밍·Reset을 유지했다. 공용 산술·필드 정의는 이 페이지를 기준으로 하며, 각 AI 프롬프트에는 단독 사용에 필요한 전문을 복사했다. 이번 변경은 RTL이나 모델 계산식, 필드 위치, DSP/BRAM 배정의 변경이 아니다.
기존 기술 문구의 모호함은 ARG-01(heatmap done 예외와 포트표), FM-01(generic done과 DMA 완료 flag 관계), DOWN-01(frame_start/SOF/EOL 표현과 실제 포트)로 구분해 해당 페이지에 확인 요청으로 남겼다. 이 문서 편집을 해당 이슈의 기술적 승인으로 해석하지 않는다. feature_map_io의 2026-09-15 batch_last 비교식 수정은 그대로 유지했다.
