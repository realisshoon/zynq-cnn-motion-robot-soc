# 승열3 Unity UART 계약

작성일: 2026-10-08. 대상은 좌우 보드가 각각 자기 로봇을 제어하는 승열3 통합 펌웨어다. 이 문서는 Unity 팀의 직렬통신 구현용이며 Unity GUI 구현 자체는 이번 작업 범위에 포함하지 않는다. 승열3 신규 녹화 경계와 `[REC_STATE]`의 빌드·native 회귀 결과 및 보드 적용·실물 시험 상태는 [릴리스 문서](seungyeol3_release.md)에 기록한다. 최신 역할별 시험 BOOT의 SD 파일 검증은 완료됐으나 실제 보드 부팅·실물 동작은 미검증이다. 통합 소스 재빌드의 파일 검증과 기존 SD 설치 이력은 구분한다.

문서의 소스 링크는 저장소 통합 후 `robot_arm/docs/`에서 정상 `src/`, `include/`, `config/`, `tools/`를 가리키도록 작성했다. 외부 실험 폴더·captures·harness 자료는 Git에 포함해야 하는 런타임 의존성이 아니다.

## 1. 포트와 실제 송신 바이트

| 연결 | 용도 | 설정 |
|---|---|---|
| LEFT USB 콘솔 / PS UART1 | LEFT 보드 명령·응답, 해당 보드의 로봇 | 8N1, TRACE 빌드 921600 baud |
| RIGHT USB 콘솔 / PS UART1 | RIGHT 보드 명령·응답, 해당 보드의 로봇 | 8N1, TRACE 빌드 921600 baud |
| USB 콘솔 / TRACE 없는 빌드 | 같은 콘솔 프로토콜 | 8N1, 115200 baud |
| 보드 간 PS UART0 | 스테레오 좌표·제어 전달 | 기본 115200 baud, USB 콘솔과 별개 |

USB 콘솔 baud는 [platform_vitis.c](../src/integration/platform_vitis.c)와 [trace.h](../include/integration/trace.h), 보드 간 baud는 [stereo_board.c](../src/integration/stereo_board.c)의 실제 빌드 설정을 따른다. 모니터 `--baud` 기본값은 921600이다. COM 번호는 연결 환경에서 식별하며 특정 번호를 프로토콜에 고정하지 않는다. Unity와 다른 터미널이 같은 COM 포트를 동시에 소유하지 않도록 한다.

`l`, `r`, `both`는 Python 모니터가 해석하는 **호스트 라우팅 문법**이다. 보드에는 이 접두어와 공백을 보내지 않는다. 보드별 상태·목록·선택 번호는 독립적이며 `both`는 왼쪽과 오른쪽에 차례로 쓰는 동작이다. 동시 실행·원자적 실행·동작 동기화를 보장하지 않는다.

| 사람이 모니터에 입력하는 문자열 | Unity가 해당 포트로 보내는 실제 바이트 |
|---|---|
| `r E` + 호스트 Enter | RIGHT에 `E`, hex `45` |
| `l R` + 호스트 Enter | LEFT에 `R`, hex `52` |
| `r P` + 호스트 Enter | RIGHT에 `P`, hex `50` |
| `r V` + 호스트 Enter | RIGHT에 `V`, hex `56` |
| `r record list` | RIGHT에 `!REC,LIST\r` |
| `l record name grab_1` | LEFT에 `!REC,NAME,grab_1\r` |
| `r record select 02` | RIGHT에 `!REC,SELECT,2\r` |
| `r value 25` | RIGHT에 `25\r`; 설정 하위메뉴에서만 사용 |
| `r enter` | RIGHT에 CR 한 바이트, hex `0D` |

표의 `\r`는 문자 `\\`와 `r`가 아니라 **실제 CR `0x0D`**이다. `!REC,LIST\r`의 hex는 `21 52 45 43 2C 4C 49 53 54 0D`이다. 단일 키에는 CR·LF를 붙이지 않는다. 프레임과 설정 하위메뉴 입력에는 CR만 붙인다. 일부 설정 메뉴가 LF도 받아들이더라도 Unity 송신은 CR로 통일한다. `!REC`, `~F`, `@CFG` 프레임은 LF를 종료자로 사용하지 않는다.

보드 명령은 대소문자를 구분한다. `R`은 녹화, `r`은 CNN 결과 조회이고 `P`는 목록/재생 중지, `p`는 CNN 식별 조회다. `X`는 로봇 PWM OFF, `x`는 CNN 정지/리셋이며 `V`는 로봇 상태, `v`는 카메라 상태다. 프레임 토큰은 `REC,NAME`, `CFG,SHOW`, `F,SHOW`처럼 대문자로 보낸다. 이름은 입력한 영문 대소문자를 보존한다.

## 2. Unity에서 상태를 판단하는 기준

승열3의 권위 있는 녹화·재생 상태는 다음 보드 응답이다. `motion_library_report_state(controller,pipeline,force)`를 통해 main에서 캐시된 상태 변경 시 출력하고 `V` 조회에서는 강제로 출력한다. 주기적 heartbeat나 명령별 ACK 번호가 아니다.

```text
[REC_STATE] schema=1 side=l|r ui=IDLE|STOP_RECORD|STOP_MENU|NAME|MENU|SAVE|START_RECORD mode=LIVE|RECORDING|ALIGNING|PLAYING|HOLDING pwm=0|1 ram_samples=<n> entries=<n> selected=<id> playing=<id-or-0> delete=<id-or-0> unsaved=0|1
```

예시이며 실제 수신·실물 시험 로그는 아니다.

```text
[REC_STATE] schema=1 side=r ui=NAME mode=HOLDING pwm=1 ram_samples=200 entries=2 selected=1 playing=0 delete=0 unsaved=1
```

| 필드 | 의미 / Unity 처리 |
|---|---|
| `schema` | 현재 1. 미지원 schema에서는 동작 버튼을 비활성화하고 상태 재확인 |
| `side` | 보드 역할 `l`/`r`. 연결 포트와 다르면 포트 매핑부터 재확인 |
| `ui` | 녹화 라이브러리 작업 단계. 아래 UI 표로 버튼 제어 |
| `mode` | 실제 제어 모드. `ui`와 함께 읽고 둘을 동일한 상태로 취급하지 않음 |
| `pwm` | 로컬 로봇 PWM 활성 상태. 카메라 PWM이나 물리적인 토크 측정값이 아님 |
| `ram_samples` | RECORDING에서는 record_count, 그 외에는 replay_count. SD에서 읽은 재생 샘플도 포함할 수 있으며 파일 저장 완료·물리 이동량의 증거가 아님 |
| `entries` | 보드의 유효 SD 목록 항목 수. 이름·샘플 수는 LIST 응답으로 별도 조회 |
| `selected` | 선택된 SD 번호. 미선택은 0; 현재 재생 중인 번호와 별개 |
| `playing` | 현재 재생 동작에 해당하는 번호, 없으면 0. ALIGNING도 재생 과정에 포함 |
| `delete` | 삭제 확인 대기 번호, 없으면 0 |
| `unsaved` | 현재 기록/저장 대기 워크플로의 미저장 표시. 준비 단계·명시 CANCEL 뒤 0일 수 있으며 RAM 샘플이 0이라는 뜻이 아님. 1이면 미저장 표시·대체 전 사용자 의도 확인 |

하위 호환 응답도 남긴다. 새 Unity는 `[REC_STATE]`를 우선하고, 오래된 `[REC]` 상태 숫자를 새 상태로 덮어쓰지 않는다.

현행 역할별 설정은 RIGHT/로봇0 elbow roll 거울 방향 `+1`, LEFT/로봇1
elbow pitch 가속도 60°/초²·최고속도 30°/초다. 명령 바이트와 보드 간
프로토콜 버전은 바꾸지 않았다. LEFT `V` 응답에는 다음 제한 진단이 포함된다.

```text
[GRIP_PWM] robot=1 close_max_us=2400 open_min_us=1722 policy=CLAMP_ONLY; no current or temperature feedback
```

Unity는 키 이름으로 해석하며 필드 순서·알 수 없는 추가 키에 의존하지 않는다.
이는 PWM 제한이지 실측 위치·전류·온도가 아니다. 열림 1722µs는 설정상 약
110°이고 닫힘 상한은 2400µs다. 기존 SD 레코드의 각도·정규화 집게 값은
자동 반전·재타이밍하지 않으며, `basic_1`처럼 새 가속도 상한을 초과한 파일은
재생 전에 거부한다. 오류에서 자동 재생 재시도나 안전검사 우회를 하지 않는다.

샘플 수 증가만으로는 캐시된 상태 변경 응답을 매 틱 출력하지 않는다. `ram_samples`는 해당 줄을 만든 시점의 값이며 매 20 ms 스트리밍 진행률이 아니다. 필요하면 V로 현재 값을 조회하고 GUI 타이머는 실제 녹화 준비 완료를 기준으로 관리한다.

```text
[REC] entries=<n> selected=<id> ui=<numeric>; r record list
```

`numeric` 매핑은 `IDLE=0`, `STOP_RECORD=1`, `STOP_MENU=2`, `NAME=3`, `MENU=4`, `SAVE=5`, `START_RECORD=6`이다. 이 구형 줄에는 mode·PWM·미저장 여부가 없으므로 이것만으로 녹화/재생 버튼을 허용하지 않는다. 호스트가 `S`/`X`를 보냈거나 화면 메뉴를 닫았다는 이유로 보드 `NAME`을 끝났다고 추정하지 않는다.

수신은 바이트 버퍼에 누적하고 CRLF로 완성된 줄을 나눈다. 한 번의 Serial read가 한 줄이라는 가정을 하지 않는다. TRACE·CNN·카메라 로그와 명령 응답은 같은 스트림에 섞인다. 원시 줄에서 `[REC_STATE]`를 파싱하고, 호스트가 붙인 시간·`[L]`/`[R]` 표시나 한글 화면 변환문을 보드 프로토콜로 해석하지 않는다. 필드 순서에 의존하지 않고 필요한 키를 검사하며 미래의 추가 키는 무시할 수 있다. 설정 하위메뉴의 `Enter=keep]: ` 프롬프트는 개행 없이 나올 수 있다.

연결·재연결·수신 누락 후에는 로컬 버튼 상태를 미확인으로 바꾸고, 수신 프레임/설정 하위메뉴가 없는 상태에서 해당 포트에 `V`, `T`, `t`를 각각 조회한다. `V`는 REC_STATE/로봇, `T`는 추종, `t`는 CNN 및 지원 기능을 확인한다. 목록이 필요하면 `!REC,LIST\r`를 보낸다. 조회 자체는 녹화·재생·추종·PWM을 시작하거나 RAM을 버리지 않는다. 앱 시작·종료·포트 재연결에 `E`, `A`, `R`, `P`, `CANCEL`, `SELECT`를 자동 송신하지 않는다.

## 3. 주요 로봇 키와 GUI 조건

아래 GUI 조건은 잘못된 전송을 줄이기 위한 클라이언트 정책이다. 펌웨어는 실제 관절 범위·승인 목표·속도/가속도·FK·HAL 등 더 많은 조건을 검사하므로 버튼 활성화가 성공 보장이 아니다. 소스 기준은 [main_integration.c](../src/integration/main_integration.c), [cnn_app_event.c](../src/integration/cnn_app_event.c), [motion_library.c](../src/record_replay/motion_library.c)다.

| 키 / 호스트 예 | 동작 | GUI 허용 / 차단 기준 |
|---|---|---|
| `E` / `l E`, `r E` | 해당 보드 로봇 PWM ON 요청 | 최신 `ui=IDLE mode=LIVE`; 기존 정지 승인 명령 정합·안전 검사 필요. 비LIVE·gripper catchup이면 거부될 수 있음. `E`로 추종은 켜지지 않음 |
| `X` / `l X`, `r X` | 해당 보드 로봇 PWM OFF, 추종 차단, 토크 해제 | 사용자 정지 요청은 진행 단계와 관계없이 제공. 프레임 중에는 아래 ESC+X 사용. 팔을 지지해야 하며 SD 대기 중 즉시 처리 시간 보장 없음 |
| `V` / `l V`, `r V` | 로봇 PWM·RR·라이브러리 상태 조회 | 녹화 라이브러리의 모든 UI 단계에서 조회용. 미완성 프레임/숫자 하위메뉴는 먼저 입력 경계를 복구 |
| `A` / `l A`, `r A` | 로컬 추종 ON 요청 | `ui=IDLE mode=LIVE pwm=1`, gripper catchup 없음. LEFT는 RIGHT가 보낸 사람 각도 추종, RIGHT는 비동기 양안 시험. 녹화·준비·정지·저장·재생 단계에서는 차단; 움직임을 녹화하려면 R 전에 A 승인 확인 |
| `S` / `l S`, `r S` | 새 추종 목표 입력 OFF | 마지막 승인 목표는 기존 제한대로 완료하고 유지. PWM 토크 해제·재생 중지·녹화 저장 명령이 아님. 프레임 중에는 ESC+S 사용 |
| `T` / `l T`, `r T` | 로컬 추종 상태 조회 | 조회용. 노출 동기화 성공 여부와 동일하지 않음 |
| `R` / `l R`, `r R` | 녹화 준비 요청, 준비 취소 또는 활성 녹화 종료 요청 | 시작은 최신 `IDLE + LIVE + pwm=1`에서 제공. `START_RECORD`에서는 명시적인 준비 취소 버튼으로 R 제공. `IDLE + RECORDING`에서는 종료 버튼 제공. 다른 UI 전환/저장/재생 단계에서는 차단하고 응답 대기 |
| `P` / `l P`, `r P` | 목록 진입 또는 재생 중지 | 목록은 IDLE/MENU에서; RECORDING/START_RECORD/STOP_RECORD/NAME/SAVE에서는 차단. ALIGNING/PLAYING 및 오류 HOLDING에서는 중지 용도로 제공 |
| `O` / `l O`, `r O` | 로컬 그리퍼 수동 열림, `H`까지 유지 | GUI는 `IDLE + LIVE + pwm=1`에서 제공. 물체가 떨어질 수 있음. 수동 열림에서는 P/SELECT가 거부됨 |
| `H` / `l H`, `r H` | 자동 그리퍼 제어 복원 | GUI는 `IDLE + LIVE + pwm=1`에서 제공. 성공 응답 후 목록/재생 재시도 |

R 준비는 기존 async/remote-follow 요청을 보존한 채 목표 입력만 gate한다. R 직전에 추종 ON이면 준비 완료 뒤 gate를 풀어 그 요청을 이어가므로 별도 A가 필요 없다. OFF였으면 OFF를 유지한다. 움직임을 녹화하려면 R 전에 A를 명시 요청하고 T/승인 응답으로 확인한다. Unity는 R 전송 성공을 추종 ON의 증거로 삼지 않는다. 미저장 기록이 있으면 새 녹화/SD 로드 전에 대체 의도를 사용자에게 확인한다. MENU/NAME에서 다른 작업을 시작하려고 자동 CANCEL을 보내지 않는다.

`E`, `X`, `A`, `S`는 ON/OFF 방향이 명시된 명령이지만 전체 UART에 transaction id, exactly-once 처리, 재시도 중복 제거가 있는 것은 아니다. `R`, `P`, `a`, `q`, `u`, `i`, `k`는 상태에 따라 동작이 바뀌는 토글/다중 용도 키다. 특히 `P` 재전송은 목록 요청이 재생 중지로 바뀔 수 있다. 연결 오류·짧은 write·응답 timeout 때 자동 재전송하지 말고 상태를 조회한다. 명시 방향 명령도 실제 상태/승인 검사가 필요하다. `Serial.Write` 성공이나 모니터 `sent`는 보드 적용 완료가 아니다.

## 4. 녹화: 준비 → 실제 기록 → 종료 정착 → 이름 → 저장

1. 해당 포트의 `V`, `T`로 상태를 확인한다. PWM OFF이면 사용자가 실제 자세를 확인한 뒤 `E`를 요청하고 성공 응답을 확인한다. 움직임 녹화에 필요한 추종은 R 전에 사용자 A 요청으로 활성화한다. 좌우 로봇의 녹화는 독립적이다.
2. `R` 한 바이트를 한 번 보낸다. 승열3는 `ui=START_RECORD`에서 새 라이브 목표를 차단하고 마지막 승인 목표가 안전하게 정착하기를 기다린다. 이 준비 구간의 이동을 새 녹화 앞부분에 넣지 않는다. GUI에는 **녹화 준비 중**으로 표시하고 실제 녹화 타이머를 아직 시작하지 않는다.
3. 준비는 연속 3개의 새 HAL 적용 틱에서 정착 조건을 확인한 뒤 정지 seed 샘플 2개를 기록하고 입력 gate를 푼다. 보드의 실제 녹화 시작 안내와 최신 `ui=IDLE mode=RECORDING`을 확인한 뒤 **녹화 중**으로 바꾼다. 내부 mode만 잠깐 RECORDING이거나 R 요청이 accepted라는 사실만으로 준비 완료를 판정하지 않는다. 이후 기존 20 ms 제어 틱에서 성공한 출력 명령을 기록한다. CNN 수신 주기나 Unity 화면 프레임마다 기록하는 방식이 아니다. R 전에 추종이 ON이면 이어가고 OFF이면 계속 OFF이므로 별도 A를 자동 전송하지 않는다.
4. 사용자가 종료하면 `R`을 한 번 보낸다. `ui=STOP_RECORD`에서 새 목표를 차단하고, 마지막 승인 목표·속도·그리퍼가 기존 제한대로 정착하는 **마지막 tail**을 계속 기록한다. 이 단계는 아직 저장 완료가 아니다. GUI에서 R/P/NAME/SELECT/삭제를 차단한다.
5. 정착과 기록 검증이 끝나면 `ui=NAME`, 보통 `mode=HOLDING` 및 `unsaved=1` 상태로 이름 입력을 허용한다. `!REC,NAME,grab_1\r`를 보낸다. 단순히 `grab_1\r`만 보내는 입력은 보드 NAME 프로토콜이 아니다.
6. `ui=SAVE`에서는 자세를 유지한 채 SD 저장·검증·게시를 진행한다. 완료는 `[REC] SAVED id=... name=... samples=...`와 최신 REC_STATE로 확인한다. 성공 후 `MENU + HOLDING`으로 유지하며 새 추종/재생은 자동 시작하지 않는다. 실패는 `[REC] SAVE_FAILED` 후 NAME으로 돌아가며 RAM 기록을 보존하므로 사용자가 이름을 다시 보내 재시도할 수 있다. 저장 시작 응답·샘플 수만으로 완료 처리하지 않는다.

START_RECORD에서 사용자가 다시 R을 누르면 준비를 취소하며 기존 SD 항목을 덮어쓰지 않는다. GUI는 이 R을 일반 시작 버튼 재전송과 구분하여 **준비 취소**로 표시한다. 취소 후 실제 상태는 REC_STATE/T로 확인한다. 앱이나 timeout이 임의로 이 R을 보내지 않는다.

정착 판정은 소프트웨어 궤적 속도와 출력/마지막 적용 명령의 일치 및 새 HAL 적용을 사용한다. 전류/엔코더 센서의 실측 정지가 아니다. 연속 3개 20 ms 틱의 안정 조건을 요구하며, 이미 정착한 뒤 확인에 필요한 시간과 목표까지 남은 이동 시간은 다르다. 실제 준비 시간에는 남은 승인 목표까지의 이동과 안정 확인·seed 기록이 포함되므로 전체 시간을 60 ms 또는 고정 지연으로 표현하지 않는다.

총 RAM 한도는 기본 2048 샘플, 20 ms × 2048 = 40,960 ms다. 승열3는 한도를 모두 자유 녹화에 쓰지 않고 안전하게 종료할 tail 공간을 미리 확보한다. reserve는 현재 vmax/amax에서 관절의 최대 가동 envelope, 그리퍼 전 범위의 변화 속도, 추가 8틱 여유로 계산한다. 따라서 일반 추종 녹화 한계는 총 한도보다 짧고 최종 파일에는 정착 tail도 들어간다. 실제 녹화 시작 안내에서 `live_max_ms`, `tail_reserve_ms`, `total_max_ms`를 보고하므로 GUI는 이 값을 표시하고 고정 자유 녹화 시간이나 reserve를 하드코딩하지 않는다. 현재 release_v2/좌우 native 검증의 보고 값은 일반 기록 한계 **1748 샘플 / 34.96 s**, tail reserve **300 샘플 / 6.00 s**, 총 **2048 샘플 / 40.96 s**다. live_max_ms는 record_count 기준이므로 시작 seed 2샘플도 포함한다. reserve는 확보 공간이고 실제 tail 전체를 항상 300샘플 기록하는 것은 아니다. 자동 종료 시험에서는 tail 포함 1829 샘플 / 36.58 s의 유효 기록을 저장·재생했다(모의 HAL/FatFS 시험이며 실제 SD/서보 시험이 아님). 한도가 가까워졌다는 이유로 기존 안전 제한을 풀거나 끝 샘플을 강제로 보정하지 않는다. 정착/샘플 검증 실패 시 저장을 거부하고 원인을 표시한다.

승열3는 기록의 시작/끝 경계를 보완한다. 손목·입력 필터·tracking·JUMP·gripper latch·속도/가속도·FK/범위/HAL 안전 경로를 바꾸지 않는다. 서보 떨림이 해소되었다거나 기록 명령과 실물 이동이 일치한다는 주장은 하지 않는다.

## 5. `!REC` 프레임 전체 계약

프레임은 ASCII, 토큰 대문자, 공백 없음, 마지막 CR이다. 번호는 1..32의 십진수 1~2자리다. 이름은 `[A-Za-z0-9_-]{1,24}`이며 공백·한글·UTF-8 확장 문자·콤마를 허용하지 않는다. 같은 이름의 기존 유효 항목은 거부하며 이름 비교는 대소문자를 구분한다. 슬롯 번호는 좌우 보드마다 별개의 ID다.

| 호스트 입력 / wire | 보드 효과 | GUI 허용 상태 및 완료 확인 |
|---|---|---|
| `l/r record list` / `!REC,LIST\r` | 유효 SD 목록 출력 | 숫자 설정 하위메뉴/미완성 프레임 외 조회 가능. 목록만 받아도 보드 MENU 진입이나 재생 허용을 의미하지 않음 |
| `l/r record name <name>` / `!REC,NAME,<name>\r` | 미저장 RAM 기록의 SD 저장 시작 | `ui=NAME`, 기록 검증/저장 안전 조건 충족 시만. `[REC] SAVED` 또는 `[REC] SAVE_FAILED` 대기 |
| `l/r record select <id>` / `!REC,SELECT,<id>\r` | 파일 로드·검증 후 시작 자세 ALIGN, 반복 재생 | `ui=MENU mode=HOLDING pwm=1`, 최신 LIST에 존재, 수동 그리퍼 열림 해제. `[REC] PLAY id=...`와 REC_STATE 대기 |
| `l/r record delete <id>` / `!REC,DELETE,<id>\r` | 삭제 확인 대기 번호 설정 | `ui=IDLE` 또는 `MENU`, PWM OFF 또는 안전 정착 HOLDING, 현재 유효 항목. `delete=<id>`와 confirm 안내 확인 |
| `l/r record confirm <id>` / `!REC,CONFIRM,<id>\r` | 확인 대기와 일치하는 항목 삭제 | DELETE와 같은 저장 안전 조건, `delete`가 같은 ID. `[REC] DELETED` 확인; 실패/부분 삭제는 목록 재조회 |
| `l/r record cancel` / `!REC,CANCEL\r` | 메뉴/이름 작업 종료 또는 삭제 확인 해제 | GUI는 NAME/MENU 또는 정착 IDLE의 삭제 확인 해제에서 제공. START_RECORD/STOP_RECORD/STOP_MENU/RECORDING/ALIGNING/PLAYING/SAVE에서는 차단. 새 추종·PWM 자동 ON 없음 |

`CANCEL`은 저장 명령도 SD 항목 삭제 명령도 아니다. 현재 저장 대기 워크플로를 끝내며 `unsaved=0`으로 바뀌어도 SD 저장 성공을 의미하지 않는다. RAM 바이트가 남아 있을 수 있으나 이후 명시적 녹화/로드 작업이나 재부팅으로 대체/소실될 수 있음을 취소 전에 안내한다. 앱 종료나 입력 timeout을 CANCEL로 변환하지 않는다. S/X의 **UART 입력 취소**와 이 프레임의 **보드 녹화 UI 취소**를 구분한다.

목록 응답은 기존 형식을 유지한다.

```text
[REC] SD records; select: r record select <number>
[REC] 1 basic_1 samples=200 duration_ms=4000 selected
[REC] 3 grab_2 samples=150 duration_ms=3000
[REC] count=2; delete: r record delete <number>; cancel: r record cancel
```

LIST의 시작 줄부터 `count=` 줄까지 한 목록으로 모아 해당 보드의 캐시를 교체한다. ID에 빈 번호가 있을 수 있으므로 행 순번을 ID로 보내지 않는다. `duration_ms=samples*20`은 파일 샘플의 시간이고 첫 ALIGN·반복 간 ALIGN 시간은 포함하지 않는다. 알려진 거부/실패에는 `UI_BUSY`, `PWM_OR_MODE`, `SETTLE_RESERVE_CONFIG`, `STILL_SETTLING`, `SAVE_BUSY`, `MANUAL_OPEN`, `select rejected`, `play rejected`, `DELETE_FAILED`, `DELETE_PARTIAL_REQUIRES_SD_REVIEW`가 있다. 기존 영어 `[REC]` 줄은 유지하며 세부 문자열은 사용자 안내에 쓰되 현재 버튼 상태는 REC_STATE로 복구한다.

NAME 요청 오류는 `NAME_STATE`(이름 입력 단계 아님), `UNSAFE_STORAGE`(저장 안전 조건 불충족), `REPLAY_VALIDATION`(RAM 기록 검증 실패), `NAME_FORMAT`, `DUPLICATE`, `LIBRARY_FULL`, `IO`로 구분한다. `UNSAFE_STORAGE`·`REPLAY_VALIDATION`은 RAM을 보존하며, 형식/중복/슬롯 부족/IO 거부도 SD 저장 완료가 아니고 RAM을 보존한다. NAME_STATE를 다른 상태에서 저장이 이루어졌다는 뜻으로 해석하지 않는다. 수정 가능한 이름 오류는 실제 NAME 상태를 확인한 뒤 사용자 재입력으로 재시도하고, 라이브러리/IO/검증 오류는 원인을 해결한 뒤 재요청한다.

## 6. 재생: 목록 → 선택 → ALIGN → 반복 → 중지

사용자가 `P`를 요청하면 새 추종 목표를 차단하고 `STOP_MENU`에서 마지막 승인 목표를 정착시킨 후 `MENU + HOLDING`으로 진입한다. PWM OFF 상태의 P는 목록을 보여줄 수 있지만 곧바로 SELECT를 허용하지 않는다. 먼저 IDLE/LIVE로 복구한 상태에서 사용자 `E` 승인, 다시 P 및 MENU/HOLDING 확인이 필요하다.

목록에서 고른 실제 ID를 `!REC,SELECT,3\r`로 한 번 보낸다. SD BIN의 형식·ABI·길이·CRC·각 샘플 안전 검사를 통과해야 시작한다. `[REC] PLAY ... repeat=1`은 재생 요청 승인이다. 로봇은 먼저 기존 속도/가속도와 gripper 제한으로 Sample0까지 ALIGN하고, 정착한 뒤 PLAYING에서 저장 샘플을 50 Hz로 출력한다. 통합 앱은 반복 ON이며 마지막 샘플 뒤 다시 Sample0까지 안전 ALIGN한 후 반복한다. 처음과 반복 사이에 순간 점프하거나 Unity가 반복 SELECT를 보내는 방식이 아니다.

ALIGNING/PLAYING에서 `P` 한 바이트는 재생 중지 요청이다. 최신 REC_STATE와 `[REC] playback stop=1 mode=LIVE` 응답으로 확인한다. 마지막 성공 명령을 기준으로 LIVE로 복구하고 과거 추종 목표를 버리며 추종은 자동 재개하지 않는다. 새 추종은 사용자의 `A` 요청이 필요하다. `S`만으로 재생 중지를 대신하지 않는다. `X`는 PWM 토크 해제 경로이며 같은 의미의 정착 중지가 아니다.

ALIGN timeout·샘플 오류·틱 overrun·HAL 실패 등은 HOLDING/거부로 이어질 수 있다. 오류 시 자동 반복 재시작·자동 A·자동 E를 하지 않는다. `V/T`로 실제 상태를 복구하고 원인·현재 PWM 상태를 표시한다. SELECT 응답이 늦어도 보드가 이미 ALIGN에 들어갔을 수 있으므로 자동 재전송하지 않는다. 모니터의 선택 응답 대기 5초는 호스트 UI timeout이며 보드 ALIGN timeout/실물 동작 완료 시간이 아니다.

## 7. 입력 중 정지, ESC와 timeout

승열3 기반 펌웨어는 부팅 및 `t`/로봇 상태 응답 경로에서 아래 지원 정보를 출력한다. 보드 역할과 포트가 맞는지 확인한다.

```text
[UART] stop_escape=1 protocol=ESC_X_S frame_timeout_ms=250 role=1
```

LEFT는 `role=1`, RIGHT는 `role=2`다. 지원 확인 후 Unity의 사용자 정지 송신은 다음처럼 한다. CR/LF를 추가하지 않는다.

| 요청 | 실제 바이트 | 의미 |
|---|---|---|
| 새 추종 입력 차단 | hex `1B 53`, 즉 ESC + `S` | 미완성 `!REC`/`~F`/`@CFG` 및 숫자 설정 입력을 취소하고 S 실행 |
| 로봇 PWM OFF | hex `1B 58`, 즉 ESC + `X` | 같은 입력 취소 후 X 실행 |

ESC는 UART 프레임과 CNN 숫자 하위메뉴를 취소한다. **보드 녹화 UI의 NAME/MENU를 취소하지 않는다.** NAME에서 ESC+S/ESC+X 후 화면 이름 입력을 지워도 보드는 여전히 NAME일 수 있다. 후속 `V`의 REC_STATE를 기준으로 재표시하고 이름 작업을 끝내려면 사용자가 `!REC,CANCEL\r`를 요청한다. `S`/`X` 문자만 미완성 프레임 안에 보내면 프레임 문자로 소비될 수 있어 입력 탈출을 보장하지 않는다. ESC만 보낸 뒤 즉시 V를 보내면 ESC 다음 제어 처리에 의해 V가 버려질 수 있으므로 ESC+S/X의 완전한 두 바이트를 사용한다.

미완성 프레임이 250 ms 넘게 유휴이면 입력을 취소하고 남은 꼬리는 CR·새 프레임 시작·정지 경계까지 버린다. 이 timeout은 보드의 녹화 NAME을 닫거나 RAM을 버리는 규칙이 아니다. 정상 프레임은 한 write로 연속 송신한다. 펌웨어는 foreground polling이고 SD 호출이 UART 처리를 지연시킬 수 있으므로 ESC도 물리적 긴급정지 장치처럼 즉시 처리 시간을 보장하지 않는다.

기존 Python 모니터는 `l/r/both X`와 `S`를 ESC 접두어와 함께 전송하며 구형 BOOT 지원 미확인 시 경고한다. 모니터 목록 화면의 키보드 **Esc**는 호스트 메뉴 동작으로 `!REC,CANCEL\r`를 보내는 경우가 있어 wire `0x1B`와 같지 않다. Unity는 별도 버튼으로 녹화 UI 취소·추종 정지·PWM OFF를 구분한다.

## 8. 필터·CFG·카메라 및 기타 콘솔 명령

### RIGHT 필터

현재 소스는 RIGHT 필터 조회에서 실제 backend와 `one_euro_tunable`을 보고한다. 승열3는 필터 선택·설정값을 바꾸는 릴리스가 아니다. G backend에서 One Euro 명령을 전송할 수 있는 파서가 있다고 적용 성공을 가정하지 않는다. LEFT는 `REJECTED_RIGHT_ONLY`다.

| 모니터의 확인된 입력 | wire | 단위/범위 |
|---|---|---|
| `r filter show` | `~F,SHOW\r` | 현재 backend/설정 조회 |
| `r filter default` | `~F,DEFAULT\r` | 해당 빌드 기본 EMA/Kalman 설정 요청 |
| `r filter 2d ema tau 0.10` | `~F,EMA,100000\r` | 초 × 1,000,000, 1000..1000000 µs |
| `r filter 3d min 0.5` | `~F,MIN,500\r` | Hz × 1000, 10..10000 milliHz; One Euro 지원 시 |
| `r filter 3d beta 0.001` | `~F,BETA,1000\r` | /mm × 1,000,000, 0..100000; One Euro 지원 시 |
| `r filter 3d derivative 1` | `~F,DERIVATIVE,1000\r` | Hz × 1000, 10..10000 milliHz; One Euro 지원 시 |

확인한 모니터 파서는 위 고수준 입력을 제공하며 임의 raw 프레임 입력이나 Kalman 고수준 별칭을 제공하지 않는다. Unity가 보드로 직접 보낼 수 있는 Kalman 토큰은 `~F,KXY,<n>\r`, `KZ`, `KACC`, `KVEL`이다. KXY/KZ는 표준편차 mm × 1000, 정수 100..1000000이며 KXY는 X/Y 양쪽에 적용한다. KACC는 가속도 표준편차 mm/s² × 1000, KVEL은 초기 속도 표준편차 mm/s × 1000, 각각 정수 0..5000000이다. KVEL은 다음 seed의 초기값이며 현재 이력을 즉시 초기화한다는 뜻이 아니다. 실제 지원은 `[FILTER]`의 backend/값/결과를 확인한다.

모니터의 소수 입력은 부호·지수 없는 ASCII 십진수이며 배율 적용 결과가 정수로 정확해야 한다. 반올림해서 보내지 않는다. 응답은 `[FILTER] result=APPLIED`, `REJECTED_UNAVAILABLE`, `REJECTED_RIGHT_ONLY` 또는 `REJECTED`와 설정 보고다. 필터 변경은 RAM이며 명시 CFG SAVE로 영속화한다. GUI는 녹화/재생/정착/저장 중 필터 변경을 차단하고 조회만 제공한다. CNN 숫자 하위메뉴 중에는 프레임이 `MENU_ACTIVE`로 거부될 수 있다. 필터 명령은 PWM/추종을 자동 ON하지 않는다.

### CFG 영속 설정

| 모니터 입력 | wire | 조건/응답 |
|---|---|---|
| `l/r/both settings show` | 대상마다 `@CFG,SHOW\r` | `[CFG]`로 현재 설정 조회 |
| `l/r/both settings save` | 대상마다 `@CFG,SAVE\r` | PWM OFF, LIVE/IDLE, CNN OFF·완전 유휴, 메뉴/녹화/재생/캡처 없음; `[CFG] SAVED` 확인 |
| `l/r/both settings load` | 대상마다 `@CFG,LOAD\r` | SAVE와 같은 조건; `[CFG] LOADED` 또는 실패/롤백 결과 확인 |

지원 정보는 `[CFG] supported=1 role=1|2 protocol=@CFG schema=1; show/save/load`다. 모니터는 이 정보를 확인하기 전 CFG 프레임을 차단한다. Unity도 구형 BOOT에 신규 프레임을 보내 지원 여부를 추측하지 않는다. CNN OFF 확인은 `t`의 `auto=0 running=0 single=0 stopping=0` 및 fault/하드웨어 상태를 함께 보고, `a`는 토글이므로 상태를 확인한 뒤 사용한다. `m`이 연속 추론을 끄더라도 메뉴가 열린 상태에서는 SAVE/LOAD할 수 없다.

CFG 파일은 `0:/UARTCFG.BIN`이며 `.NEW`/`.BAK`, 역할·ABI·CRC 검증을 사용한다. 저장 대상은 RGBY 검출 설정과 RIGHT의 EMA/Kalman 설정이고 로봇 pose·PWM ON/OFF·추종 ON/OFF·녹화 라이브러리·카메라 설정 전체를 저장하는 기능이 아니다. LOAD/부팅 복원은 PWM/추종을 자동 켜지 않는다. 파일 복원이 실패하면 응답을 확인하고 현재 설정/기본값 유지 여부를 조회한다.

### 카메라 및 숫자 설정 메뉴

카메라 PWM은 로봇 PWM과 다른 IP다. `v` 조회가 `V`를 대신하지 않는다.

| 단일 키 | 실제 효과 |
|---|---|
| `u` | 카메라 추적/PWM 토글. ON→OFF에서는 카메라 PWM도 OFF |
| `f` | 추적 OFF, 카메라 PWM ON, 현재 pulse에서 FIXED |
| `v` | 카메라 tracker/PWM 상태 조회 |
| `h` | pan/tilt 중앙 정렬 요청, 움직일 수 있음 |
| `i`, `k` | 각각 pan/tilt 방향 반전 토글 |
| `j` | 카메라 설정 숫자 하위메뉴 |
| `m` | RGBY 설정 숫자 하위메뉴; 연속 추론 OFF 요청 후 진입 |
| `J` | body joint 표시 설정 숫자 하위메뉴 |

GUI는 녹화/재생/정착/저장 중 카메라 설정/이동·검출 설정 변경을 차단한다. 조회 버튼은 별도로 둔다. 숫자 메뉴는 프롬프트 하나를 확인한 뒤 `숫자\r` 또는 `\r`로 현재값 유지 요청을 보내며 마지막 필드 뒤 적용한다. 숫자 입력에서 일반 R/P/V 키를 섞지 않는다. ESC+S/X는 입력 메뉴를 취소할 수 있지만 녹화 NAME과 다른 메뉴임을 구분한다.

RGBY `m`의 필드 순서는 red/green/blue margin, yellow R-G 차이, yellow R/G-B 최소 gap, R/G/B 최소 밝기, yellow 최소 R/최소 G/최대 B, min detected taps, color mask다. 앞 11개는 0..255, taps는 1..262143, mask는 0..15이며 R=1, B=2, G=4, Y=8이다. `J`는 joint index 5..16, show 0/1의 두 필드다.

카메라 `j`의 순서는 target X 0..1279, Y 0..719, horizontal deadband 0..320, vertical deadband 0..180, IIR shift 0..5, jump px 20..640, pan/tilt px per pulse-us 각각 1..64, target delta us/frame 1..200, search step 1..50, search confirm frames 1..20, slew us/20ms 1..100, servo min us 500..1800, max us 1200..2500, pan/tilt center us 각각 500..2500, pan/tilt invert 각각 0/1이다. min < max이며 center는 해당 범위 안이어야 한다. 범위 오류 시 메뉴 종료/설정 유지 응답을 확인한다. Unity에 수치를 하드코딩하는 경우 실제 펌웨어 프롬프트와 함께 검증한다.

### 기타 콘솔 지원 범위

`?` 도움말, `p` CNN identity, `t` CNN 상태/지원 정보, `r` 마지막 CNN 결과, `d` 진단, `o` overlay/color 레지스터 조회가 있다. `w` SD 가중치 로드/검증과 `g` image SG descriptor 생성/검증은 추론 유휴에서만 가능하다. `s` 1프레임, `a` 연속 추론 토글, `x` 추론 정지/soft-reset은 로봇 녹화·PWM 명령과 별개다.

`b` HDMI overlay 모드, `B` body joint 표시, `c` grouped-color 시험, `n` green detection, `y` yellow detection은 진단/검출 변경이다. `1`/`2`는 CALIB/JIG 캡처 폴더 선택, `L`은 JIG 삭제 지원 조회, `C`는 PPM 사진 SD 저장, `D`는 JIG 사진 전체 삭제다. 사진 저장/삭제는 SD와 제어 틱을 지연시킬 수 있어 GUI는 녹화/재생/정착/저장 중 차단하고 로봇 비구동 작업으로 분리한다. `D`는 `!REC,DELETE`와 대상이 다르다.

`q`는 보드 전체 UART 출력 토글이며 제어는 계속된다. 모니터에 대상 없이 입력한 `q`는 호스트 종료이고 보드로 전송되지 않는다. `r q`는 RIGHT 보드 출력 토글이다. `z`는 로봇 TRACE만 토글한다. Unity가 상태 응답을 필요로 할 때 전체 출력 mute를 자동 사용하지 않는다. 무응답을 PWM OFF/정지로 판정하지 않는다.

## 9. Python 모니터와 Unity의 책임 구분

[monitor_stereo_uart.py](../tools/stereo_vision/monitor_stereo_uart.py)의 명령 입력에는 `--interactive`가 필요하다. 확인한 모니터의 `--allow-motion-commands` 대상은 `R P E X A u f j h i k O H` 및 `!REC,SELECT,...`다. `S`와 조회·LIST/NAME/삭제 프레임은 이 motion gate 목록에 없더라도 보드 상태 검사를 따른다. 이 옵션은 보드 인증/안전 우회 기능이 아니며 Unity가 같은 플래그를 wire로 보내는 것도 아니다.

모니터의 숫자 목록 UI·두 번 번호 선택·Backspace 삭제 요청·Enter 확인은 호스트 UI 규칙이다. 보드의 SELECT/DELETE/CONFIRM 프로토콜에는 이 키보드 순서를 보내지 않는다. 대상 없는 숫자 입력은 모니터 기본 RIGHT 라우팅이므로 Unity는 선택한 포트와 실제 ID를 명시한다. 기존 모니터의 `[REC]` 문구 기반 표시 상태는 정지/재연결 때 보드와 어긋날 수 있어 승열3용 parser/Unity는 REC_STATE로 복구해야 한다. 최종 모니터 parser 회귀 결과는 릴리스 표에 기록한다.

양쪽 송신 중 한쪽만 성공하면 결과를 좌우별로 표시하고 자동 재시도하지 않는다. SD 저장처럼 시간이 걸리는 동작은 요청 대기 상태를 두고 `[REC]` 결과와 REC_STATE로 완료를 판정한다. timeout 숫자는 실측 전 **TBD**이며, timeout은 작업 취소/보드 정지의 증거가 아니다.

## 10. SD 형식과 실제로 알 수 있는 것

파일은 보드 SD의 `0:/MOTION/R0001.BIN`, `R0001.TXT`부터 최대 32 슬롯이다. TXT는 이름만 저장하며 개행을 붙이지 않는다. NEW/NTX는 새 저장 staging, DEL은 삭제 staging이다. 저장은 기존 항목을 덮어쓰지 않는 빈 슬롯 생성·sync·재읽기 검증·rename을 사용한다. 이 절차가 전원 차단 전체에 대한 원자성을 보장하는 것은 아니며 실패/잔여 staging은 SD 검토가 필요할 수 있다.

BIN은 기존 MRP1 형식을 유지한다. 24-byte 헤더는 magic `MRP1`, version 1, tick 20000 µs, sample count, ABI `0x77D4E3BB`, payload CRC32의 순서다. 뒤에는 [MotionSample](../include/record_replay/motion_record_replay.h)의 float 5개, 20 byte/샘플을 기존 ARM 파일 표현으로 저장한다: elbow_roll_deg, elbow_pitch_deg, wrist_pitch_deg, wrist_roll_deg, gripper_norm. 별도 timestamp·Unity 좌표·UI 필드를 삽입하지 않는다. gripper는 0..1 정규화 값이다. 로그 표시 정밀도로 반올림한 값을 저장하는 포맷으로 바꾸지 않는다.

파일에 저장되는 것은 제어 틱에서 검증하고 성공 적용한 **명령**이다. 서보 전류·엔코더 실측 위치·접촉력 피드백은 이 UART/기록 계약에 없다. `pwm=1`, HAL 성공, pose settled, 저장/재생 완료는 실물 위치 일치·기계적 떨림 해소·물체 파지 성공의 증명이 아니다. PWM OFF 중 실제 처짐/수동 이동을 피드백으로 검증하지 못하므로 E와 재생 시작은 사용자의 실제 상태 확인이 필요하다.

소스 기준: [motion_library.c](../src/record_replay/motion_library.c), [motion_record_replay.c](../src/record_replay/motion_record_replay.c), [motion_sd.c](../src/record_replay/motion_sd.c), [cnn_app.c](../src/integration/cnn_app.c), [cnn_console.c](../src/integration/cnn_console.c), [stereo_filter_command.c](../src/stereo_vision/stereo_filter_command.c), [uart_settings.c](../src/integration/uart_settings.c), [camera_tracking_app.c](../src/cnn_firmware/camera_tracking/camera_tracking_app.c), [dual_arm_config.h](../config/dual_arm_config.h).
