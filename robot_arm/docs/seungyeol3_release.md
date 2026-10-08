# 승열3 릴리스 준비 및 검증 기록

작성일: 2026-10-08. **상태: 정상 루트 소스 통합·release_v3 좌우 ARM 빌드·native 회귀·호스트 131개 회귀 완료. 보드 부팅/SD 복사는 미수행.** 아래 결과는 실제 생성 아티팩트와 실행 로그에 근거한다. 이 문서는 새 BOOT 설치나 실제 서보 구동/녹화/재생 시험이 이루어졌다는 보고가 아니다.

## 변경 목적과 경계

승열3는 녹화/재생 메뉴의 표시 상태와 보드 실제 상태를 맞추고 녹화의 시작/끝 경계를 안전하게 정리한다. 작업 기준은 운영 소스의 최신 사본이며 좌우 보드별 로컬 로봇 제어 및 Robot1/LEFT 그리퍼 닫힘 최대 2400 µs 제한을 포함한다.

| 대상 | 승열3 변경/유지 계약 |
|---|---|
| 녹화 시작 | R 요청 후 START_RECORD에서 마지막 승인 목표 정착, 새 HAL 안정 3틱 확인 및 정지 seed 2샘플 기록 후 gate 해제; 준비 이동 제외 |
| 준비 중 추종 | R 직전 async/remote-follow 요청 보존; 기존 ON이면 이어가고 OFF이면 유지, 추가 A 자동 요청 없음 |
| 준비 취소 | START_RECORD 중 사용자 R은 준비 취소; 기존 SD 미덮어쓰기, 실제 상태는 보드 응답으로 확인 |
| 녹화 종료 | R 또는 녹화 한계 도달 후 STOP_RECORD에서 기존 안전 제한으로 tail 기록·정착·검증 |
| 용량 경계 | 현재 vmax/amax의 관절 최대 envelope·그리퍼 전 범위 속도·8틱 여유로 tail 공간 계산; 실제 시작 시 live_max_ms/tail_reserve_ms/total_max_ms 보고 |
| 상태 보고 | `[REC_STATE] schema=1`을 상태 변경과 V 조회에 출력; 기존 `[REC]` 상태 숫자/문구 호환 유지 |
| 모니터 복구 | 호스트 송신/메뉴 표시만으로 NAME/SAVE/재생 종료를 추정하지 않고 보드 응답으로 복구 |
| 기존 wire | R/P/E/V, NAME/LIST/SELECT/DELETE/CONFIRM/CANCEL 프레임 유지 |
| 입력 정지 | ESC+S/X는 UART 부분 입력·CNN 숫자 메뉴 취소; 보드 NAME UI 취소와 분리 |
| 이름 오류 | NAME_STATE/UNSAFE_STORAGE/REPLAY_VALIDATION/NAME_FORMAT/DUPLICATE/LIBRARY_FULL/IO 구분; 실패를 SD 저장 완료로 처리하지 않음 |
| 거부 명령 보호 | pending UI에서 거부된 A는 gripper·elbow·wrist 추적 이력을 초기화하지 않음; 정상 A/S 정책 유지 |
| 시작/종료 정책 | 앱 시작·종료·재연결에 자동 PWM ON·추종 ON·재생·녹화·RAM 폐기 없음 |
| 파일 형식 | 기존 MRP1 24-byte 헤더, 20 ms, float 5개/20 byte 샘플, ABI/CRC 유지 |

손목 매핑·속도, wrist unwrap/reacquire, 입력 필터와 Kalman 시간 정책, tracking/JUMP, gripper 판단/latch/open 유지, 축 가동범위·속도/가속도·FK·HAL 실패 보호는 이번 수정 대상이 아니다. Robot1 2400 µs clamp도 유지한다. 승열3가 손목 떨림이나 모터/기구/전원 문제를 해결했다는 결론은 내리지 않는다. 기록/재생은 명령 기반이며 전류·엔코더 실측 피드백을 추가하지 않는다.

외부 작업 위치는 `D:/Working/robot-motion-harness/experiments/seungyeol3_20261008/`이며 `source/`에서 승인된 수정·교차 리뷰·빌드·회귀를 수행했다. 검증한 소스를 정상 `robot_arm/src/`, `include/`, `config/`와 호스트 `tools/`에 통합했다. 기존 운영 스냅샷과 보드 SD는 변경하지 않았다. Git 공유 대상은 정상 소스·UART 문서·회귀 테스트·현행 XSA이며 사진, 가중치, BOOT 및 실험 산출물은 포함하지 않는다.

## Unity 인계

구현 계약은 [Unity UART 문서](uart_protocol_unity.md)를 따른다. Unity GUI 구현은 요청 범위가 아니다. PC의 `r E`는 호스트 라우팅이며 RIGHT wire에는 E 한 바이트만 전달한다. USB 콘솔은 TRACE 빌드 921600, TRACE 없는 빌드 115200이며 보드 간 UART의 기본 115200과 혼동하지 않는다.

권위 상태 줄:

```text
[REC_STATE] schema=1 side=l|r ui=IDLE|STOP_RECORD|STOP_MENU|NAME|MENU|SAVE|START_RECORD mode=LIVE|RECORDING|ALIGNING|PLAYING|HOLDING pwm=0|1 ram_samples=<n> entries=<n> selected=<id> playing=<id-or-0> delete=<id-or-0> unsaved=0|1
```

기존 숫자 매핑은 IDLE=0, STOP_RECORD=1, STOP_MENU=2, NAME=3, MENU=4, SAVE=5를 유지하고 START_RECORD=6을 뒤에 추가한다. UI 단계와 제어 mode를 분리해서 해석한다. V 조회는 상태 복구용이며 자동 메뉴 취소·새 동작을 만들지 않는다.

R 요청 승인과 실제 준비 완료(`ui=IDLE mode=RECORDING`, seed 기록 완료)를 구분하고, 종료 정착 중 타이머/이름 입력을 성급하게 완료하지 않는다. 준비에는 남은 목표까지의 이동과 안정 확인·seed 기록 시간이 포함되어 고정 지연이 아니다. R 전 추종 요청은 보존하며 준비 완료 후 기존 ON이면 이어가므로 추가 A가 필요 없다. 저장은 SAVED와 REC_STATE, 재생은 PLAY와 REC_STATE로 승인 확인한다. SELECT는 파일 검사와 첫 자세 ALIGN을 거쳐 무한 반복하며 반복 사이에도 ALIGN한다. P로 중지한 뒤 추종은 사용자 A 요청으로 재개한다. NAME에서 S/X를 보냈다는 이유로 이름 입력 UI를 자동 폐기하지 않는다.

## 구현 확정 항목

| 항목 | 확정 값 / 근거 |
|---|---|
| START_RECORD 직전/직후 추종 정책 | 기존 요청 보존·입력 gate 해제; 실제 main gate/추종 이어짐/종료 한계 시 새 입력 차단 native 회귀 통과 |
| 추종 녹화 시작 순서 | LIVE에서 사용자 A 승인 → R → 실제 녹화 시작. 기존 OFF면 계속 OFF; 준비 이동 제외·seed 회귀 통과 |
| tail reserve 샘플 수·시간 | envelope/vmax/amax, gripper 전 범위 속도, 8틱 여유로 계산; 현 검증값 300 샘플 / 6.00 s |
| 일반 기록 한계 / 총 파일 길이 | 일반 기록 1748 샘플 / 34.96 s, 총 상한 2048 샘플 / 40.96 s. 일반 기록 count에는 시작 seed 2샘플 포함 |
| 준비/정착 완료 판정 | 연속 3개 새 HAL 안정 틱, 준비의 정지 seed 2샘플; 좌우 HAL 실패/취소 및 기록 경계 회귀 통과 |
| UART/SD timeout의 GUI 표시 정책 | 향후 실물 측정 대상 — 실측 이전에 동작 취소/정지로 추정하지 않음 |
| unsaved/selected/playing/delete 전이 | status schema·이름/저장/취소·PWM OFF native 회귀와 삭제/재접속을 포함한 호스트 131개 회귀 통과 |

tail reserve를 마련해도 각 샘플의 finite/range/FK/속도/가속도/gripper delta와 tick/HAL 실패 검사를 유지해야 한다. 한계 도달 직전·이후와 사용자가 R 종료를 누른 경우 모두 안전한 마지막 기록 prefix를 다룬다. 정착 실패 기록을 정상 저장물로 게시하지 않는다.

## 검증 결과

실행하지 않은 항목은 통과로 표기하지 않는다. native는 PC의 driver mock과 메모리 FatFS를 사용한 실제 소스 실행이며 실제 보드/SD/서보 시험과 구분한다.

| 검증 | 결과 | 수치·로그·아티팩트 |
|---|---|---|
| 문서의 실제 source/monitor parser 대조 | 초안 작성 시 확인 | cnn_app/event/console, main, motion_library/rr/sd, filter parser, CFG, camera, Python monitor |
| 정상 소스 링크·문서 형식 확인 | 정상 저장소 경로로 확인 | UART 문서 링크는 정상 src/include/config/tools를 가리킴 |
| 문서 송신 예시·상태 예제의 parser 대조 | 통과 | 현재 모니터 parse_command로 23개 호스트/wire 예시, LIST hex, REC_STATE 예제, 숫자 UI 매핑 확인. 직렬 포트 미개방; 전체 회귀와 별개 |
| R 준비 정착 뒤 첫 샘플 / 기록 시작 승인 | 좌우 native 통과 | prepare_hal_settle_and_seed, actual_main_preparation_gate, actual_main_seamless_record_resume |
| START_RECORD 중 R 취소/PWM OFF/HAL 실패 | 좌우 native 통과 | prepare_cancel_and_pwm_off, prepare_hal_failure; 기존 파일 보호 |
| R 종료 tail / 이름 진입 / 파일 샘플 검증 | 좌우 native 통과 | manual_stop_and_name_survives_controls, mrp1_compatibility_and_saved_payload |
| 일반 기록 한계 자동 종료 / 안전 tail | 좌우 native 통과 | automatic_limit_safe_tail, actual_main_limit_blocks_fresh_input; 기록 한계 1748, 최종 유효 clip 1829 샘플 / 36.58 s 저장·재생 |
| REC_STATE 상태/필드·숫자 UI 매핑 | 검사 범위 통과 | status_schema와 호스트 131개 회귀 통과; V 강제 보고와 숫자 UI 호환 확인 |
| NAME 중 S/X 등 control 후 보드 UI 유지 | 좌우 native·호스트 회귀 통과 | manual_stop_and_name_survives_controls; UART ESC/250 ms는 기존 입력 계약 |
| SELECT/ALIGN/반복/P stop 및 오류 HOLD | native·호스트 검사 범위 통과 | 자동 종료 clip 저장·재생, basic_1 3회 반복·stop to LIVE·overrun to HOLD; 호스트 선택 대기/복구 확인 |
| CANCEL 및 NAME/SAVE 실패·RAM/기존 파일 보존 | 좌우 native 통과 | cancel_transitions, names_and_duplicate_preserve_ram, save_failure_atomicity |
| DELETE/CONFIRM·재접속·호스트 표시 복구 | 호스트 회귀 통과 | 실제 COM 재연결·SD 삭제는 수행하지 않음; 메모리/mock 상태 복구 검사 |
| Python 모니터 parser 회귀 | 외부·정상 루트 각각 131 통과, skip 0 | viewer/OBS/좌표 parser 의존성을 포함한 test*monitor*.py 전체 실행, 직렬 포트 미개방 |
| Record/replay 경계 및 실제 main gate 회귀 | 최종 로그 좌우 통과 | 역할별 15 cases, 239988 checks, failures=0, audited_ticks=4362. MSVC x64 C11 /W3 /WX /O2, source_stable=true, 양쪽 build/test exit 0 |
| 기존 RIGHT 적용 명령/PWM/register 스트림 | frozen baseline과 정확히 일치 | full_stop 2020틱, only_elbow_roll 2035틱, basic_1 3200틱; native 경고 0 |
| 기존 손목/필터/tracking/gripper/PWM 경로 보존 | 소스 차이 및 검사 범위 확인 | 펌웨어 차이는 library header/c, main의 경계 훅 3파일. native A2/Agent3/PWM 비교와 하위 안전 검사 통과; A1 재구성/필터 및 LEFT 패킷 전체를 native stream 일치로 검증했다고 주장하지 않음 |
| LEFT firmware 빌드 | ARM compile/link/Bootgen exit 0, warning 0 | split LEFT, final_uart0.xsa, 자동 PWM ON 없음. release_v3/left/BOOT.BIN 4592208 byte; 아래 SHA256 |
| RIGHT firmware 빌드 | ARM compile/link/Bootgen exit 0, warning 0 | split RIGHT, final_uart0.xsa, 자동 PWM ON 없음. release_v3/right/BOOT.BIN 4592208 byte; 아래 SHA256 |
| BOOT 구성·기존 FSBL/PL·SHA loader | 파일/정적 검사 완료 | 좌우 3 partitions, 기존 FSBL/PL 동일 확인, SHA loader disassembly 확인; 실제 부팅 검증은 아님 |
| DDR/BSS 및 버퍼 크기 확인 | ELF/linker 정적 확인 | LEFT BSS 3085264 byte, _end=0x45D3F0; RIGHT BSS 3083632 byte, _end=0x45CD90. 앱 DDR 0x100000..0xA000000 안에 배치, 최대 샘플 수 증가 없음. 런타임 stack/heap 사용량 실측은 아님 |
| 일반 PC 런타임 CMake 빌드 | MSVC 빌드 통과, BUILD_TESTING=OFF | G Kalman 등록, MSVC C11/UTF-8 설정. 구형 One Euro pose/runtime 테스트는 euro 필드 계약이 달라 전체 CTest 빌드 실패; 최신 G용 전환은 후속 과제 |
| XYZ/영상 리뷰 보조 검사 | parser 12개·브라우저 로직 통과 | viewer 22개 중 21개 통과, native geometry 1개는 host GCC 미설치로 실행 실패. 모니터 131개와 별도; 이 실패를 통과로 집계하지 않음 |
| 보드 SD 복사/적용 | 양쪽 미수행 — NOT_PERFORMED | 실제 적용 담당자가 별도 수행 후 날짜·대상·백업·해시 기록 |
| 실제 보드 부팅·물리 구동/녹화/저장/재생 | 양쪽 미수행 — NOT_TESTED | 파일/모의 실행 통과와 별개. 별도 수행 후 좌우 보드 로그·물리 관측 기록 |

release_v3 BOOT 아티팩트(경로는 외부 실험 루트 기준):

| 역할 | 파일 | 크기(byte) | SHA256 |
|---|---|---|---|
| RIGHT | `release_v3/right/BOOT.BIN` | 4592208 | `be2ab1b30da52d2789654b295416ea8e6a0f31a80f293bb4c569bfd4e4ca35c0` |
| LEFT | `release_v3/left/BOOT.BIN` | 4592208 | `abb86dabd8364eb11fe4b3f822aa4cb072ef4a831c5670993203b934256360eb` |

근거 파일은 `release_v3/release_report.json`, 역할별 `release_v3/left|right/verification.json`, `build_v3/native/validation.json` 및 경계 시험의 `summary.json`·역할별 `test.log`다. 최종 외부 회귀는 `tests/runs/20261008_100302_333/summary.json`, 정상 루트 회귀는 `tests/record_replay/boundaries/runs/20261008_100448_715/summary.json`이며 양쪽 모두 역할별 15 cases/239988 checks/4362 audited ticks 통과다. 새 case `actual_main_rejected_async_preserves_state`가 pending UI의 거부 A에서 reset 호출·보드 활성화·추적 이력 변경이 없는지 실제 main dispatcher로 검사한다. 시험 내 여러 assert 수는 독립 시나리오 개수나 실물 시험 횟수가 아니다.

native 스트림 비교는 실제 A2·20 ms record/replay·Agent3·HAL/PWM driver mock에 기록된 사람 목표를 넣은 범위다. A1 재구성, main/UART 패킷 전체 dispatch, 실제 모터/전류/엔코더, 실제 SD 지연은 이 일치 검사의 대상이 아니다. `basic_1` 3200틱 일치는 기존 파일 재생 결과 보존의 증거이고 로그 복원 오차나 실물 손목 떨림 해결의 증거가 아니다. torque/속도 변경은 이번 릴리스에 넣지 않았으며 관련 의견 요청은 별도 검토로 남긴다.

기존 RAM 계산: MotionSample 20 byte, 2048 샘플 버퍼 하나 40,960 byte, record/replay 두 버퍼 합 81,920 byte, SD staging 40,960 byte. 이는 헤더/코드상의 버퍼 계산이며 최종 ELF BSS·DDR 여유 실측 수치를 대신하지 않는다.

## 통합 및 운영 인계

검증한 작업 사본과 문서를 정상 `robot_arm/src/`, `include/`, `config/`, `tools/`, `docs/`로 통합했다. 문서 링크와 최신 설명은 정상 루트 소스를 가리킨다. 과거 snapshots·captures·외부 harness는 시험 근거로 보존하되 Git에 대형 아티팩트나 런타임 의존성으로 포함하지 않는다.

정상 저장소 `setup_vitis.ps1`에 split LEFT/RIGHT 빌드 flags와 `final_uart0.xsa`를 반영했다. PowerShell 구문 검사를 통과했으며 이번 ARM 릴리스는 검증된 기존 BSP/FSBL로 빌드했다. 새 workspace의 XSCT 재생성 자체는 실행하지 않았다. split 역할은 기본 PWM OFF다. 구형 OneEuro-A/LEFT sender-only 설명은 이력으로 표시하고 최신 계약으로 연결했다. canonical 소스는 정상 루트다.

새 호스트 parser만 배포해서 구형 BOOT에 REC_STATE/녹화 경계가 생기지 않는다. 실제 설치 후 포트별 V와 t를 조회하여 schema=1·side·stop_escape·CFG capability를 확인하고, 좌우 중 한쪽만 적용되면 UI에서 버전/지원 차이를 표시한다. 포트 연결·앱 종료는 자동 E/A/R/P/CANCEL을 보내지 않는다.

현재 운영 스냅샷과 SD를 보존한 상태에서 빌드/검증 결과를 비교한다. 보드 적용·실물 시험은 별도 실행 기록으로 남기며, 실제 수행 전에는 이 문서의 미수행 표시를 완료로 바꾸지 않는다. SD 저장 중 UART 지연과 PWM OFF 후 실물 처짐은 명령 로그만으로 해결/검증하지 못한다.

소스 링크: [main_integration.c](../src/integration/main_integration.c), [motion_library.c](../src/record_replay/motion_library.c), [motion_record_replay.c](../src/record_replay/motion_record_replay.c), [MotionSample/모드](../include/record_replay/motion_record_replay.h), [motion_sd.c](../src/record_replay/motion_sd.c), [UART guard](../src/integration/cnn_app_event.c), [monitor parser](../tools/stereo_vision/monitor_stereo_uart.py), [좌우/2400 µs 설정](../config/dual_arm_config.h).
