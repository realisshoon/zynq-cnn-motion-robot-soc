# UART 참고 문서 (콘솔 명령 + TRACE 로그)

## 현행 승열3 우선 안내 (2026-10-08)

최신 운영 소스는 정상 `src/include/config`이며 G의 3D 칼만과 로봇0·1 독립 제어를 사용한다.
다음 One Euro/A 수치는 이전 버전 이력이다. 현재 UART 프레임·녹화 메뉴 상태·Unity GUI 연동은
[Unity UART 계약](uart_protocol_unity.md), 개별/공통 명령은 [로봇별 명령](robot0_robot1_commands.md)을 우선한다.
콘솔의 `l/r/both` 접두어는 호스트 문법이며 Unity에서 보드에 그대로 보내지 않는다.

운영 A는 2D 시간 EMA τ=0.10 s → 삼각측량 → 3D One Euro(min=0.5 Hz,
β=0.001 /mm, derivative=1 Hz)를 기본 적용한다. 별도 UART 활성화 키가
필요하지 않으며 PWM/추종 E/A와는 독립된 처리 단계다. RIGHT 런타임 설정은 아래의
`~F,...\r` 프레임을 사용하며 A1 각도 EMA와 안전검사를 유지한다.
설정·비교 빌드와 미해결 손목 기준 복구 항목은
[One Euro 계약](stereo_one_euro.md)을 따른다.

2026-10-04 수정 소스 기준. USB 콘솔 UART1은 **CNN 브링업 콘솔 명령 입력**과
**디버그 로그(TRACE) 출력**에 공유한다. 별도의 보드 간 UART0는 LEFT CNN 좌표를
RIGHT로 보내며 로봇 입력은 양안 재구성 결과에서 온다.
보드 간 패킷/세션/처리 쌍 정보는 [stereo_board_uart.md](stereo_board_uart.md)를 따른다.
아래 키는 보드에 직접 전송하는 바이트이고 PC 모니터에서는 `l/r/both` 대상을 앞에 붙인다.

## 1. 연결 설정

| 항목 | 값 |
|---|---|
| 포트 | PS UART1 (보드 USB-UART) |
| 형식 | 8N1 |
| Baud (ROBOT_TRACE 켠 빌드) | **921600** |
| Baud (ROBOT_TRACE 끈 빌드) | 115200 |

TRACE는 `ROBOT_TRACE` 컴파일 심볼로 켜진다(`setup_vitis.ps1` 기본 빌드는 항상 켬).
터미널/PC 스크립트도 baud를 맞춰서 열어야 한다.

RX(터미널→보드)는 콘솔 명령 전용이고, TX(보드→터미널)는 `xil_printf` 콘솔 출력과
TRACE 로그 줄이 같은 스트림에 섞여 나온다.

## 2. 콘솔 명령 (터미널에서 키 입력)

한 글자를 누르면 즉시 실행된다(Enter 불필요, `m`/`j` 하위 메뉴 및 아래 런타임 필터 프레임은 예외).
`?`를 누르면 이 표와 같은 도움말이 그대로 출력된다.

| 키 | 동작 |
|---|---|
| `p` | CNN identity(하드웨어 식별) 조회 |
| `w` | SD에서 가중치 로드+검증 (추론 유휴 상태에서만) |
| `g` | 이미지 SG(scatter-gather) descriptor 생성/검증 (추론 유휴 상태에서만) |
| `s` | 완료 IRQ 기반으로 1프레임만 실행 (바쁘면 거부) |
| `1` | 캡처 저장 폴더를 `0:/CALIB/`로 선택 (체커보드용, 부팅 기본값) |
| `2` | 캡처 저장 폴더를 `0:/JIG/`로 선택 (마커 지그 검증용) |
| `C` | 완료된 카메라 프레임 1장을 선택한 폴더의 `CAP0001.PPM` 등으로 저장 (폴더별 자동 번호) |
| `L` | JIG 사진 삭제 명령 지원/대기 상태 확인. 파일 변경 없음 |
| `D` | 선택된 폴더와 관계없이 `JIG/CAP0001.PPM`~`CAP9999.PPM` 일반 파일 전체 삭제. 성공 후 JIG 번호를 0001로 초기화 |
| `a` | 연속(IRQ 구동) 추론 시작/정지 토글 |
| `t` | CNN 상태 + 마지막 처리 시간 출력 |
| `r` | 마지막 CNN 결과 출력 |
| `d` | CNN 진단 덤프 |
| `x` | 추론 정지 + CNN soft-reset (진행 중 프레임은 끝난 뒤 처리) |
| `o` | 오버레이/컬러 레지스터 덤프 |
| `b` | HDMI 오버레이 전환 (로봇 6점 / 상반신) |
| `c` | 그룹 컬러 오버레이 강제 테스트 |
| `n` | CNN 초록 마커 검출 on/off |
| `m` | R/G/B 검출 margin 입력 하위메뉴 진입 (3절) |
| `j` | 카메라 추적(deadband/filter/속도/서보) 설정 하위메뉴 진입 (3절) |
| `q` | **전체 UART 출력** mute/재개 (CNN·로봇 제어 자체는 계속 동작) |
| `z` | **로봇 TRACE만** mute/재개 (CNN 로그는 그대로 나옴) |
| `E` | 로봇 PWM 활성화: LIVE에서 마지막 성공한 HAL 명령으로 내부 궤적 정합 후 기존 안전검사; LEFT는 거부 |
| `X` | 로봇 PWM·비동기 시험 OFF, 마지막 성공한 HAL 명령에서 내부 궤적 정지. 토크 해제; 소문자 `x`와 다름 |
| `V` | 로봇 PWM 상태 조회. 소문자 `v`의 카메라 상태와 다름 |
| `A` | RIGHT PWM ON·LIVE에서 비동기 양안 절대좌표 시험 활성화. 실제 노출 동기화 아님 |
| `S` | 새 비동기 시험 입력 차단. 마지막 승인 목표까지 이동 후 유지; 토크 해제 아님 |
| `T` | 비동기 양안 시험 상태 조회 |
| `u` | 카메라 추적 + 카메라 PWM 출력 토글 |
| `f` | FIXED: 추적은 끄고 PWM은 현재 pulse로 고정 유지 |
| `v` | 카메라 추적기/PWM 상태 출력 |
| `i` | 카메라 pan 방향 반전 토글 |
| `k` | 카메라 tilt 방향 반전 토글 |
| `h` | 카메라 pan/tilt 즉시 중앙 정렬 |
| `?` | 도움말 |

`E/X/V`는 런타임 PWM 제어 BOOT.BIN에서 지원하며, 아래 OFF→ON 정합 수정에는 2026-10-04 수정 소스의 새 BOOT이 필요하다.
Stereo 빌드는 부팅 OFF이며 활성화 시 최초 홈/마지막 HAL 명령의 펄스를 인가한다.
수동 X는 마지막 성공한 HAL 적용 명령을 보존하고 내부 궤적을 그 명령에서 정지시킨다.
E는 OFF 중 내부 계산과 기준 명령이 어긋난 경우 이를 재정합한 뒤 활성화 검사를 수행한다.
역할·LIVE 모드·관절 범위·FK 안전검사·HAL 실패 보호를 삭제하지 않으며 무조건 강제 ON이 아니다.
마지막 전송 명령은 실측 위치가 아니므로 OFF 중 실제 처짐/수동 이동은 피드백 없이 검증하지 못한다.
ON 시 팔이 움직일 수 있으므로 지지·실제 자세를 확인하며 비동기 추종 A는 E와 별도다.
PWM ON 중 새 입력이 탈락하면 마지막 승인 목표까지 이동 후 유지한다. 새 유효 입력이 승인되면 재개하지만,
직전 승인 좌표와 150mm 이상 벌어진 입력이 계속 거부되면 유지가 오래 지속될 수 있다.
이는 수동 X에 의한 궤적 정지·토크 해제와 다르며 시간 경과로 자동 완화하지 않는다.
스테레오 노출 동기화 gate는 그대로 유지한다. 상세 안전 조건과 PC 입력법은
[양안 시험 운영 안내](stereo_async_trial.md)를 따른다.
PC 모니터에서는 `r E`, `r X`, `r V` + Enter이며 E/X는 `--allow-motion-commands`가 필요하다.

### RIGHT 런타임 필터 프레임

`monitor_stereo_uart.py --interactive`의 고수준 명령을 사용한다. 필터는 RIGHT 전용이며
`l filter ...`/`both filter ...`는 PC에서 거부한다. 기존 단일 키와 `both m/value/enter`
문법은 유지한다. 원시 프레임을 임의 payload로 보내는 PC 입력 문법은 제공하지 않는다.

| PC 입력 | 보드에 전송하는 프레임 | 값·단위 |
|---|---|---|
| `r filter show` | `~F,SHOW\r` | 현재 설정 조회 |
| `r filter default` | `~F,DEFAULT\r` | 해당 BOOT 기본값 복원 |
| `r filter 2d ema tau <seconds>` | `~F,EMA,<정수 µs>\r` | 0.001..1 s, 입력 × 1,000,000 |
| `r filter 3d min <Hz>` | `~F,MIN,<정수 milliHz>\r` | 0.01..10 Hz, 입력 × 1,000 |
| `r filter 3d beta <계수>` | `~F,BETA,<정수 계수>\r` | 0..0.1 /mm, 입력 × 1,000,000 |
| `r filter 3d derivative <Hz>` | `~F,DERIVATIVE,<정수 milliHz>\r` | 0.01..10 Hz, 입력 × 1,000 |

`\r`는 실제 CR 한 바이트이며 LF를 붙이지 않는다. 값은 부호·지수 없는 유한 ASCII
십진수(`숫자+` 또는 `숫자+.숫자+`)만 받는다. Decimal 정밀도로 범위를 검사하고
배율 적용 결과가 정수로 정확히 표현되어야 한다. τ `0.10` → `100000`, min `0.5` →
`500`, β `0.001` → `1000`, derivative `1` → `1000`이며 τ `0.1000001`이나
min `0.5001`은 반올림 없이 거부한다. NaN/Infinity·부호·지수·원시 UART 문자열도 거부한다.

런타임 필터 명령을 구현한 **새 RIGHT BOOT이 필요**하다. PC 업데이트만으로는 이전
BOOT에 기능이 추가되지 않는다. 이전 BOOT에 새 프레임을 보내 지원 여부를 시험하지 않는다.
변경은 **RAM 전용이며 SD에 영속 저장하지 않는다**. 재부팅하면 해당 BOOT 기본값으로
돌아가며 운영 A의 기본값은 위의 τ=0.10 s/min=0.5 Hz/β=0.001 /mm/derivative=1 Hz다.
`ACK`/`FILTER` 응답으로 적용·조회 결과를 확인한다. PC `sent`는 UART write 성공이며
적용 확인이 아니다. `--filter commands`는 전송 대상의 응답 창 안에서 두 태그를 표시하고,
창 밖 응답도 디스크 전체 로그에는 남긴다.

외부 C는 약한 2D EMA τ=0.03 s → 2D One Euro → 삼각측량 → 운영 A와 같은 3D One Euro,
외부 G는 2D EMA τ=0.10 s → 삼각측량 → 상수 속도 Kalman3D로 3D One Euro를 대체한다.
C/G는 별도 비교 빌드이며 `r filter`로 프로필을 선택하는 문법은 없다. 해당 BOOT의 실제
지원·적용 결과는 응답으로 확인한다. 필터 명령은 PWM/비동기 추종을 활성화하지 않고
`--allow-motion-commands` 없이 허용한다. 하드웨어 ABI `0x77D4E3BB`, SD 가중치·SHA
sidecar, PWM/async 부팅 OFF와 matching/JUMP/원시 입력 승인/A2 안전검사 계약은 유지한다.

`C`는 진행 중인 CNN 프레임이 끝나면 연속 추론을 잠시 멈추고, VDMA 쓰기 채널을
계속 실행하면서 다른 버퍼로 잠시 park하여 완료된 버퍼를 복사하고, circular 모드로 복원한 뒤 PPM(P6, 1280×720)으로 저장한다.
기존 연속 추론이 켜져 있었다면 저장 후 자동으로 재개한다. SD 쓰기가 끝날 때까지
메인 루프가 동기적으로 대기하므로 로봇 제어 틱도 지연될 수 있다. 로봇 서보 전원을
분리하고 카메라 캘리브레이션용으로 사용할 것. 파일의 픽셀은 현재 PL/DDR 채널
배치(G-B-R)를 PPM 표준 R-G-B 순서로 바꿔 기록한다.

캡처 폴더 선택은 콘솔 기본 화면에서 `1` 또는 `2`를 누른다(Enter 불필요).
숫자 입력 하위 메뉴(`m`/`j`/`J`) 안에서는 해당 메뉴의 입력으로 처리된다.
선택만으로는 촬영하지 않으며, `C`로 저장할 때 폴더가 없으면 자동 생성한다.
`?` 또는 `t`로 현재 폴더를 확인할 수 있다. 촬영 대기 중에는 폴더 변경을 거부한다.
재부팅하면 선택은 `CALIB`으로 돌아가며 각 폴더에서 0001부터 빈 번호를 다시 찾는다.
기존 파일은 덮어쓰지 않는다. 폴더 전환 시 각 폴더의 번호를 따로 이어 간다.
예: `1` → `C` → `C`는 `CALIB/CAP0001.PPM`, `CALIB/CAP0002.PPM`,
이어서 `2` → `C`는 `JIG/CAP0001.PPM`(해당 번호가 비어 있을 때)로 저장한다.
SD 루트에 있던 이전 캡처는 그대로 두며 자동 이동하지 않는다.
좌우 보드의 폴더 선택과 번호는 독립적이다. 양쪽에 같은 폴더를 선택하고 실제 파일 쌍을 기록한다.
체커보드 `CALIB` 사진은 각 카메라 mono 보정과 좌우 stereo 보정에 공통 사용한다.

`D`는 되돌릴 수 없는 삭제 명령이다. 촬영 대기/추론 fault/stop 중에는 거부하고,
진행 중 CNN 프레임이 끝난 뒤 SD 삭제를 수행한다. VDMA 설정은 변경하지 않는다.
`CALIB`, 부트/가중치 파일, JIG 내 다른 이름의 파일/하위 폴더와 PC 백업은 삭제하지 않는다.
성공 응답은 `Frame capture: JIG deleted N files; next CAP0001.PPM`이다.
실패는 `Frame capture: JIG delete failed after N files ...`로 부분 삭제 건수를 알린다.
SD 작업 동안 메인 루프 제어 틱이 지연될 수 있으므로 캡처와 같은 카메라 검증 환경에서 사용한다.
PC에서는 `capture_stereo_uart.py --left COM3 --right COM4 --delete-jig` 또는
촬영 대기창의 `delete-jig`로 양쪽 지원 확인/삭제/결과 기록을 실행한다.
이 기능은 새 펌웨어를 양쪽 SD에 적용해야 사용할 수 있다.

### 촬영 시 VDMA 오류 진단 (2026-10-01 수정)

HDMI가 정상이어도 이전 펌웨어는 부팅 중 남은 VDMA 오류 비트를 보고
`VDMA configuration/error rejected`로 즉시 거부할 수 있었다. 이 문구만으로
실제 원인을 확정할 수는 없다. 수정 펌웨어는 다음처럼 구분한다.

- 매 촬영 시 `Frame capture: info S2MM SR=0x... CR=0x... stores=3 flush=1`을 기록한다.
- 실행 중인 채널의 과거 프레임/라인 크기 오류는 관측된 비트만 한 번 clear하고
  새 프레임이 실제로 들어오는지 확인한다. 과거 frame IRQ는 clear한 뒤 새 assertion을
  두 번 확인하며, 버퍼 번호가 바뀌어야 한다는 조건은 사용하지 않는다.
- **촬영 중 VDMA를 정지하지 않는다.** 현재 쓰기 버퍼를 관측하고 새 프레임 두 번을
  확인한 뒤, 다른 버퍼에 S2MM을 park한다. 새 프레임 두 번과 park 대상 도달을 확인하면
  보호된 원래 버퍼를 앱 DDR에 복사한다. 이후 circular 모드와 이전 park reference를
  복구한다. MM2S 설정, S2MM RUN/STOP 및 FrameCntEn은 변경하지 않는다.
- circular 복구 후와 SD sync 후에 각각 새 프레임이 들어오는지 확인한다. 파일 쓰기가
  끝났더라도 영상 진행 검증이 실패하면 `saved`를 보내지 않고 실패 파일을 제거한다.
  오류/timeout 경로에서도 park 설정을 복구하며 강제 stop/start/reset은 하지 않는다.
- 이 경로는 현재 빌드의 circular/free-running, IRQFrameCount=1을 요구한다.
- 정지된 채널, 버스/주소 오류, 단독 internal 오류는 clear하거나 강제 재시작하지 않는다.
  clear되지 않는 오류나 재발 오류도 저장을 거부한다. BSP 오류 마스크에서 빠진
  EOLLate(bit 15)도 검사한다.
- `info` 줄은 성공 응답이 아니다. 업데이트된 `capture_stereo_uart.py`는 이를 UART 로그에
  보관하고 최종 `saved`/오류 응답까지 기다린다. 펌웨어와 PC 스크립트를 함께 갱신한다.
- 실패 시 세션의 `left_uart.log`, `right_uart.log`에 있는 SR/CR 값을 확인한다.
  파일이 저장되려면 **양쪽 모두 `saved`**가 나와야 한다.

실물 관측(14:07 세션): 양쪽의 과거 오류 `0x90` clear 성공, 오른쪽 `CAP0003.PPM`
저장 성공. 왼쪽은 수동 정지 중 포인터 변경 `(1 -> 0)`을 거부하던 조건 때문에 실패했다.
이 실패 쌍의 오른쪽 파일은 stereo 보정 쌍으로 사용하지 않는다.

실물 관측(14:21~14:26): 첫 촬영은 파일 저장 성공 후 영상이 멈추고, 다음 촬영은
`SR=0x00011000` 또는 `0x00015000`으로 timeout했다. 사용자가 처음에는 영상이 정상이라고
보고했으나 이후 정지 화면임을 확인했다. 양쪽 재부팅으로 영상이 복구되었고,
첫 쌍 저장 성공 → 영상 정지 → 두 번째 쌍 실패가 재현됐다. 따라서 오류 비트가 없거나
재시작 API가 성공했다는 것만으로 실제 프레임 진행을 보장할 수 없다.
중간에 만든 선행 검사 제거/자동 정지 버전은 SD에 배포하지 않았다.

회귀 검증: 과거/치명 오류, live parking과 연속 촬영, 같은 버퍼 반복과 입력 없음의 구분,
park 실패/미도달, 복사 중 오류, circular 복구 후 영상 정지를 모의 검증한다.
테스트에서 DMA stop/start 또는 FrameCntEn 사용은 즉시 실패한다.
**live parking 방식은 실제 보드에서 연속 촬영과 HDMI 움직임을 다시 검증해야 한다.**

**바쁠 때 거부되는 명령**: CNN이 fault-latched 상태면 `x`(soft-reset) 외엔 전부 거부된다.
추론 중(`running`/연속모드/`s` 대기/정지처리 중)이면 `w`/`g`/`s`도 거부되고, 메시지에
현재 상태(`running/auto/single/stopping`)가 같이 찍힌다.

**부팅 시 자동 실행**: `cnn_app_init()` 끝에서 자동으로 `w`(가중치 로드) → 연속모드
`a` ON까지 수행한다(SD CSV 로깅은 자동 시작에서 비활성). 가중치 로드가 실패하면
자동 시작만 중단되고 콘솔 메뉴는 그대로 남는다.

## 3. 하위 메뉴 (`m`, `j`)

`m`/`j`를 누르면 필드를 하나씩 순서대로 물어본다. 각 필드에서:
- 숫자를 입력하고 **Enter** → 그 값으로 설정(범위 밖이면 즉시 취소하고 메뉴 종료)
- 아무것도 안 치고 **Enter만** → 현재 값 유지
- **Backspace**(0x08 또는 0x7F) → 마지막 입력 숫자 한 글자 지움

마지막 필드까지 입력하면 한꺼번에 적용된다(`j`는 pulse 범위/center 값 유효성도 같이
검사, 안 맞으면 "설정 변경 없음"으로 전체 취소).

- `m` 필드 13개: red/green/blue margin, yellow R-G 최대차/RG-B 최소차,
  red/green/blue 최소 밝기, yellow R/G 최소·B 최대 밝기(앞 11개 모두 0..255),
  최소 검출 taps(1..262143), color enable mask(0..15: R=1,B=2,G=4,Y=8).
  마지막 필드 완료 후 한꺼번에 적용한다. m 진입 시 연속 추론은 중단되며 완료 후 a로 재개한다.
  PC 모니터에서는 `r m` → `r value 40` 또는 `r enter`를 각 필드에 입력한다.
  양쪽 모두 일반 콘솔에서 `both m`을 보낼 수 있고, 이후 **같은 항목 프롬프트를 양쪽에서 확인한 경우에만**
  `both value 40`/`both enter`로 순차 진행한다. `value`는 ASCII 십진 숫자 1~6자리 + CR,
  `enter`는 CR 한 바이트이며 보드별 상태·허용 범위는 펌웨어가 판단한다.
  `both`는 동시 적용이 아니고 한쪽만 성공할 수 있다. 보드별 전송 바이트/실패를 기록하고 자동 재시도하지 않는다.
  항목이 엇갈리면 `l`/`r`로 각각 처리하며 PC 모니터 재실행은 보드 메뉴를 초기화하지 않는다.
  양쪽 applied 및 추론 OFF를 확인한 뒤에만 `both a`로 재개한다. a는 토글이므로 상태가 다르면 개별 재개한다.
- `j` 필드 18개: target X/Y, deadband X/Y, IIR shift, jump rejection, pan/tilt
  pixels-per-pulse-us, max target change, search step/confirm frames, motor slew,
  servo min/max us, pan/tilt center us, pan/tilt invert

부팅 기본값은 R/G/B margin **40/30/50**, red minimum brightness **100**이다.
red minimum brightness는 빨간 마커 검출 임계값이지 센서 노출 설정이 아니다.
수동 m 변경은 재부팅 후 보존되지 않으며 이 기본값 변경에는 새 BOOT 설치가 필요하다.
양쪽 빨강 설정 예: 1번 `both value 40`, 2~5번 각각 `both enter`, 6번 `both value 100`,
7~13번 각각 `both enter`. 매 전송 전 두 프롬프트를 확인하며 일괄 붙여넣지 않는다.
자세한 PC 문법·부분 실패 복구는 [UART 모니터 안내](stereo_uart_monitor_guide.md)를 따른다.

## 4. TRACE 로그 포맷

`ROBOT_TRACE` 빌드에서만 나온다. 한 줄이 한 레코드, 쉼표로 구분, 줄끝은 `\r\n`.
`#`으로 시작하는 줄은 컬럼 정의(스키마) — 부팅 직후와 이후 10초마다 다시 보낸다.
실수는 고정소수점 텍스트(각도 1자리, 그리퍼/CNN 통계 등 2자리, Point3D 3자리)이고
`%f`를 안 쓴다. 값이 없는 필드는 빈 칸으로 남는다(예: 타겟이 없는 프레임의 A1 각도 5개).

| 태그 | 주기 | 의미 |
|---|---|---|
| `A1` | 프레임마다(agent1_run 직후) | Agent1 출력: fid,t_ms,dur_us,dt_ms,pv,vm,rc,ov,er,ep,wp,wr,grip |
| `P3` | A1 바로 뒤 | Agent1 내부 3D 점 6개(어깨L/R,팔꿈치,손목,손가락1/2): fid,pm,fl,age_ms,x,y,z×6 |
| `A2` | agent2_run 직후 | fid,t_ms,dur_us,st,fg,unwrap된 타겟4,매핑된 명령5 |
| `TK` | 제어 틱마다(20ms) | tick,t_ms,dur_us,출력5,PWM5,rem,w,err |
| `SM` | 1초마다 | 파이프라인 누적 통계(아래 표) |
| `EV` | 상태 바뀔 때만 | t_ms,code,arg — 이벤트(BOOT, A1_LOST/BACK, A2_REJECT/BACK, SERVO_ERR, TICK_OVERRUN, UART_ERR, TRACE_DROP, TRACE_ON) |
| `CN` | CNN 프레임 완료마다 | fid,t_ms,seq,irq,elapsed_us,flags,overwritten |
| `CE` | CNN 오류마다 | fid,t_ms,error |
| `CAM` | CNN 프레임 완료마다 | fid,t_ms,state,pan_us,tilt_us,pan_target_us,tilt_target_us |
| `IN` | Agent1에 넘긴 프레임마다 | fid,t_ms,6점(x,y,valid)×6 — CNN 결과를 HumanPose2D로 바꾼 직후 값 |
| `CS` | 1초마다 | CNN 누적 통계: t_ms,irq,ok,error,timeout,last_us,max_us,overwritten |
| `RAW` | 좌우 CNN 완료마다 | 필터 전 좌표·검출 정보. 어깨 L/R, 사용 팔꿈치/손목, 빨강/초록 손가락 마커; 보드 세션/sequence/fid로 출처 추적 |
| `PAIR` | RIGHT가 좌우 쌍을 처리할 때 | 오른쪽 세션+쌍 순번으로 양쪽 세션/sequence/fid를 연결. 좌우 fid 동일 여부로 짝짓지 않음 |
| `PIX` | 처리 쌍마다 | 선택된 쌍의 좌우 2D 필터 후 좌표(운영 A는 시간 EMA). RAW와 비교해 필터 효과 확인 |
| `PG` | 처리 쌍의 입력 판정마다 | 해당 쌍의 입력 승인/거부와 이유. 승인과 실제 PWM 적용 성공은 별개 |
| `RQ` | 처리 쌍마다 | 추적 상태·재획득 후보 수·3D 필터 세대·좌우 수신 나이·수신 시각 차이 |

전체 컬럼 이름은 부팅 시 나오는 `#A1,...`/`#P3,...` 등 스키마 줄이 원본이다(코드:
`trace.c`의 `k_schema[]`) — 이 표는 요약이니 정확한 필드 순서가 필요하면 실제 수신한
스키마 줄을 봐라.

RAW/PAIR/PIX/PG/RQ는 2026-10-04 수정 소스의 새 BOOT이 필요하다. 개별 CNN fid는 보드마다 독립적이다.
모니터 로그 폴더와 보드 세션을 함께 보관하고 PAIR가 선택한 좌우 출처로 RAW↔PIX↔PG를 연결한다.
17개 전체 관절을 기록하지 않고 필요한 점만 남긴다. 손가락은 COCO 관절이 아니라 빨강/초록 컬러 마커다.
세션/쌍 식별자는 처리 출처 추적용이지 공통 노출 시간·동기화 검증값이 아니다.
IN/PIX는 원시 픽셀 로그가 아니므로 원시 튐 측정에는 RAW를 사용한다.
정확한 필드/단위/valid/score 의미는 실제 스키마와 [보드 간 UART 문서](stereo_board_uart.md)를 따른다.
commands/console 화면에서 새 주기적 태그는 숨기지만 수신한 전체 RAW/PAIR/PIX/PG/RQ는 파일에 보존한다.
TRACE_DROP/SM drop·ST qdrop이 증가하면 해당 측정 구간의 누락도 함께 보고한다.

비동기 LIVE 시험(`r A`)에서는 최신 RIGHT에 가장 가까운 수신 시각의 LEFT를 연결하며,
수신 나이 ≤250 ms·좌우 수신 시각 차이 ≤100 ms를 요구한다. 수신 시각은 노출 동기화가 아니다.
2D 큰 도약은 EMA 전에 보류하며 3개 원시 표본의 안정성을 확인한다.
운영 소스는 외부 G 제작 당시 전체 원본으로 복원했다. 기준은
`captures/acg_build_20261004_acg_final/source/`이며 운영 A는 One Euro를 유지한다.
같은 세션에서 이전 승인 위치 대비 elbow/wrist 변화 ≤150 mm이면 승인한다.
세션 변경·150 mm 초과는 `JUMP_HOLD` → `REACQ_WAIT` → `REACQ_ACCEPT`로 진행하며
첫 후보의 50 mm 이내인 서로 다른 3개 유효 쌍을 요구한다. 후보 간 간격은 ≤250 ms다.

같은 세션·재획득 아님·승인 간격 ≤250 ms이면 그 간격을 dt로 사용한다. 그 외에는
dt=0.1초이며 승인 공백 >250 ms이면 필터 epoch를 초기화한다. 무효점은 해당 필터를
초기화하며 필수점 출력 기하 탈락은 전체 필터, 손가락 출력 기하 탈락은 손가락 필터를 초기화한다.
최근 500 ms 승인/이력 보존·실제 필터 시각 분리·이동 예측 재획득은 제거했다.
A1/A2/PWM·신선도·150 mm·기하 검사는 유지한다. A 계수는 0.5 Hz/0.001/mm/1 Hz다.

복원 전 소스·문서·테스트는 `captures/restore_G_baseline_20261004/before/`에 보관했다.
기존 SD의 최신 A BOOT은 소스 복원만으로 바뀌지 않는다. 칼만 UART 조정은 외부 G 전용이며
[G 전용 설명서](../tools/stereo_vision/filter_acg/g_variant/uart_tuning/README.md)를 따른다.
G 시험은 RIGHT BOOT만 교체하며 LEFT·가중치 BIN/SHA·XSA/비트스트림/PACK_ID는 유지한다.
`RQ,rsid,pair,state,candidates,filter_epoch,l_age_us,r_age_us,gap_us`로 이 상태를 확인한다.

2026-10-04 사용자 확인 기구 치수: 전완 16 cm·손목~손끝 20 cm,
팔꿈치 원점은 테이블보다 10 cm 높으며 FK의 테이블면은 `z=-10 cm`다.
관절 방향/가동범위는 유지한다. 링크 중심선 모델이므로 실물 접촉 여유를 보증하지 않는다.

**A2의 `st` 값**: `N`=새 목표 승인, `S`=직전과 동일(재계획 생략), `R`=안전검사 거부,
`V`=validate 거부, `-`=이번 프레임 실행 없음. `fg`는 거부 사유 플래그(hex) —
`FOREARM_SAFETY_CHECK_*`(`forearm_safety_check.h`).

**SM 필드 순서**: `t_ms,fr(frames_in),tv(targets_valid),acc(commands_accepted),
rej(commands_rejected),rt(retargets),tk(ticks),sw(servo_writes),se(servo_errors),
ovr(tick_overruns),crc,fmt,rng(UART 패킷 오류 — 지금은 항상 0, 자세 입력이 UART가
아니라서),ow(overwritten),drop(trace_dropped),hi(링버퍼 최대 사용 바이트)`.

## 5. 출력 끄고 켜기

- `q`: **모든** UART 출력(콘솔 print + TRACE 전부) 끔/켬. CNN 추론과 로봇 제어 자체는
  계속 동작 — 화면만 조용해진다. 다시 켜면 스키마를 다시 보낸다.
- `z`: **로봇 TRACE만**(`A1/P3/A2/TK/SM/EV`) 끔/켬. `CN/CE/CAM/IN/CS`(CNN 쪽)는
  영향 없음 — CNN 브링업하면서 로봇 로그만 조용히 하고 싶을 때 쓴다.
- 코드 레벨: `trace_set_output_enabled()`(전체) / `trace_set_robot_output_enabled()`
  (로봇 태그만) — `line_begin()`이 태그로 `robot` 플래그를 판정한다(`trace.c:226`).

## 6. 세션 읽는 법 (실전 팁)

1. 부팅 직후 `#`로 시작하는 스키마 줄 + `EV,0,BOOT,<baud>`가 나온다. 이게 없으면
   ROBOT_TRACE가 아예 안 켜진 빌드다.
2. `CNN automatic startup: w -> camera FIXED -> a` 배너 이후 `CNN command PASS`가
   나오면 가중치 로드 성공, 이어서 `CNN continuous mode: ON (automatic boot)`이 뜬다.
3. 로봇 쪽만 보고 싶으면 `z`로 로봇 TRACE만 켠 채로 CNN 태그(`CN/CE/CAM/IN/CS`)는
   계속 보이는 상태 유지 가능.
4. `EV,...,TRACE_DROP,...`이 보이면 링버퍼(8192B)가 넘쳐서 줄이 통째로 버려진 것 —
   출력량이 UART 대역폭보다 많다는 뜻. `SM`의 `hi` 필드로 최근 1초간 최대 사용량을
   확인할 수 있다.
5. `A2`의 `st=R`이 반복되면 `fg` hex 값으로 어떤 안전검사에 걸렸는지 확인
   (`FOREARM_SAFETY_CHECK_SELF_COLLISION`=0x2, `_TABLE_COLLISION`=0x4,
   `_INVALID_COMMAND`=0x1).
