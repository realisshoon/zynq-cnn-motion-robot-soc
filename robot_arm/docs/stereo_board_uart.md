# 보드 간 CNN 좌표 UART 및 오른쪽 보드 depth 처리

> 아래는 2026-10-02 통신 통합 이력이다. 현행 승열3의 XSA는 `vitis/xsa/final_uart0.xsa`이며 LEFT=로봇1/JC, RIGHT=로봇0/JB로 분리한다. 양안 depth는 RIGHT에서 계산하고 LEFT 추종 요청에는 승인된 목표를 UART0로 전달한다. 현재 역할·명령은 [Unity UART 계약](uart_protocol_unity.md), [로봇0/1 사용 안내](robot0_robot1_commands.md)를 따른다.

## 실행 경로

2026-10-02 통합. 왼쪽 카메라 보드는 CNN 결과를 **PS UART0**로 전송하고,
오른쪽 카메라 보드는 수신한 좌표와 자신의 CNN 결과로 스테레오 depth를 계산한다.
콘솔/TRACE는 **PS UART1**을 계속 사용하며 바이너리 좌표 패킷과 섞지 않는다.

`cnn_app.c` → `stereo_board_on_result()` → 왼쪽 TX / 오른쪽 로컬 큐 →
`stereo_board_service()` → `stereo_link` 좌우 매칭 → `stereo_geometry` → depth.

전송은 COCO 관절 17개와 RGBY 마커 4개를 모두 보존한다. depth도 같은 21개 순서다.
현재 A1 제어 경로는 오른팔만 지원한다. 두 번째 로봇 PWM IP를 이용한 양팔 구동은
이번 통신 통합에 포함하지 않는다. 관절 7·9(왼팔)도 전송·삼각측량하므로 후속 확장이 가능하다.

## 새 XSA에서 확인한 하드웨어

파일: `vitis/xsa/cnn_rgby_pack77_dual_arm_uart0.xsa`.
HWH와 새로 생성한 BSP의 주소/디바이스 ID를 확인했다.

| 기능 | 주소 / 설정 |
|---|---|
| 보드 간 UART0 | 0xE0000000, MIO14 RX / MIO15 TX, 3.3 V I/O, IRQ59 |
| USB 콘솔 UART1 | 0xE0001000, MIO48..49, TRACE 빌드 921600 8N1 |
| 로봇 PWM 1 | servo_pwm_0, 0x43C70000 |
| 로봇 PWM 2 | servo_pwm_arm2_0, 0x43CA0000 |
| 기존 카메라 짐벌 PWM | **새 XSA에 없음**. 기존 0x43C90000 fallback 접근을 제거 |

UART0 연결은 좌측 TX → 우측 RX, 좌측 RX ← 우측 TX, 공통 GND다.
현재 좌표 송신은 왼쪽→오른쪽 단방향이며 오른쪽 TX는 사용하지 않는다.
프로젝트 문서의 **Zybo Z7-20** 기준으로 JF9=MIO14, JF10=MIO15다.
이는 [Digilent Reference Manual Table 16.1](https://digilent.com/reference/_media/reference/programmable-logic/zybo-z7/zybo-z7_rm.pdf)과
[공식 회로도 JF 커넥터](https://files.digilent.com/resources/programmable-logic/zybo-z7/zybo-z7-d1-sch.pdf)에서 확인했다.

| 연결 | 좌측 보드 | 우측 보드 |
|---|---|---|
| 현재 좌표 전송 | JF10 TX | JF9 RX |
| 추후 역방향 통신 | JF9 RX | JF10 TX |
| 공통 접지 | JF5 또는 JF11 GND | JF5 또는 JF11 GND |

JF6/JF12는 전원 핀이므로 데이터/GND와 혼동하지 않는다. 각 보드는 자기 전원을 사용하고
이번 UART 연결에서 3.3 V 전원끼리는 연결하지 않는다. 실제 보드 모델·리비전·커넥터 핀1 표기를 확인한다.
RS-232 전압이나 5 V 신호를 직접 연결하지 않는다. 보드 시험 전 전압·핀 라우팅을 확인한다.

UART0 추가 후 `XPAR_XUARTPS_0_DEVICE_ID`는 UART1이 아닌 UART0를 가리킨다.
콘솔 초기화를 물리 UART1 매크로로 바꾸고 BSP stdin/stdout도 `ps7_uart_1`로 지정했다.
콘솔 주소가 UART1이 아니면 초기화가 실패하도록 방어한다.

카메라 PWM 매크로가 없는 BSP는 base=0, `FIXED(NO PWM IP)`로 동작하고
카메라 레지스터를 읽거나 쓰지 않는다. `u`/`h` 짐벌 동작 요청도 거부한다.
기존 카메라 PWM이 있는 XSA의 FIXED/추적 동작은 유지한다.
새 XSA에서는 **두 카메라를 물리적으로 고정해야 한다**.

## 패킷 v1

### 2026-10-04 픽셀 흔들림 진단 로그

콘솔 UART1의 다음 레코드는 로봇 PWM을 켜지 않아도 기록한다. UART0의 170-byte
좌표 패킷이나 프레임 매칭 정책은 변경하지 않는다. `z`로 로봇 TRACE를 끄더라도
아래 진단 레코드는 유지하며, 모니터 `--filter commands`는 화면에서만 숨긴다.

| 레코드 | 의미 |
|---|---|
| `RAW` | 각 보드의 필터 전 CNN 좌표. 양쪽 어깨, 오른팔 팔꿈치·손목, 빨강·초록 손가락 마커만 기록 |
| `PAIR` | 오른쪽에서 실제 삼각측량한 좌우 프레임쌍. 좌우 session/sequence/frame ID를 각각 보존 |
| `PIX` | 해당 쌍에서 실제 사용한 각 카메라의 2D EMA 후 좌표. `L`/`R` 두 줄 |
| `PG` | 해당 쌍의 로봇 입력 승인 여부와 거절 사유. A2 안전검사나 PWM 적용 성공을 의미하지 않음 |

```text
RAW,t_ms,sid,seq,fid,slx,sly,sls,slv,srx,sry,srs,srv,ex,ey,es,ev,wx,wy,ws,wv,redx,redy,redv,greenx,greeny,greenv
PAIR,t_ms,rsid,pair,lsid,lseq,lfid,rseq,rfid,sync,gap_us,control,async,es,ws,red_s,green_s
PIX,rsid,pair,side,sid,seq,fid,slx,sly,slv,srx,sry,srv,ex,ey,ev,wx,wy,wv,redx,redy,redv,greenx,greeny,greenv
PG,rsid,pair,accepted,reason
```

좌표 단위는 원본 1280×720 영상의 픽셀이다. `s`는 관절 score, `v`는 유효 플래그다.
마커에는 관절 score를 만들어 붙이지 않는다. `sl/sr`은 사람의 좌/우 어깨이고,
`e/w`는 현재 제어하는 사람 오른팔이다. 팔꿈치는 150 mm 검사와 전완 기하 진단을 위해 포함한다.

쌍의 키는 **`(rsid,pair)`**다. 왼쪽 RAW는 `(lsid,lseq,lfid)`, 오른쪽 RAW는
`(rsid,rseq,rfid)`로 연결한다. 예를 들어 `L fid=507`, `R fid=9019`여도
PAIR에 명시된 조합이면 실제 처리된 같은 쌍이다. 번호가 같다고 매칭하거나 보드 간
부팅 후 `t_ms`를 직접 빼서 촬영 시차로 해석하지 않는다. 재부팅 시 session이 바뀌며,
쌍 번호는 현재 오른쪽 실행에서 단조 증가한다. 로그 누락 시 원시 좌표를 추정해서 채우지 않는다.

`gap_us`는 오른쪽 보드 시각계에서 두 결과를 받은 시각의 절댓값 차이이고, 최대
4294967295로 포화한다. **실제 카메라 노출 시차가 아니다.** `sync=0`이면 노출 동기화 미검증이다.
`control/async`는 StereoPoseStatus, `es/ws/red_s/green_s`는 각 점의 StereoStatus다.
PIX는 gripper용 좌우 뷰 선택으로 합치기 전의 각 카메라 좌표다.

PG 사유는 `OK`, `OFF`, `GEOMETRY`, `CLOCK`, `INVALID`, `SEQUENCE`, `STALE`,
`MATCH_GAP`, `JUMP_HOLD`, `REACQ_WAIT`, `REACQ_ACCEPT`, `UNVERIFIED`, `METADATA`,
`TIME_SEQUENCE`, `FILTER_GEOMETRY`다. 같은 세션에서 이전 승인 위치 대비 elbow/wrist
변화 ≤150 mm인 신선한 기하 유효 입력은 승인한다. 세션 변경·150 mm 초과는 첫 후보의
50 mm 이내인 서로 다른 3개 쌍으로 재획득한다. 후보 간 간격은 ≤250 ms다.

운영 소스는 외부 G 제작 당시로 복원했다. 승인 공백 >250 ms이면 filter epoch 초기화와
dt=0.1초를 사용한다. 무효점/기하 탈락 초기화도 그 당시 방식이다. 최근 500 ms 승인 경계,
실제 갱신 시각 분리, 짧은 누락 이력 보존, 이동 예측 재획득은 적용되지 않는다.
복원 전 자료는 `captures/restore_G_baseline_20261004/before/`에 보관하며 SD는 변경하지 않았다.

`RQ,rsid,pair,state,candidates,filter_epoch,l_age_us,r_age_us,gap_us`는 재획득 상태·후보 수·
필터 세대·좌우 수신 나이·간격을 기록한다. 입력 거부 시 마지막 승인 궤적은 완료 후 유지하며
재획득도 기존 A2/FK/20 ms 검사를 우회하지 않는다. `X`는 별도의 명시적 토크 해제 명령이다.

가만히 유지한 구간의 PAIR 범위를 골라 좌우 RAW/PIX의 표준편차·픽셀 범위·프레임 간
점프를 비교한다. 유효점만 통계에 포함하되 invalid/누락 개수를 함께 확인한다.
실제 정지 여부는 실험자가 기록해야 하며 통계가 정지 상태를 자동 보증하지 않는다.

170 bytes 고정 길이, little-endian, 115200 8N1 기본값. C 구조체를 memcpy해서 보내지 않는다.
전체 166 bytes에 CRC-32/ISO-HDLC(반사 다항식 0xEDB88320, 초기/최종 XOR 0xFFFFFFFF)를 적용한다.

| offset | bytes | 내용 |
|---|---:|---|
| 0 | 4 | magic `SCN1` |
| 4 / 5 / 6 | 1 / 1 / 2 | version=1 / type=1 / length=170 |
| 8 / 12 | 4 / 4 | 송신 session_id / 패킷 sequence |
| 16 / 20 / 24 / 28 | 각 4 | CNN frame_id / result_seq / cycle_count / joint_flags |
| 32 / 40 | 8 / 4 | 공통 시각계의 노출 시각 µs / shared_clock_epoch |
| 44 | 1 | bit0 노출 시각 검증, bit1 고정 기하 검증, bit2 위치 검출 품질 검증 |
| 45 | 3 | reserved=0 |
| 48 | 16 | red / blue / green / yellow 원래 packed marker word |
| 64 | 102 | COCO 17 × (uint16 x, uint16 y, int8 score, uint8 valid) |
| 166 | 4 | CRC32 |

valid 관절/마커는 원본 1280×720 범위인지 검사한다. 파서는 CRC·버전·길이·범위 오류를
따로 집계하며 잡음, 잘린 패킷, 잘못된 CRC 뒤에 magic을 재탐색한다.
115200에서 한 패킷은 약 14.76 ms, 이론상 최대 약 67.8 packets/s다.
실제 여유율과 송수신 지연은 보드에서 측정한다.

TX는 진행 중인 패킷을 끝까지 보존하고, 다음 패킷 하나만 최신 값으로 대체한다.
큐가 무한히 늘어나거나 패킷 중간에 다른 프레임 바이트를 섞지 않는다. 대체는 `drop`으로 센다.
RX IRQ는 최대 64 bytes씩 4096-entry 링에 복사하고, 파싱/삼각측량/A1은 foreground에서만 실행한다.
UART0 전용 IRQ이며 기존 콘솔 UART1의 polling 정책은 바꾸지 않았다.
RX overrun/링 overflow를 감지하면 부분 패킷을 폐기하고 다시 동기화한다.
4096 bytes는 115200에서 약 356 ms 용량일 뿐, 긴 SD 작업에서도 무손실이라는 보장은 아니다.

## 프레임 시간과 제어 승인

후속 하드웨어/펌웨어 설계는 [프레임 시간 정합·노출 동기화 구현안](stereo_frame_sync_plan.md)에
단계별로 정리했다. 제조사 자료의 OV5640 frame exposure 동기화 지원과 Pcam FREX 경로는
확인했지만 현행 720p RAW10 MIPI 모드에서의 적용/전기 조건은 추가 검증이 필요하다.

### 2026-10-02 동기화 리뷰 결과

현재 XSA HWH와 현행 펌웨어에서 **보드 간 촬영 동기화/실제 노출 시각 공급 경로는 미구현**이다.
Claude `260916_ver.1`의 2026-09-30 기록에서도 동기화는 후속 구현 항목이었고,
현재 코드·XSA에서 완료 근거를 찾지 못했다. 세션에 새 질문을 보내지는 않고 기존 기록을 읽었다.

| 확인한 근거 | 판단 |
|---|---|
| XSA 외부 포트 | 카메라·UART·PWM은 있지만 공유 촬영 trigger/SOF 포트 없음 |
| MIPI/VDMA의 TUSER | 보드 내부 영상 경로의 프레임 시작 신호. 공통 시간으로 latch하는 IP/레지스터 없음 |
| VDMA의 internal genlock 설정 | 같은 보드의 영상 버퍼 경로 설정. 두 센서의 노출을 맞춘 근거가 아님 |
| irq_concat_0 | CNN 완료 IRQ와 20 ms Timer만 연결. VDMA mm2s/s2mm IRQ는 HWH에서 연결되지 않음 |
| cnn_bringup_prepare_frame_internal() | 로컬 VDMA의 최근 완료 버퍼를 선택. 버퍼별 노출 시각이나 공통 capture_id를 보존하지 않음 |
| next_frame_id / XTime | 소프트웨어 추론 번호와 CNN 실행/완료 시간. 카메라 노출 타임스탬프가 아님 |
| cam_gpio EMIO54 | Pcam sensor power-down 제어 출력. 촬영 동기화 입력이 아님 |

동기화 후속 설계에는 실제 선택 프레임의 SOF/노출 정보를 latch해서 버퍼 ID와 결합하고,
양쪽 시계를 공통 펄스/시간계로 매핑하는 경로가 필요하다. 공통 CNN START만 동시에 보내도
이미 DDR에 들어 있는 영상의 노출 시각이 같아지지는 않는다. 센서의 지원 모드와 Pcam 배선을
확인하기 전에는 GPIO를 센서 trigger라고 가정하거나 펌웨어만으로 노출을 동기화했다고 하지 않는다.

**UART로 좌표를 보내는 것은 카메라 노출 동기화가 아니다.** 두 보드 frame_id 일치,
CNN 완료 시각, UART 도착 시각, PC 명령 송신 간격도 노출 동기화의 증거가 아니다.

현재 `cnn_app.c`에는 노출 시각을 제공하는 하드웨어 경로가 없어 metadata=NULL로 호출한다.
노출 시각/품질/고정 기하 검증을 임의로 true로 만들지 않는다.
따라서 현재 빌드는 다음처럼 구분된다.

- 기본 비동기 입력(시험 OFF): 오른쪽 보드에서 측정한 UART 수신 시각과 로컬 CNN 완료 시각으로
  후보를 매칭하고 **진단용 depth**를 계산한다. `sync=0`, `control=-2`이며 A1/A2 목표를 갱신하지 않는다.
- 명시적 비동기 시험 ON: 수신 나이 250 ms 이내인 최근 RIGHT 프레임에 대해 수신 시각이
  가장 가까운 LEFT 프레임을 선택한다. 좌우 수신 시각 차이는 100 ms 이하여야 한다.
  사용한 프레임보다 오래된 미사용 큐 항목은 버리고 각 프레임은 한 쌍에만 사용한다.
  순서대로 모두 처리하는 것은 저장 로그의 오프라인 재생이며 LIVE 정책과 구분한다.
  `sync=0`, `control=-2`와 원본 메타데이터를 유지한다. 기하/150 mm 이동 또는
  3-frame 재획득을 통과한 입력만 A1에 전달하고, 실제 노출 동기화로 주장하지 않는다.
- 검증된 입력: 공통 epoch의 실제 노출 시각으로 매칭한다. 임시 skew 한계는 1000 µs다.
  고정된 보정 기하와 검출 품질까지 검증되고 elbow/wrist 삼각측량이 성공한 경우에만
  `HumanPose3D` → `agent1_run_stereo()` → 기존 A2/20 ms 제어 경로로 전달한다.
  skew=1000 µs는 구현 검증용 설정이지 움직이는 손의 허용 오차를 실측 인증한 값이 아니다.

카메라별 8-frame 큐, sequence 중복/역행 검사, 검증된 노출 시각의 단조 증가 검사를 둔다.
250 ms 로컬 보관 시간 한계는 strict와 비동기 LIVE 모두에 적용한다.
비동기 시험에서도 양쪽에 검증된 노출 메타데이터가 있으면 epoch/skew 기준을 우회하지 않는다.
각 좌우 프레임은 한 쌍에만 사용한다. 비동기 A1 dt는 수신기 로컬 CNN 완료 시각의 차이이며
노출 간격이 아니다. UART 수신 완료 시각과 CNN 완료 시각으로 동기화를 주장하지 않는다.
session 변경 시 미매칭 큐를 비우고, epoch 변경은 새 세션 또는 명시적인 재초기화가 필요하다.
기본 session은 부팅 중 로컬 타이머에서 생성한 비암호학적 값이며 재부팅마다 유일함을 보장하지 않는다.
재부팅/재연결 시험 때는 수신기 재초기화까지 확인한다. 이 프로토콜은 인증 프로토콜이 아니다.

동기화 구현 시 실제 CNN 입력 프레임과 연결된 `StereoFrameMetadata`를
`stereo_board_on_result(result, metadata)`에 공급한다. 서로 독립적인 PS 타이머를
같은 epoch라고 표시하거나 CNN 완료 시각을 exposure_time_us로 넣으면 안 된다.
quality 검증은 같은 사람/같은 해부학적 관절, 마커 색 대응, 검출 신뢰도·포화 여부를 확인해야 한다.
나쁜 마커는 valid를 해제해 주요 관절은 사용하고 손목/그리퍼는 이전 값을 유지할 수 있다.
양쪽 기하 재투영 오차의 기본 한계는 각각 15 px다. 이 기준만으로 시간 정합이나
손 추적 정확도를 증명할 수 없다.

모든 stereo 경로는 카메라별 21점 픽셀 좌표를 push 시점에 τ=0.10 s EMA로 처리한 뒤
삼각측량한다. 미검증 입력은 수신기 시각, 검증된 입력은 노출 시각으로 필터 dt를 계산한다.
불량 점은 해당 EMA 이력을 갱신하지 않는다. 팔꿈치·손목의 30 px 초과 도약,
session/시험 모드/시각 검증 상태 변경·250 ms 초과 입력 간격은 임시 후보로 분리한다.
고정 최초 후보와 직전 후보 양쪽에서 30 px 미만인 3개 서로 다른 입력이 모이면
새 앵커로 재획득한다. 빨강·초록은 손목 거리 ≤2×max(전완 픽셀 길이,80 px)를
검사하고 실패하면 invalid 처리한다. 정상 범위의 손가락 개폐는 도약 후보로 묶지 않는다.
원본 패킷/메타데이터와 기존 각도 계산 후 EMA/출력 필터는 보존한다.
최근 수신 기준 매칭에서도 서로 다른 노출 순간이 결합될 위험은 남는다.
픽셀 EMA/재투영/거리 gate는 동기화나 실제 손 정확도를 보장하지 않는다.

계산부는 현행 `captures/stereo_pairs_20261001_151719/calibration_20mm` 보정값을 사용한다.
XYZ는 왼쪽 광학 좌표계의 mm이고, depth 결과의 +Y는 영상 아래쪽이다.
A1 입력은 Y 부호를 반전한 +Y 위쪽 고정 rig 좌표계다. 2026-10-02 사용자 지시에 따라
스테레오 경로의 어깨 기반 몸통 좌표계와 어깨 폭 XYZ 정규화를 제거한다. 실측 mm 좌표와
고정 rig 축에서 forearm/hand 각도를 생성하며 어깨는 선택 진단점이다.
mm Z를 기존 가상 XY에 섞지 않고 가상 major/finger Z 재구성을 우회한다.
손가락 2점이 없거나 불량하면 가상 손가락 depth를 만들지 않는다.
입력 거부/중단은 마지막 승인 목표까지 이동 후 유지하는 기존 정책을 바꾸지 않는다.
기본 노출 gate는 유지하며 명시적인 A/S/T 명령의 별도 비동기 시험 경로를 제공한다.
time_verified=0/control=-2는 그대로이며 async_status/accept로 시험 입력 상태를 표시한다.
PWM ON 후 A를 입력해야 추종이 가능하다. 상세 운영·제약은
[비동기 양안 시험](stereo_async_trial.md), 각도 정의는 [A1 절대좌표](stereo_absolute_angles.md)를 따른다.

## 빌드와 확인

### 2026-10-02 부팅 초기화 오류 수정

처음 배포한 stereo UART BOOT에서 양쪽 HDMI가 검게 나오고 USB 콘솔 수신이 없었다.
코드에서 재현 가능한 원인은 `stereo_board_init()`의 `XUartPsFormat` 위치 기반 초기화다.
실제 Vitis 2020.2 BSP의 필드는 `BaudRate, DataBits, Parity, StopBits` 순서인데
3개만 초기화해 `BaudRate=0`, `DataBits=4`가 들어갔다. BSP의 `XUartPs_SetDataFormat()`은
이 DataBits를 invalid parameter로 거부하며 main은 카메라 초기화 호출 전에 종료한다.
콘솔 출력도 foreground 송신 링에 남아 있어 이 종료 경로에서는 보이지 않을 수 있다.

4개 필드를 이름으로 지정하는 초기화로 수정했다. 테스트 stub도 실제 BSP 필드 순서와
StopBits 타입에 맞췄고, format 설정 시 baud까지 검증한다. 종전 stub은 BaudRate가
빠져 있어 기존 호스트 테스트가 이 문제를 놓쳤다. 실제 BSP와 같은 stub에서 수정 전
코드는 `-Werror=missing-field-initializers`로 실패하고 수정 후 LEFT/RIGHT 테스트는 통과한다.

기존 SD 배포 패키지는 이력으로 보존한다. 수정 패키지는
`captures/stereo_uart_fix_20261002/`에 별도로 준비하며 SD 교체/보드 재부팅은
사용자의 별도 요청 후 수행한다. 실보드의 영상 복구까지 확인한 상태는 아니다.

```powershell
powershell -ExecutionPolicy Bypass -File robot_arm/vitis/setup_vitis.ps1 -Workspace D:/vws_st_left -StereoRole Left
powershell -ExecutionPolicy Bypass -File robot_arm/vitis/setup_vitis.ps1 -Workspace D:/vws_st_right -StereoRole Right
```

Left/Right는 새 XSA를 기본 사용하며 **로봇 PWM은 기본 비활성화**다.
2026-10-02 런타임 PWM 제어 빌드에서도 부팅 OFF다. UART 대문자 `E`/`X`/`V`로
활성화/해제/상태 조회를 제공하며 LEFT의 `E`는 거부한다. RIGHT는 기존 오른팔
CH0..CH4만 대상으로 한다. `E`는 LIVE, 정지 상태이며 기준 명령과 일치할 때만 허용하고,
shadow/UPDATE를 먼저 적용한다. OFF 중 누적된 가상 자세로 점프하지 않지만 위치 피드백은
없어 실제 자세 불일치/처짐에 따른 움직임은 예방할 수 없다. 해제 전 팔을 지지해야 한다.
노출 동기화 gate와 두 번째 PWM의 미사용 정책은 유지한다. 새 BOOT.BIN 적용 후 사용하며
PC 모니터의 E/X 전송에는 `--interactive --allow-motion-commands`가 필요하다.
Mono는 종전 XSA와 PWM 기본 정책을 유지한다. 양쪽 StereoBaud는 같아야 한다.
`EnableStereoRobotPwm`은 Right에서만 허용하지만 실제 동기화·검출 품질 공급과
별도의 사용자 서보 시험 승인이 없으면 사용하지 않는다.

2026-10-02 초기 검증용 workspace는 `captures/st_l`, `captures/st_r`이며 ELF는
`captures/st_l/st_left/Debug/st_left.elf`, `captures/st_r/st_right/Debug/st_right.elf`다.
당시에는 BOOT.BIN 생성/SD 교체/보드 flash/실제 서보 구동을 하지 않았다.
현행 소스 기반 LEFT 공통·RIGHT 필터별 배포 파일은 `vitis/boot/stereo_live_filters_*`에
별도로 생성하며 각 폴더의 `manifest.json`에서 파일 검증/실제 보드 부팅 상태를 구분한다.

UART1에서 1초마다 `[ST]` 통계를 출력한다. `L/R`은 마지막 쌍의 CNN ID,
`fresh`는 마지막 쌍에서 더 오래된 쪽의 수신 나이가 250 ms 이내인지의 진단값이다.
`sync`는 노출 시각 정합 여부,
`control`은 승인 상태(0=승인, -2=검증 정보 없음)다.
`qL`/`qR`은 좌우 FIFO 잔량, `qdrop`은 양쪽 큐 용량 초과로 버린 프레임의 누적 수다.
송신 대체 `drop`과 RX `overflow`와 구분한다. `time_gate=RECEIPT_LATEST`는 비동기 LIVE,
`STRICT`는 기본 시간 gate 모드다.
`stale/gap/skip`은 만료 입력·매칭 간격 거부 평가·오래된 큐 항목 폐기를 구분한다.
`marker_bad/jump_bad/reacquire`는 마커 품질 거부·2D 도약 후보 보류·2D 점 재획득 수다.
`Zmm(wrist,red,green)`은 mm 정수 진단값이며 없는/불량 점과 오래된 결과는 0을 출력한다.
이 0은 실제 거리 0 mm가 아니다. 전체 21점 XYZ와 상태는 `stereo_board_take_depth()`로 확인한다.

좌우 USB 콘솔을 한 터미널에서 보려면 ComPortMaster의 양쪽 포트를 닫고
`robot_arm`에서 다음을 실행한다. 명령을 보드에 보내지 않는 수신 전용 도구이며
모니터만 사용하는 경우 현재 SD의 BOOT.BIN을 다시 만들 필요는 없다.
운영 소스의 One Euro 등 펌웨어 변경을 적용하려면 해당 소스를 다시 빌드한 BOOT가 필요하다.

```powershell
python tools/stereo_vision/monitor_stereo_uart.py --left COM3 --right COM4 --filter stereo
```

`--filter all`은 전체 화면 표시다. 필터와 관계없이 `captures/monitor_.../`에 좌우 전체
원시 바이트/텍스트 및 통합 로그를 보존한다. Ctrl+C로 종료한다.
공유 코드의 설정/사용법은 [양안 시험 운영 안내](stereo_async_trial.md)를 참고한다.
캘리브레이션 사진 및 로컬 도구 스크립트는 공유 패키지에서 제외된다.

호스트 회귀: `python robot_arm/tests/robot_calibration/run_tests.py`.
추가 시험은 프로토콜 복원/CRC, 좌우 큐·시간/품질 게이트·A1 실측 XYZ,
LEFT TX 혼합 방지/드롭, RIGHT IRQ 수신·overflow 복구·strict 오래된 입력 거부,
비동기 최신 쌍 선택/8-frame overflow/250 ms 만료·100 ms 수신 간격 제한/
2D 품질 게이트·3D 재획득/검증된 epoch·skew 유지,
카메라 PWM 누락 시 MMIO 금지를 포함한다. C/OpenCV 하네스도 동일 계산 소스와
펌웨어 보정 상수가 원본 JSON과 정확히 일치하는지 검사한다.

실보드에서 남은 확인: UART0 물리 핀/전압/배선, 카메라 고정,
좌표 수신률/CRC·overrun/틱 지연, 실제 입력 프레임의 노출 시각 및 공통 동기화,
움직이는 손의 depth 정확도와 계산 지연. 양팔 A1/A2/A3와 두 번째 PWM 출력은 별도 작업이다.

2026-10-02 검증: 새 XSA로 LEFT/RIGHT의 BSP·FSBL·ELF 빌드 성공(로봇 PWM 비활성).
추가 호스트 5 suites PASS, 기존 16 suites 중 11 PASS/5 기존 FAIL로 신규 회귀 없음.
기존 실패는 integration_smoke, trace, axis_replay, forearm_calibration, forearm_replay다.
C/OpenCV 계산 및 보정 상수 검증 14 tests PASS. BSP 자동 생성 servo_pwm_selftest의
기존 unused-variable/sign-compare 경고는 남아 있으며 실제 보드 실행 검증은 하지 않았다.
기존 Python 스테레오 테스트 40 tests PASS. 이번 18쌍/36점의 동일 픽셀 C/OpenCV 재검증도
PASS이며 최대 XYZ 성분 차이는 9.095e-13 mm다. 이는 실제 손 정확도나 촬영 동기화 검증이 아니다.

검증한 아티팩트 SHA-256:

- XSA: `5FF83E0C2D4D8B8330E422A5CF47088C8BF5E1A026172FF15947F258D10DE0FF`
- LEFT ELF: `D6590CEAD489A6CAD53DDA3F2DBEA599BEEEA704B4E4256466C13F749C2998E4`
- RIGHT ELF: `0D264DF606F99EC62673A284D236F7FF1E8C57E0AD2414E6EFFDC396D02A8B4B`

2026-10-04 안정화 소스 호스트 회귀: 28 suites 중 23 PASS/5 기존 FAIL.
수정 전 소스에서도 동일한 integration_smoke/trace/axis_replay/forearm_calibration/
forearm_replay 실패를 확인했다(재생 자료는 현행 원본 자료로 다시 실행).
신규 재획득·최신 매칭·2D 품질·RQ·16/20 cm FK와 테이블 -10 cm·충돌 궤적 검사는 PASS다.
필터 세대 초기화는 4개 실제 C 변형에서 검증했고 모니터/빌드 계약 Python 검사도 PASS다.
이 결과는 실제 보드 부팅·노출 동기화·실물 추종 정확도 검증을 대체하지 않는다.
