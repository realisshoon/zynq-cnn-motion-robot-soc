# CNN 가속기(PL) 인터페이스 계약 — main.c 통합용

CNN-v4.0 가속기 팀(에이전트)으로부터 2026-09-18에 받은 답변을 정리한 문서입니다.
main.c(ARM/Vitis, baremetal)가 CNN 가속기와 어떻게 상호작용해야 하는지에 대한
현재까지의 확정/미확정 사항을 기록합니다. 원본 근거:
[top_level_fsm](https://app.notion.com/p/3d9e5946183b8193b5c1e6a05a751c69),
[cnn_accelerator_top](https://app.notion.com/p/3d9e5946183b81d59925ed678e8fb87a),
[CNN-v4.0 프로젝트 기준](https://app.notion.com/p/3d9e5946183b815692cbec0376e53196).

## 전체 구조

```
PS(ARM/Vitis) --S_AXI_CTRL(AXI-Lite)--> top_level_fsm
top_level_fsm --AXI-Lite DMA 제어--> IMAGE/WEIGHT/FEATURE DMA (3개)
DMA <--AXI-MM(HP 또는 ACP, 미확정)--> DDR
DMA <--64-bit AXI-Stream--> CNN datapath
```

정상 추론 중 DMA 레지스터 소유자는 PL(`top_level_fsm`)이다. ARM이 DMA를 직접
제어하는 범위는 **부팅 초기화와 오류 복구뿐**이며, 프레임마다 레이어별 DMA를
ARM이 관리하지 않는다 — HLS `m_axi` 직접 접근 구조도 아니고, ARM이 레이어마다
개입하는 구조도 아니다. ARM은 프레임 1개당 `CTRL.START`를 1회만 쓴다.

## 확정된 것

### DMA/레지스터 주소

| DMA     |        제어 주소 | 역할 |
| ------- | -----------: | --- |
| IMAGE   | `0x40400000` | 카메라 프레임 DDR → stage0 (MM2S, SG) |
| WEIGHT  | `0x40410000` | weight/parameter DDR → weight BRAM (MM2S, Simple) |
| FEATURE | `0x40420000` | FM_A/FM_B 읽기·쓰기 (MM2S+S2MM, Simple) |

### CNN 제어 레지스터 맵

| Offset | Register | 의미 |
| ------ | -------- | --- |
| `0x000` | `CTRL` | bit0 START, bit1 DONE_CLEAR, bit2 SOFT_RESET |
| `0x004` | `STATUS` | bit0 DONE, bit1 BUSY, bit2 ERROR, bit3 IMAGE_READ_DONE |
| `0x018~0x058` | `JOINT_0~16` | 관절 결과 |
| `0x05C` | `VALID_FLAGS` | 관절 유효 비트 |
| `0x068` | `THUMB_XY` | 색상 마커 결과 |
| `0x06C` | `INDEX_XY` | 색상 마커 결과 |
| `0x070` | `FRAME_ID` | PS가 입력한 프레임 번호 |
| `0x074` | `RESULT_SEQ` | 결과 publish마다 증가 (seqlock 패턴) |
| `0x078` | `ERROR_STATUS` | protocol/DMA/timeout/parameter 오류 |
| `0x07C` | `IRQ_ENABLE` | bit0 IRQ enable |
| `0x080` | `CYCLE_COUNT` | START부터 publish까지 걸린 cycle (100MHz 기준) |
| `0x084` | `RESULT_FRAME_ID` | 결과가 어느 입력 프레임인지 |

레이어별 완료 인터럽트는 없다. `top_level_fsm`이 내부적으로 레이어 진행을
관리하고, ARM에는 `STATUS.DONE`/`STATUS.ERROR`/`STATUS.IMAGE_READ_DONE` 세
가지만 노출된다. 완료 통지는 GIC 레벨 인터럽트(권장) 또는 `STATUS` 폴링 둘 다
가능. `ap_done`은 없음(HLS IP 아님).

### DRAM 버퍼 배치 (기본값, 레지스터로 재설정 가능)

| 영역 | 기본 주소 | 예약 크기 | 설정 방법 |
| --- | ---: | ---: | --- |
| Weight | `0x10000000` | 16 MiB | `WGT_BASE` |
| FM_A | `0x11000000` | 1 MiB | `FM_A_BASE` |
| FM_B | `0x11100000` | 1 MiB | `FM_B_BASE` |
| SG Descriptor | `0x11200000` | 64 KiB | `SG_DESC_BASE` |
| Input frame | 런타임 결정 | VDMA frame 크기 | SG BD + `FRAME_BASE` |

레이어별 개별 버퍼는 없고 FM_A/FM_B 2개를 ping-pong으로 재사용(최대 페이로드
786,432 B이므로 1 MiB로 충분). main.c/linker는 이 구간이 프로그램 코드, heap,
stack, framebuffer, VDMA 버퍼와 겹치지 않게 예약해야 한다.

### Cache coherency (HP non-coherent 가정 — 아래 "미확정" 참고)

- **Flush 필요**: ARM이 DDR에 쓴 weight binary, SG descriptor, 입력 frame, DMA source buffer.
- **Invalidate 필요**: DMA가 쓴 DDR 데이터를 ARM이 읽을 때(IMAGE SG BD status, FEATURE DMA 결과 디버깅 등).
- **Invalidate 불필요**: 최종 관절 결과(`JOINT_0~16` 등)는 DDR이 아니라 AXI-Lite 레지스터로 publish되므로 캐시 처리 불필요. FM_A/FM_B도 ARM이 직접 안 읽으면 매 stage 캐시 처리 불필요.

### 결과 읽기 패턴 (seqlock)

```c
seq1 = CNN_REG(RESULT_SEQ);
/* read JOINT_0..16, VALID_FLAGS, THUMB_XY, INDEX_XY, RESULT_FRAME_ID */
seq2 = CNN_REG(RESULT_SEQ);
if (seq1 == seq2) { /* 일관된 결과 */ }
```
주의: 위 스니펫은 불일치 시 재시도 루프가 없음 — 실제 main.c 구현 시 재시도
필요 여부를 CNN 팀에 재확인 필요 (아래 "확인 필요" 참고).

### 타이밍 예산 (실측 아님, 공학적 추정)

| 항목 | 예산 |
| --- | ---: |
| CNN compute | 약 57.04 ms |
| DDR payload (8,230,400 B, 300MB/s 가정) | 약 27.43 ms |
| 입력/제어 여유 | 6 ms |
| **전체 추정** | **약 90.48 ms (목표 100ms/10fps 이내)** |

실측은 `CYCLE_COUNT / 100000.0f`(ms, 100MHz 기준)로 확인. `START`~publish만
포함, 카메라 프레임 생성 시간은 미포함.

**→ 57~90ms 추론 시간은 Agent2의 20ms(50Hz) 서보 틱보다 훨씬 길다.** 서보
루프가 CNN 완료를 기다리는 구조는 불가능 — 반드시 비동기(CNN DONE ISR이 최신
pose를 교체, 서보 루프는 항상 마지막으로 완료된 pose 사용, CNN BUSY 중 새
프레임은 skip, 일정 시간 결과 없으면 fail-safe)로 설계해야 함. 이는 Agent2가
이미 전제하고 있던 구조와 일치.

## main.c 책임 요약 (CNN 팀 권장안)

**부팅 시**: DMA 3개 reset/halted 확인 → CNN `SOFT_RESET` → weight binary 배치
+ cache flush → FM_A/FM_B/SG 영역 예약 → SG descriptor 구성 + flush → GIC 레벨
인터럽트 설정 → `IRQ_ENABLE=1` → VERSION/MODEL_TAG 확인.

**프레임마다**: VDMA frame 확보 → SG BD 갱신 + flush → `FRAME_BASE`/`FRAME_ID`
기록 → 이전 DONE clear → `CTRL.START` → 즉시 서보 루프로 복귀(non-blocking) →
ISR은 완료 flag만 세팅 → 메인 루프에서 `RESULT_SEQ` 패턴으로 결과 읽기.

**오류 시**: `ERROR_STATUS` 저장 → DMA 3개 reset/halt → CNN `SOFT_RESET` → SG
descriptor 재초기화 → 새 프레임부터 재시작.

## 아직 미확정 (Vivado BD/XSA 확정 후 xparameters.h에서 확인 필요)

- `S_AXI_HP0~3` 중 실제 연결 번호, 또는 ACP 사용 여부, 사용 포트 수
- CNN 제어 레지스터(`S_AXI_CTRL`)의 절대 base address
- GIC interrupt ID

→ **이 세 값이 나오기 전까지는 main.c의 실제 초기화 코드를 확정 구현할 수
없음.** 설계(인터럽트 핸들러 구조, cache 처리 위치, 버퍼 예약)는 지금 진행
가능하나, 실제 주소/ID 상수는 XSA export 이후 채워야 함.

## 확인 필요 (CNN 팀에 재질문 후보)

- `RESULT_SEQ` seqlock 읽기에서 `seq1 != seq2`일 때 재시도 루프가 필요한지,
  아니면 그 프레임을 버리는 게 의도된 설계인지.
