# 로봇0·로봇1 UART 명령 요약

## 대상과 실행

| 접두어 | 보드 | 로봇 | 출력 |
|---|---|---|---|
| `r` | RIGHT / main / COM4 | 로봇0 | JB CH0..CH4 |
| `l` | LEFT / sub / COM3 | 로봇1 | JC CH0..CH4 |
| `both` | 양쪽 보드 | 두 로봇에 순차 전송 | 정확히 같은 순간의 실행을 보장하지 않음 |

명령은 아래 모니터를 실행한 터미널에 입력한다. 대문자와 소문자를 구분한다.

```powershell
python tools/stereo_vision/monitor_stereo_uart.py --left COM3 --right COM4 --interactive --allow-motion-commands --filter commands --format plain
```

## 로봇 개별 제어

| 기능 | 로봇0 | 로봇1 | 의미 |
|---|---|---|---|
| 상태 | `r V` | `l V` | PWM·모드·로봇 역할 조회. 실제 관절 위치는 아님 |
| PWM 켜기 | `r E` | `l E` | LIVE에서 안전검사 후 출력 활성화. 팔이 움직일 수 있음 |
| 추종 상태 | `r T` | `l T` | RIGHT 스테레오 / LEFT 각도 수신·추종 상태 |
| 추종 시작 | `r A` | `l A` | 해당 로봇만 실시간 추종. PWM ON·LIVE 필요 |
| 추종 중지 | `r S` | `l S` | 새 목표 차단. 마지막 승인 목표까지 이동 후 유지 |
| PWM 끄기 | `r X` | `l X` | 해당 로봇 PWM·추종 OFF. 보드·서보 전원을 차단하는 명령은 아님 |
| 수동 집게 열기 | `r O` | `l O` | PWM ON·LIVE에서 자동 그리퍼 대신 열림 유지. 물건을 놓을 수 있음 |
| 자동 집게 복귀 | `r H` | `l H` | 수동 열림 해제. 실제 자동 판정은 추종·신선한 관측이 필요 |

`S`는 급정지가 아니다. `X` 후에는 팔이 처질 수 있으므로 받친다.
LEFT 추종에는 양방향 UART0와 RIGHT의 스테레오 A1 계산이 필요하지만 로봇0 PWM ON은 필수가 아니다.
LEFT 레코드 재생은 자기 SD에서 수행하며 LEFT CNN 좌표 송신은 유지한다.

## 레코드 개별 제어

| 기능 | 로봇0 | 로봇1 |
|---|---|---|
| 녹화 시작·종료 토글 | `r R` | `l R` |
| 목록·재생 중지 | `r P` | `l P` |
| 목록 조회 | `r record list` | `l record list` |
| 이름 지정 | `r record name grab_1` | `l record name grab_1` |
| 번호 선택·재생 직접 요청 | `r record select 1` | `l record select 1` |
| 삭제 요청 | `r record delete 1` | `l record delete 1` |
| 삭제 확정 | `r record confirm 1` | `l record confirm 1` |
| 이름·선택·삭제 안내 취소 | `r record cancel` | `l record cancel` |

- 승열3에서 `R`은 준비 요청이다. 마지막 승인 목표 정착·정지 seed 기록 뒤 실제 녹화 시작 응답을 기다린다.
  준비 중 `R`은 취소다. 준비 전 ON이던 추종만 이어지고 OFF였던 추종은 자동 활성화되지 않는다.
  종료 `R` 후에는 마지막 목표까지 정착하는
  꼬리를 녹화한 뒤 이름을 묻는다. 이름을 입력하고 SD 저장 완료 응답을 기다린다.
- 이름 안내에서는 `grab_1` + Enter도 가능하다. 영문·숫자·`_`·`-` 1~24자만 허용한다.
- `P`는 PWM OFF일 때 목록만 보여준다. 재생 선택에는 `E` 성공과 `P` 목록이 필요하다.
- 목록에서 번호 한 번은 선택, 같은 번호를 다시 누르면 재생한다. Enter는 필요 없다.
- 선택 후 Backspace는 삭제 요청, 보드의 삭제 확인 안내 후 Enter는 실제 삭제다. Esc로 취소한다.
- 재생 중 해당 보드의 `P`는 반복 중지다. 실시간 추종은 자동 재개하지 않으므로 필요하면 `A`를 보낸다.
- 양쪽 목록이 함께 있으면 대상 보드를 확인한다. 직접 `l/r record ...` 명령을 쓰면 혼동을 피할 수 있다.
- 레코드는 자기 SD `MOTION`에 저장한다. 같은 이름·번호여도 두 보드에서 독립적이다.
- 초기 정렬과 반복 사이 복귀 시간이 추가된다. 기록된 각도 명령이며 실제 위치 피드백은 아니다.

## 두 로봇 공통 명령

| 입력 | 동작 |
|---|---|
| `both V` | 양쪽 PWM·모드·역할 조회 |
| `both E` | 양쪽 PWM 활성화 요청. 각 보드가 별도로 안전검사하며 둘 다 성공했는지 확인 |
| `both T` | 양쪽 추종 상태 조회 |
| `both A` | 양쪽 추종 시작 요청. 둘 다 PWM ON·LIVE일 때 사용 |
| `both S` | 양쪽 신규 추종 목표 차단. 레코드 재생 중지 명령은 아님 |
| `both X` | 양쪽 PWM·추종 OFF. 팔을 받치고 필요하면 서보 전원도 직접 차단 |
| `both O` | 양쪽 수동 집게 열림 요청. PWM ON·LIVE 필요 |
| `both H` | 양쪽 수동 집게 열림 해제 |
| `both ?` | 양쪽 펌웨어 도움말 |

`both P`와 `both R`도 단일 키로 전송되지만 상태별 토글이므로 한쪽은 시작하고 다른 쪽은
중지할 수 있다. **추종·재생을 섞는 시연에서는 각 보드에 따로 입력한다.**
`both record ...`는 지원하지 않는다. `both S`만으로 재생이 멈췄다고 판단하지 않는다.

## 시연 순서

### 로봇0 실시간 추종 + 로봇1 basic_1 반복 재생

```text
r V
r E
r T
r A
l V
l E
l P
```

LEFT 목록에 `1 basic_1`이 뜬 뒤 `1`을 두 번 누른다. `l A`나 `r P`는 필요 없다.

### 로봇1도 실시간 추종

```text
l P
l V
l E
l T
l A
```

첫 `l P`는 로봇1이 재생 중일 때만 재생 중지로 사용한다. 이미 LIVE라면 생략한다.
선택 메뉴가 열려 있다면 `l record cancel`로 나와야 한다.

### 양쪽 PWM 끄기

```text
both X
```

## CNN·마진·로그 명령

아래도 `r`을 `l` 또는 지원되는 `both`로 바꿀 수 있다. 소문자 명령은 로봇 대문자 명령과 다르다.

| 입력 예 | 의미 |
|---|---|
| `r t` / `both t` | CNN 상태·설정 기능 지원 조회 |
| `r a` / `both a` | CNN 연속 추론 ON/OFF 토글. 로봇 추종 `A`와 다름 |
| `r m` / `both m` | RGBY 설정 메뉴. 진입 시 CNN 연속 추론 OFF |
| `r value 20` / `both value 20` | 현재 숫자 항목에 20을 입력·확정 |
| `r enter` / `both enter` | 현재 값을 유지하고 다음 항목 |
| `r settings show` / `both settings show` | 현재 RGBY·해당 필터 설정 조회 |
| `r settings save` / `both settings save` | 자기 SD에 설정 저장 |
| `r settings load` / `both settings load` | 자기 SD의 설정 복원 |
| `r q` / `both q` | 보드 UART 출력 mute/resume 토글. 로봇·CNN은 멈추지 않음 |
| `r z` / `both z` | 로봇 TRACE 출력만 토글 |
| `q` 또는 Ctrl+C | PC 모니터 종료. 보드 로봇 정지 명령을 자동 전송하지 않음 |

`both m/value/enter`는 양쪽 메뉴의 현재 항목이 같을 때만 사용한다.
설정 저장·복원 전에는 `both t` 등으로 지원을 확인하고, 해당 로봇 PWM OFF·CNN OFF·LIVE·메뉴 없음
조건을 만족해야 한다. `a`는 토글이므로 현재 ON/OFF를 먼저 확인한다.
메뉴 완료 후 CNN이 OFF이면 해당 보드의 소문자 `a`로 재개한다.

## 로봇1 닫힘 상한 시험판

2400µs 시험판은 LEFT SD 설치·파일 검증 완료 / 실제 부팅·발열 미검증 상태다.

`stereo_G_robot1_gripper_cap2400_20261007/left/BOOT.BIN` 설치 후:

- 로봇1 그리퍼만 기존 변환 결과가 2400µs를 넘으면 2400µs로 제한한다.
- 완전 열림 1500µs, 상한 이하 출력, 나머지 관절과 로봇0 출력은 그대로다.
- 초기 자세·실시간 추종·레코드 재생에 같은 제한을 적용하며 레코드 파일은 수정하지 않는다.
- `l V`에 `[GRIP_PWM] robot=1 close_max_us=2400 policy=CLAMP_ONLY`가 표시된다.
- 2400µs는 명령상 약 171°에 대응할 뿐, 실제 집게 각도·온도·전류를 측정한 값은 아니다.
- 이번 상한은 2300µs 시험판보다 닫힘 끝을 늘린 것이다. 별도 LEFT SD 설치와 재부팅이 필요하다.
- 먼저 충분히 식힌 빈 집게로 확인한다. 여전히 무리하게 닫히거나 과열되면 중지·서보 전원 차단 후
  실제 닫힘 끝점·서보 혼·기계 간섭·전원 규격을 점검한다. 이 제한이 과열 방지를 보증하지 않는다.

상세 설명: `stereo_uart_monitor_guide.md`.
