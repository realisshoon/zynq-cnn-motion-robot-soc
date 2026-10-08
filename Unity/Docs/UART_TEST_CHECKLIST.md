# UART 시험 기록표

작성일 2026-10-08. 이 문서는 팀원이 기록할 빈 시험표이다. **아래 표는 로봇팀의 새 시험 회차용이므로 모두 NOT TESTED로 유지한다. 이번 개발 PC Clean Clone 검증은 하단 별도 이력으로 구분한다. 따라서 새 PC·실물 시험을 완료했다는 의미가 아니다**이다. 개발 PC의 과거 PASS를 팀원 PC/실물 PASS로 복사하지 않는다.

실제 보드 명령과 TK 수신은 현 Unity UI에 연결되어 있지 않다. STEP3/4는 로봇팀이 검증한 별도 모니터에서, STEP5는 Unity 후속 구현 후 수행한다. 승인 전 E/A 및 실제 TX를 활성화하지 않는다.

## 시험 회차 공통 기록

| 필드 | 기록 |
|---|---|
| 회차 ID / 시험 일자 / 담당자 | 미기입 |
| PC / OS | 미기입 |
| Unity 버전 / Render Pipeline | 미기입 |
| Git 전달 Commit SHA | 업로드 후 확정 필요 |
| Player 경로 / SHA256 | 미기입 |
| LEFT BOOT 버전 / SHA256 / 소스 | 미기입 |
| RIGHT BOOT 버전 / SHA256 / 소스 | 미기입 |
| LEFT COM / Baud / 8N1 | 미기입 |
| RIGHT COM / Baud / 8N1 | 미기입 |
| 검사 전 실제 PWM / FOLLOW / 로봇 지지 상태 | 미기입 |
| 실물 시험 승인자 / 승인 시각 / 전원 차단 담당자 | 미기입 |

각 아래 행은 이 회차의 모든 환경 필드를 상속한다. PC/펌웨어/포트를 바꾸면 새 회차를 만든다. 개별 항목에 일자/담당자·결과·증거·후속 조치를 반드시 채운다.

## STEP 1 — Unity 실행

| ID / 시험·판정 기준 | 일자 / 담당자 | 결과 | 로그·캡처 경로 | 문제 / 후속 조치 |
|---|---|---|---|---|
| 1.1 Unity 6000.3.24f1 clean import, Console 오류0 | — | NOT TESTED | — | — |
| 1.2 Demo07 Scene 로드, Missing Script/Material 없음 | — | NOT TESTED | — | 인계본의 첫 활성 Scene=Demo07 확인 |
| 1.3 Windows x64 Player 실행, DLL 로드 정상 | — | NOT TESTED | — | — |
| 1.4 Robot Arm 표시·Grid·조명·도구 정상 | — | NOT TESTED | — | — |
| 1.5 Humanoid 표시, 오른팔 ACTIVE/왼팔 preview 구분 | — | NOT TESTED | — | — |
| 1.6 Manual M0~M4 작은 범위, Applied/Approved/Requested 확인 | — | NOT TESTED | — | 실제 TX OFF |
| 1.7 CSV Recorded / XYZ A / XYZ B 샘플·Pause/Step/EOF 확인 | — | NOT TESTED | — | 저장 각도/XYZ 모드 구분 |
| 1.8 MENU/Controls Hide/Show/Presentation 복귀·카메라 유지 | — | NOT TESTED | — | — |

## STEP 2 — Mock UART UI

| ID / 시험·판정 기준 | 일자 / 담당자 | 결과 | 로그·캡처 경로 | 문제 / 후속 조치 |
|---|---|---|---|---|
| 2.1 Mock 표기, 기본 PWM OFF/FOLLOW OFF, 실물 연결 없음 | — | NOT TESTED | — | — |
| 2.2 PWM OFF에서 A 거부, E 후 PWM ON | — | NOT TESTED | — | 로봇은 움직이지 않아야 함 |
| 2.3 A 후 FOLLOW ON, S 후 FOLLOW OFF/PWM 유지 | — | NOT TESTED | — | 실물 HOLD 시험과 구분 |
| 2.4 X 후 PWM OFF/FOLLOW OFF | — | NOT TESTED | — | 실제 토크 상태 증거 아님 |
| 2.5 Robot0/Robot1 Mock 상태 독립 | — | NOT TESTED | — | COM 라우팅 아님 |
| 2.6 RGB 40/30/50 변경·Apply, 송신 없음 표시 | — | NOT TESTED | — | — |
| 2.7 Camera SHOW/HIDE/Size1~3, MOCK PREVIEW 표기 | — | NOT TESTED | — | HDMI 입력 아님 |
| 2.8 임시 POSE3D Mock sample 수신/오류/중복/stale | — | NOT TESTED | — | 보드 TK 시험 아님 |

## STEP 3 — 실제 UART 조회만

별도 모니터 사용. 실제 BOOT 지원 확인 전 Unity의 레거시 M0= TX는 사용하지 않는다. 한 포트당 하나의 프로그램만 연다.

| ID / 시험·판정 기준 | 일자 / 담당자 | 결과 | 로그·캡처 경로 | 문제 / 후속 조치 |
|---|---|---|---|---|
| 3.1 RIGHT USB UART1 COM·Robot0 JB 식별 | — | NOT TESTED | — | — |
| 3.2 LEFT USB UART1 COM·Robot1 JC 식별 | — | NOT TESTED | — | UART0와 구분 |
| 3.3 BOOT SHA·921600/115200 baud·8N1 확인 | — | NOT TESTED | — | — |
| 3.4 V 응답 PWM/역할/REC_STATE, 자동 E/A 없음 | — | NOT TESTED | — | — |
| 3.5 T 응답 follow·PWM 구분 | — | NOT TESTED | — | — |
| 3.6 t 응답 CNN 상태/지원 기능 | — | NOT TESTED | — | — |
| 3.7 분할 줄/혼합 로그/timeout·disconnect=UNKNOWN 처리 | — | NOT TESTED | — | 무응답을 OFF로 처리 금지 |

## STEP 4 — 로봇팀 승인 후 실물

S는 마지막 승인 목표까지 움직인 뒤 유지한다. X는 토크를 해제해 팔이 처질 수 있고 물리 비상정지 장치가 아니다. 실제 전원 차단 경로를 먼저 마련한다.

| ID / 시험·판정 기준 | 일자 / 담당자 | 결과 | 로그·캡처 경로 | 문제 / 후속 조치 |
|---|---|---|---|---|
| 4.1 실물 지지/무하중/가동 공간·물리 차단 경로 확인 | — | NOT TESTED | — | 승인자 필수 |
| 4.2 E 명시 승인, 응답 확인, 급격한 catch-up 없음 | — | NOT TESTED | — | IDLE/LIVE/실제 자세 확인 |
| 4.3 A 명시 승인, 작은 동작에서 방향·제한 확인 | — | NOT TESTED | — | PWM ON·허용 상태 |
| 4.4 S 후 신규 목표 차단·최종 명령 자세 유지·PWM 유지 | — | NOT TESTED | — | 즉시 정지로 판정 금지 |
| 4.5 X 후 PWM OFF 응답, 지지 상태에서 토크 해제 확인 | — | NOT TESTED | — | 낙하·처짐 관리 |
| 4.6 RGB m 13 prompt/범위/CR·완료·CNN 상태 재조회 | — | NOT TESTED | — | 필요할 때만 별도 승인 |
| 4.7 연결 유실 및 입력 프레임 중단 시 대응 절차 확인 | — | NOT TESTED | — | 물리 안전장치 별도 |

## STEP 5 — 후속 구현 이후 Unity 동기화

현 버전은 TK/상태 파서가 없으므로 이 단계는 구현 전 BLOCKED / 시험 결과는 NOT TESTED이다.

| ID / 시험·판정 기준 | 일자 / 담당자 | 결과 | 로그·캡처 경로 | 문제 / 후속 조치 |
|---|---|---|---|---|
| 5.1 동일 BOOT TK·V/T 원시 로그를 Unity가 수신 | — | NOT TESTED | — | 파서 연결 필요 |
| 5.2 M0~M4 단위/순서/범위·PWM w/err·valid 해석 | — | NOT TESTED | — | 명령/실측 분리 |
| 5.3 보드 명령과 Unity 표시 수치 비교, 재보정 중복 없음 | — | NOT TESTED | — | 구버전 DLL과 구분 |
| 5.4 오른팔·왼팔 direction/offset/부착 profile 비교 | — | NOT TESTED | — | 왼팔 제어 미연결 |
| 5.5 S/HOLD/PWM OFF 표시 및 마지막 승인 목표 처리 | — | NOT TESTED | — | — |
| 5.6 trace drop/mute/reconnect/역순/session·stale 처리 | — | NOT TESTED | — | 정책 확정 필요 |
| 5.7 실물과 Unity 동작 비교, 센서 피드백 아님 명시 | — | NOT TESTED | — | — |

## 결과 기록 규칙

PASS는 해당 회차에서 실제 시험하고 로그/캡처를 연결한 항목만 사용한다. FAIL은 관측 실패와 재현 조건을 남긴다. 장치 미연결·미구현·승인 대기·실행 안 함은 NOT TESTED와 이유를 남긴다. 기존 16.41° wrist 비교 차이나 미검증 calibration을 이번 표로 해결됐다고 바꾸지 않는다.

상세 근거: [UART 인계서](ROBOT_TEAM_UART_HANDOFF.md), 시작 절차: [Quick Start](ROBOT_TEAM_QUICK_START.md).

## 추가 — Pcam/HDMI/UART 경계 검증

동일 회차 환경 기록을 사용한다. 문서/코드 경로의 정적 감사와 아래 하드웨어 시험은 별개다.

| ID / 시험·판정 기준 | 일자 / 담당자 | 결과 | 로그·캡처 경로 | 문제 / 후속 조치 |
|---|---|---|---|---|
| P.1 BOOT/XSA/bitstream/weight pack SHA·Pcam 모델·배선 확인 | — | NOT TESTED | — | final_uart0.xsa 기준과 대조 |
| P.2 OV5640/MIPI 상태·VDMA DDR frame·오류·실제 FPS 확인 | — | NOT TESTED | — | 픽셀 데이터는 UART 아님 |
| P.3 CNN IRQ/timeout·joint/marker valid 및 frame sequence | — | NOT TESTED | — | PL 연산과 PS 결과 읽기 구분 |
| P.4 UART0 좌우 pairing·calibration·strict/async·노출 timing | — | NOT TESTED | — | receipt 시간=노출 동기 아님 |
| P.5 Agent1/2/3→HAL→PWM와 TK w/err 비교 | — | NOT TESTED | — | PWM OFF 변환값 구분 |
| P.6 UART1 양방향 조회/응답과 명시 승인 명령 확인 | — | NOT TESTED | — | Unity 연결 후 수행; 현재 Mock |
| P.7 Startup INPUT=UART / 패널=UART / COMMUNICATION 확인 | — | NOT TESTED | — | 개발 PC Clean Clone 확인 완료; 이 행은 팀원 새 시험용 |
| P.8 HDMI→USB Capture→Windows 실제 영상 장치/FPS/지연 | — | NOT TESTED | — | 현재 Unity Preview Mock |
| P.9 실제 Unity 영상 업로드·TK와 영상 타이밍 비교 | — | NOT TESTED | — | 장치 통합 후 수행 |


## 개발 PC Clean Clone 검증 이력 — 팀원/실물 시험표와 구분

2026-10-08, Unity 6000.3.24f1 / URP 17.3.0 / Windows x64. 새 Library를 생성한 별도 `dev/unity` clone에서 진행했다.

| 항목 | 결과·범위 |
|---|---|
| Startup INPUT 명칭 | Manual / CSV·XYZ / UART. 기존 RX/TX 내부 식별자는 유지 |
| 최신 원본의 UART 선택·START | 사용자 직접 확인 PASS. 원본 UI 자동화는 helper 초기화 실패로 수행하지 못함 |
| 원본/인계본 UI 파일 일치 | 문자열 변경 3개 파일 SHA256 동일 |
| Clean Clone Import·Editor Play | 컴파일 오류 0, Mock UI 및 XYZ 재계산 검증 |
| Windows Player | 실제 실행·EventSystem 버튼/슬라이더 입력, G51/Humanoid·RGB Mock·Camera Mock·두 Native DLL 검증 |
| Git 보호 | 원격 전용 11개 유지, 로봇팀 코드 변경 0, 삭제 0, add/commit/push 미실행 |
| 하드웨어 | RX/TX 연결 없음, Hardware TX=0. 실제 UART/Pcam/HDMI/Servo 시험 아님 |

구체적인 증거와 제한은 [Clean Clone 재현성 결과](CLEAN_CLONE_REPRODUCIBILITY.md)를 따른다. 위 결과를 상단의 새 팀원 PC·실물 시험 행에 자동 복사하지 않는다.


## Q. Humanoid / 양측 Mock 추가 인계 확인

개발 검증과 로봇팀 새 PC/실물 검증을 분리한다. 상세 결과는 [Clean Clone 결과](CLEAN_CLONE_REPRODUCIBILITY.md)를 따른다.

- [ ] Humanoid Manual에서 SHOULDER / ARM JOINTS 독립 접기와 R/L 현재값 확인
- [ ] RIGHT/LEFT/BOTH 선택만으로 PWM·FOLLOW·관절값·epoch가 바뀌지 않음
- [ ] RIGHT PWM ON / LEFT PWM OFF → BOTH A에서 RIGHT Accepted / LEFT Denied 및 PARTIAL 표시
- [ ] 양측 Pitch X / Roll Z가 기존 pivot에서 회전하고 하위 팔/ToolMount가 따라감
- [ ] LEFT arm 프리뷰 편집이 RIGHT Requested/Approved/Applied에 영향을 주지 않음
- [ ] BOTH 명시 편집만 양측 적용; G51 기존 5축 경로와 preset 유지
- [ ] Camera Preview, Controls Hide, Presentation 및 1280×720 배치 확인
- [ ] 실제 좌우 COM/보드별 명령 응답/거부/timeout 계약 확정 (**실물 미검증**)
- [ ] 물리 Shoulder 채널·방향·보정·limit 존재 여부 확정 (**현재 펌웨어 5축, 어깨는 VISUAL ONLY**)

위 체크박스는 로봇팀 재검증용으로 비워 둔다. Mock 동작 통과를 UART/양팔 실물 동기화 통과로 바꾸지 않는다.
