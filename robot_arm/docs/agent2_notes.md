# Agent2 통합 기록

> 현행 수정 지침은 [../AGENTS.md](../AGENTS.md)이다. 2026-09-24 사용자 결정으로
> 6축 A2 보존 지침은 폐기되었고, 아래의 보존/legacy 연결 설명은 작성 당시 이력이다.
> 정리 결과: [agent2_cleanup_20260924.md](agent2_cleanup_20260924.md).

Agent2(robot_calibration/motion 담당) 쪽에서 Claude Code/Codex가 생성한 설계 로그·핸드오프·검증 리포트를
한 파일로 모았다. 원래 여러 개의 `agent2_*`/`agent3_forearm_handoff_prompt`/날짜별 인계 문서로
흩어져 있던 것을 2026-09-24에 통합했다. 각 절은 원문을 그대로 보존했고(작성 시점 기준 코드/로그를
가리키므로 이후 코드와 다를 수 있음), 목차 순서는 작성 일자순이다.

## 목차

1. [Agent2 설계 결정 로그 (2026-09-16 ~ 09-22, 누적 기록)](#1-agent2-설계-결정-로그-2026-09-16--09-22-누적-기록)
2. [1차 UART 로그 분석과 수정 순서 인계 (2026-09-22, Codex)](#2-1차-uart-로그-분석과-수정-순서-인계-2026-09-22-codex)
3. [Agent1 BodyFrame 수정 검토 프롬프트 — 레거시 6축 (2026-09-22)](#3-agent1-bodyframe-수정-검토-프롬프트--레거시-6축-2026-09-22)
4. [Agent2 5축(forearm) 전환 작업 지시 프롬프트 (2026-09-22)](#4-agent2-5축forearm-전환-작업-지시-프롬프트-2026-09-22)
5. [Agent2 5축(forearm) 독립 검증 리포트 (2026-09-22)](#5-agent2-5축forearm-독립-검증-리포트-2026-09-22)
6. [Agent3 5축 인터페이스 변경 핸드오프 (2026-09-22)](#6-agent3-5축-인터페이스-변경-핸드오프-2026-09-22)
7. [2차 UART 로그·실물 사진 기반 코드 리뷰 (2026-09-23, Codex)](#7-2차-uart-로그실물-사진-기반-코드-리뷰-2026-09-23-codex)

---

## 1. Agent2 설계 결정 로그 (2026-09-16 ~ 09-22, 누적 기록)

원본: `agent2_design_log.md`. Claude Code와 Codex가 번갈아 작업하며 남긴 설계 결정 공유 로그.
날짜별 항목이 이미 시간순으로 쌓여 있는 문서라 이 절 안에서는 원문 순서를 그대로 유지한다.


`robot_calibration`(Agent 2) 작업을 Claude Code와 Codex가 나눠서 진행하면서 남기는 공유 결정 로그입니다.
서로 모르는 상태에서 판단이 필요한 설계 결정(모호한 스펙, 예외 케이스, 하드웨어에 대한 가정 등)을
내릴 때는 조용히 혼자 결정하지 말고 이 파일에 기록합니다. 포맷:

```
### <날짜> - <짧은 제목>
- 작성자: Claude | Codex
- 쟁점: 무엇이 애매했는가
- 결정: 무엇으로 정했는가
- 근거: 왜 그렇게 정했는가
- 사용자 확인 필요: yes/no
```

최신 항목이 맨 아래에 추가됩니다.

---

### 2026-09-16 - FK 중립 자세(neutral-pose) 관례 (safety_check 작업에서 이어짐)
- 작성자: Codex
- 쟁점: 조립된 로봇팔에서 서보 각도 90도가 물리적으로 무엇을 의미하는가?
- 결정: 90도는 "이 링크가 이전 링크와 일직선으로 이어짐"을 의미한다고 가정했습니다.
  shoulder_deg는 +X축 기준 절대각, elbow_deg/wrist_pitch_deg는 이전 링크 기준 상대적인 굽힘 각도
  `(servo_angle - 90)`으로 처리합니다.
- 근거: `robot_calibration_config.c`의 모든 `zero_offset_deg` 값이 90인데, 이는 문서화되어 있진
  않지만 "중립"을 나타내는 자연스러운 신호입니다.
- 사용자 확인 필요: yes — 아직 아무도 실제 조립된 로봇팔에서 이 가정을 검증하지 않았습니다.

### 2026-09-16 - 자기 충돌(self-collision) 여유 임계값
- 작성자: Codex
- 쟁점: (CAD나 매뉴얼이 없는 상태에서) 자기 충돌 기준으로 "너무 가깝다"는 어느 정도인가?
- 결정: `BASE_CLEARANCE_CM = 2.0`, `LINK_CLEARANCE_CM = 1.0`, `MIN_JOINT_INTERIOR_DEG = 25`.
- 근거: 보수적인 임시값이며, `safety_check.c`에도 "하드웨어 실측 전까지의 임시값"이라고
  명시해뒀습니다.
- 사용자 확인 필요: yes — 실제 링크 두께/모터 하우징 치수를 측정해서 교체해야 합니다.

### 2026-09-16 - robot_calibration_apply()의 위험 명령 처리 방식
- 작성자: Claude
- 쟁점: `safety_check_apply()`가 문제를 보고하면 `robot_calibration_apply()`는 해당 명령을
  어떻게 처리해야 하는가?
- 결정: 0을 반환하고 `output->valid = 0`으로 설정합니다. 명령을 "안전한" 자세로 수정/clamp하지
  않고 그냥 무효로 표시만 합니다. 즉 호출자는 이 명령 대신 마지막으로 알려진 안전한 `JointCommand`를
  계속 유지해야 한다는 암묵적 계약입니다.
- 근거: "가장 가까운 안전 자세"를 계산하는 복구 알고리즘을 만드는 건 이번 작업 범위를 벗어나므로,
  대신 기본값으로 fail-safe(위험 쪽으로 움직이지 않음)를 택했습니다.
- 사용자 확인 필요: yes.

### 2026-09-16 - motion_limits의 관절 배열 순서 규칙
- 작성자: Claude
- 쟁점: rate-limiter/동기화 모듈은 6개 관절을 일반적인 float 배열로 다뤄야 하는데,
  `JointCommand`는 이름이 붙은 필드 구조체입니다. 순서/개수를 어떻게 맞출 것인가?
- 결정: `MOTION_LIMITS_JOINT_COUNT = 6`, 인덱스 순서는
  `[0]=base, [1]=shoulder, [2]=elbow, [3]=wrist_pitch, [4]=wrist_roll, [5]=gripper`로
  `JointCommand`의 필드 선언 순서와 동일하게 맞췄습니다. `max_delta_per_tick` 값은
  `robot_calibration_config.c`의 관절별 `max_delta_deg`를 그대로 사용합니다 (단위는 관절마다
  다름 — 5개는 degree, gripper는 0..1 정규화 값).
- 근거: 서로 다른 두 가지 순서가 따로 떠도는 것보다 하나로 고정된 규칙을 쓰는 게 낫고, 기존
  구조체와 맞춰두면 `JointCommand`와의 상호 변환이 단순한 인덱스 순회로 끝납니다.
- 사용자 확인 필요: no (내부 구현 세부사항이며, 공개된 `HumanJointTarget`/`JointCommand`
  계약에는 영향 없음).

### 2026-09-16 - Motion Smoothing 방식: smoothstep + 시간 늘리기 vs. 사다리꼴 프로파일
- 작성자: Claude
- 쟁점: 교과서적인 속도/가속도 제한 궤적은 사다리꼴 속도 프로파일(가속-등속-감속)을 사용해서
  `motion_limits_synchronize()`가 계산한 하드 `max_delta_per_tick` 한계를 정확히 지킵니다.
  3차 smoothstep(ease-in/ease-out)은 구현은 훨씬 간단하지만, 순간 최대 속도가 평균 속도의
  1.5배라서 — `motion_limits_synchronize()`가 계산한 틱 수 위에 그대로 적용하면 동작 중간
  지점에서 틱당 속도 한계를 넘어버립니다.
- 결정: smoothstep을 쓰되, 적용하기 전에 공통 소요 시간을 1.5배(`ceil(total_ticks * 1.5)`)
  늘려서, 곡선의 최대 속도가 다시 `max_delta_per_tick` 이하로 들어오도록 했습니다. 이는
  근사치이며 진짜 가속도 제한 프로파일은 아닙니다.
- 근거: 이번 작업 범위 안에서 구현을 작고 테스트하기 쉽게 유지하면서도, 완전한 사다리꼴 프로파일
  상태 머신을 만들지 않고도 동작 시작/끝의 속도 불연속(급출발/급정지의 실질적인 문제 원인)은
  없앨 수 있습니다.
- 사용자 확인 필요: yes — 실제 서보/기어 특성상 (단순히 더 매끄러운 접근 곡선이 아니라) 진짜
  가속도 제한이 중요하다면 사다리꼴 프로파일로 바꿔야 할 수 있습니다. 당장 막을 사안이라기보다는
  후속 작업 후보로 표시해둡니다.

<!-- Codex: motion_limits.c를 구현하면서 위에 이미 다뤄지지 않은 결정 사항이 있다면 이 줄
     아래에 같은 포맷으로 추가하세요. -->

### 2026-09-16 - 잘못된 motion-limit 입력값과 틱 수 오버플로 처리
- 작성자: Codex
- 쟁점: 0/음수인 속도 제한값, 비유한(non-finite) 값, 그리고 `int`로 표현 가능한 범위를 넘는
  이동 요청은 어떻게 보고해야 하는가?
- 결정: `motion_limits_ticks_to_target()`은 이런 유효하지 않거나 표현 불가능한 이동 요청에
  대해 `-1`을 반환하고, 이미 목표에 도착한 경우에는 제한값이 0이거나 음수여도 정상적으로 `0`을
  반환합니다. `motion_limits_synchronize()`는 어느 관절이든 유효하지 않으면 `0`을 반환하고
  출력은 변경하지 않습니다. 상태 반환 채널이 없는 `motion_limits_step_toward()`는 잘못된
  입력이거나 제한값이 0 이하일 때 안전하게 `current`를 그대로 반환합니다.
- 근거: 유효한 틱 수는 항상 0 이상이므로 `-1`은 명확한 오류 sentinel이 됩니다. 정지해 있는
  관절은 사용 가능한 이동 속도가 필요 없습니다. 비유한 값이거나 범위를 초과하는 계획을 거부하는
  것은 설정된 한계를 위반하는 속도나 소요 시간이 만들어지는 것을 막아줍니다.
- Claude 확인 필요: no.

### 2026-09-17 - 코드 주석은 한글로 작성 (팀 컨벤션)
- 작성자: Claude (사용자 지시)
- 쟁점: 지금까지 Claude와 Codex가 작성한 주석이 대부분 영어였음.
- 결정: 앞으로 이 저장소의 모든 새 코드 주석은 한글로 작성한다. 기존 영어 주석은 해당 파일을
  다른 이유로 수정할 때 같이 한글로 바꾼다.
- 근거: 사용자의 명시적 지시.
- Codex 확인 필요: no — Codex도 다음 라운드부터 이 컨벤션을 따라주세요.

### 2026-09-17 - gripper를 속도제한/동기화/스무딩 대상에서 완전히 제외
- 작성자: Claude (사용자 지시)
- 쟁점: gripper_norm은 Agent 1이 이미 0/1로 양자화해서 보내고, Agent 3이 세부 동작을 처리하기로
  합의되어 있음. 그런데 지금까지는 gripper도 다른 5개 관절과 똑같이 6개짜리 배열의 인덱스
  5번으로 취급되어 calibration(scale/offset/clamp), 속도제한, 다관절 동기화, smoothing을
  전부 거치고 있었음 — Agent 1→2→3 구간에서 값이 바뀌면 안 된다는 합의와 어긋남.
- 결정: gripper를 관절 배열에서 완전히 빼고 단순 pass-through로 바꿈.
  - `RobotCalibrationConfig`에서 `.gripper` 필드(JointCalibration) 삭제, `motion_control_apply_limits()`의
    gripper clamp 라인 삭제 (`motion_control_map_target()`의 단순 복사는 유지 — 값이 안 바뀌므로 문제 없음).
  - `ROBOT_MOTION_JOINT_COUNT`(robot_calibration.h)와 `MOTION_LIMITS_JOINT_COUNT`(motion_limits.h)를
    6에서 5로 변경 (base/shoulder/elbow/wrist_pitch/wrist_roll만 포함).
  - `RobotMotionState`에 `float gripper` 필드를 추가해서 `robot_calibration_set_target()`이 목표값을
    그대로 저장하고, `robot_calibration_step()`이 램프 없이 매 틱 그대로 출력.
- 근거: 사용자의 명시적 지시 — "agent1 -> agent2 -> agent3 과정에서 정보가 변하면 안 됨. 이걸
  건드리는 코드가 있다면 다 지워줘."
- Codex 확인 필요: yes — `MOTION_LIMITS_JOINT_COUNT`가 6에서 5로 바뀌었습니다. `motion_limits.c`
  자체는 이 매크로만 참조해서 로직 변경 없이 그대로 컴파일되는 걸 확인했지만
  (`tests/robot_calibration/test_motion_limits.c`도 5개짜리 배열로 이미 갱신/재검증 완료),
  Codex가 다음 라운드에서 이 상수나 관절 배열 관례를 다시 참조할 일이 있다면 6이 아니라 5,
  그리고 gripper가 더 이상 이 배열에 포함되지 않는다는 점을 참고해주세요.

### 2026-09-16 - 동기화된 델타 값은 부호 없는 속도 크기(magnitude)
- 작성자: Codex
- 쟁점: `scaled_delta_per_tick`이 각 관절의 이동 방향 부호를 가져야 하는가, 아니면 입력값인
  `max_delta_per_tick`처럼 틱당 한계값(크기)으로 남아야 하는가?
- 결정: 동기화된 각 델타는 부호 없는 크기(magnitude)입니다. 방향은 계속 `current`와 `target`
  에서 나오며, `motion_limits_step_toward()`에 넘길 때도 마찬가지입니다.
- 근거: 이 계획은 설정된 최대 속도(크기 값)를 스케일링하는 것이고, 부호를 분리해두면 같은
  stepping 함수로 양방향 이동을 모두 처리할 수 있습니다. float 변환 과정에서 평균 속도가
  아래로 반올림되면, `scaled_delta_per_tick * total_ticks`가 원래 설정된 최대값을 넘지 않으면서도
  요청된 거리를 여전히 커버하도록 표현 가능한 다음 float 값으로 한 단계 올려줍니다.
- Claude 확인 필요: no.

### 2026-09-17 - (해결됨) `motion_limits_synchronize()`의 미사용 `scaled_delta_per_tick` 제거
- 작성자: Claude (사용자 지시)
- 쟁점: 위 항목(부호 없는 속도 크기)에서 다룬 `scaled_delta_per_tick`이, 실제로는
  `robot_calibration_set_target()`에서 계산만 되고 어디서도 읽히지 않고 있었음
  (2026-09-16 "Motion smoothing approach" 항목 참고 -- motion_smoothing의 공통 진행률
  eased(t) 방식이 다관절 동기화를 대신 해결하고 있었기 때문). 사용자가 코드 리뷰에서
  직접 이 중복/낭비를 지적했고("motion limit이랑 motion smoothing이 역할이 좀 겹친거
  아닌가?"), 정리하기로 함.
- 결정: `motion_limits_synchronize()`의 시그니처를 `MotionLimitsPlan *out` →
  `int *total_ticks_out`으로 단순화하고, `MotionLimitsPlan` 타입과 관절별 속도 스케일링
  계산(반올림 보정용 `nextafterf` 포함)을 전부 제거. 이제 이 함수는 "가장 느린 관절
  기준 공통 틱 수"만 계산한다. `motion_limits.h`, `motion_limits.c`,
  `robot_calibration.c`, `tests/robot_calibration/test_motion_limits.c` 전부 갱신,
  4개 테스트 전체 재확인 완료.
- 근거: 안 쓰는 계산을 남겨두는 것보다, 실제로 쓰이는 책임(동기화 = 가장 느린 관절
  기준 공통 시간 계산)만 명확하게 API로 남기는 게 나음.
- Codex 확인 필요: yes — `motion_limits_synchronize()`를 호출하거나 `MotionLimitsPlan`을
  참조하는 코드가 있다면 새 시그니처(`int *total_ticks_out`)에 맞춰 갱신 필요합니다.

### 2026-09-17 - (해결됨, 4번 재작업) `motion_limits_synchronize()`를 아예 제거하고 관절 개수 상수 중복 자체를 없앰
- 작성자: Claude (사용자 지시)
- 쟁점: 위 항목에서 `MotionLimitsPlan`은 없앴지만 `motion_limits_synchronize()` 함수 자체는
  남겨뒀는데, 이 함수가 배열(`float[MOTION_LIMITS_JOINT_COUNT]`)을 받다 보니 관절 개수 상수가
  `motion_limits.h`(`MOTION_LIMITS_JOINT_COUNT`)와 `robot_calibration.h`
  (`ROBOT_MOTION_JOINT_COUNT`)에 여전히 따로 정의돼 있어야 했음 (코드 리뷰 4번 항목).
  `common/robot_types.h`에 넣는 건 Agent1/2/3 공유 계약을 Agent2 내부 구현 디테일로
  오염시키는 것이라 부적절하다고 판단. 대안으로 `count`를 런타임 파라미터로 바꾸는 것도
  검토했으나, 임베디드 환경에서 고정 크기 배열의 컴파일타임 안전성을 포기하는 것이라
  사용자가 반대함.
- 결정: `motion_limits_synchronize()`와 `MOTION_LIMITS_JOINT_COUNT` 매크로를 `motion_limits.h/.c`
  에서 완전히 삭제. `motion_limits`는 이제 관절 개수를 전혀 모르는 순수 스칼라 유틸
  (`motion_limits_step_toward`, `motion_limits_ticks_to_target`)만 남김. "여러 관절 중
  가장 느린 것 찾기" 로직은 원래 이 개수를 아는 쪽인 `robot_calibration.c`의
  `robot_calibration_set_target()` 안에 5줄짜리 for 루프로 인라인. 이제 관절 개수 상수는
  저장소 전체에서 `ROBOT_MOTION_JOINT_COUNT`(`robot_calibration.h`) 단 하나뿐.
  `test_motion_limits.c`에서 동기화 관련 테스트 삭제(스칼라 함수 테스트만 남음),
  `test_robot_calibration.c`에 다관절(shoulder 10틱 vs elbow 2틱) 동기화 페이싱을
  검증하는 테스트 추가. 4개 테스트 전체 재확인 완료.
- 근거: 컴파일타임 고정 배열 유지(임베디드 안전성) + 상수 중복 완전 제거를 동시에
  만족하는 유일한 방법은,애초에 배열/개수를 다루는 함수를 개수를 아는 호출자 쪽으로
  옮기는 것. Codex의 핵심 기여(`ticks_to_target`의 NaN/오버플로 등 예외 처리 로직)는
  그대로 남아있고, 없앤 건 그 위에 있던 5줄짜리 조합(reduction) 로직뿐임.
- Codex 확인 필요: yes — `motion_limits_synchronize()`가 완전히 삭제됐습니다. 이 함수나
  `MOTION_LIMITS_JOINT_COUNT`를 참조하는 코드가 있다면, 대신 `motion_limits_ticks_to_target()`을
  직접 필요한 만큼 반복 호출하는 방식으로 바꿔주세요.

### 2026-09-17 - (보류로 결정, 수정 안 함) 코드 리뷰 2·3번 항목 -- `set_target()`/`clamp_value()` 방어적 검증 누락
- 작성자: Claude / 사용자
- 쟁점: 코드 리뷰에서 두 가지가 나왔음 — (2) `robot_calibration_set_target()`이 `target`의
  `isfinite`/`valid` 여부를 검증하지 않음. (3) `motion_control.c`의 `clamp_value()`가 NaN을
  걸러내지 못함(`<`/`>` 비교가 NaN에 대해 항상 거짓이라 그대로 통과). 사용자가 "파이프라인
  제대로 통과하면 문제 없지 않냐"고 반문.
- 결정: 수정하지 않는다. 대신 **호출 순서 규칙(call-order contract)** 으로 다룬다: 이 저장소의
  코딩 원칙("발생할 수 없는 상황은 검증하지 않는다, 경계에서만 검증한다")에 따라, 실제 호출
  그래프를 확인해본 결과 두 함수 모두 프로덕션 코드에서 호출되는 경로가 정확히 하나뿐이고
  (`motion_control_apply_limits()`는 `robot_calibration_apply()` 안에서만, 그것도
  `motion_control_validate_target()`이 먼저 NaN을 거른 뒤에만 호출됨; `robot_calibration_set_target()`도
  `robot_calibration_apply()`가 성공(1)을 반환했을 때만 호출하는 게 설계 의도), 그 경로 안에서는
  이미 검증이 끝난 값만 들어오므로 실제 "경계"가 아님.
- 근거: `robot_calibration_apply()`를 우회하는 두 번째 호출 경로가 없는 한, 이 두 함수는
  진짜 시스템 경계가 아니라 같은 모듈 내부의 정해진 순서일 뿐이라 방어 코드가 불필요함.
- 재검토 조건: 나중에 `robot_calibration_apply()`를 거치지 않고 `set_target()`이나
  `apply_limits()`를 직접 호출하는 새 경로(예: 수동 캘리브레이션 모드, 비상정지 후 강제
  포지션 이동 등)가 생기면 그때 이 판단을 다시 봐야 한다.
- Codex 확인 필요: no — Codex도 이 두 함수를 `robot_calibration_apply()` 밖에서 직접 호출하는
  코드를 추가하지 않도록 유의.

### 2026-09-17 - Agent1 wrap 각도(-180~180) shortest-angle unwrap 처리 추가
- 작성자: Claude (Agent 1 프롬프트 전달받아 사용자와 논의 후 결정)
- 쟁점: Agent1이 `base_deg`/`wrist_pitch_deg`/`wrist_roll_deg`를 -180~180으로 wrap해서 출력한다
  (예: 179도 -> -179도는 실제로는 +2도 정도의 움직임). 그런데 `motion_control_map_target()`은
  매 프레임 `input`의 각도를 이전 프레임과 비교 없이 그대로 선형 매핑하고 있어서, wrap 경계를
  넘는 프레임이 오면 실제로는 거의 안 움직였는데도 로봇 관절이 반대쪽 리밋까지 튀려는 명령이
  나갈 수 있었음(직접 계산으로 재현 확인). `shoulder_deg`(atan2 기반 elevation, 대략 -90~90)와
  `elbow_deg`(acos 기반 내각, 0~180)는 Agent1 스펙상 애초에 wrap되지 않으므로 대상에서 제외.
  상태를 어디 둘지 세 가지를 검토: (a) `robot_calibration_config`에 추가, (b) `RobotMotionState`에
  필드 추가 + `robot_calibration_apply()` 시그니처에 상태 인자 추가, (c) 전용 신규 구조체.
- 결정: (c) 전용 신규 구조체 `HumanAngleUnwrapState`(`motion_control.h`)를 만들고,
  `motion_control_unwrap_target()`이 이 상태를 받아 `HumanJointTarget`의 세 각도를 in-place로
  풀어쓰도록 함. `robot_calibration_apply()`/`motion_control_map_target()` 시그니처는 전혀 건드리지
  않음 — 호출자(추후 main.c)가 `robot_calibration_apply()`를 부르기 "전"에 한 번 먼저
  `motion_control_unwrap_target()`을 호출하는 새 파이프라인 단계로 추가됨. 호출 순서 계약:
  `motion_control_validate_target()`으로 이미 유효성 확인된 target만 넘겨야 함(그렇지 않으면 NaN이
  unwrap 상태에 누적될 수 있음) — 2026-09-17 "보류로 결정" 항목과 같은 호출 순서 계약 패턴.
  검증: `test_robot_calibration.c`에 wrap 경계(179->-179), 반복 프레임(같은 물리 각도 재수신 시
  델타 0), 경계와 무관한 정상 이동 케이스를 각각 확인하는 테스트 추가. 4개 테스트 전체 재확인 완료.
- 근거: (a)는 `robot_calibration_config`가 `extern const`로 선언된 하드웨어 고정 설정값인데
  unwrap 추적값은 매 프레임 바뀌는 런타임 값이라 의미상 안 맞고 const를 깨야 함. (b)는 아직
  main.c가 없어 리스크는 작았지만, "apply()=순수 변환, set_target/step=상태 보유"로 지켜온 경계를
  깨뜨림. 사용자가 최종적으로 "새 구조체를 만드는 게 명확하다"고 판단해 (c)로 결정.
- 사용자 확인 필요: no (이미 사용자와 직접 논의해서 결정).
- Codex 확인 필요: yes — `motion_control.h`에 `HumanAngleUnwrapState` 타입과
  `motion_control_unwrap_state_init()`/`motion_control_unwrap_target()` 함수가 새로 생겼습니다.
  main.c 작성 시 `robot_calibration_apply()` 호출 직전에 이 unwrap 단계를 넣어야 합니다.

### 2026-09-18 - Codex 읽기 전용 확인: 120/154/180/229줄 4개 항목
- 작성자: Codex (`codex exec --sandbox read-only`로 실행, Claude가 결과 재검증)
- 쟁점: 위 4개 "Codex 확인 필요: yes" 항목이 실제 코드/테스트와 여전히 일치하는지 확인 필요.
- 결정(확인 결과): 120·154줄은 180줄의 후속 결정(동기화 함수 완전 삭제)으로 대체된 상태로
  코드와 일치. 229줄(unwrap)도 API/호출 순서 모두 일치. `motion_limits.c`/`safety_check.c`에는
  삭제된 `MOTION_LIMITS_JOINT_COUNT`/`motion_limits_synchronize()` 참조나 gripper 포함 6관절
  가정이 남아있지 않음. 다만 부수적으로 두 가지 사소한 불일치를 발견해 Claude가 수정함:
  (1) `robot_calibration.h`가 이미 삭제된 `motion_limits.h`의 `MOTION_LIMITS_JOINT_COUNT`를
  참조하는 stale 주석 2곳(17-20줄, 37줄) → 현재 설계에 맞게 수정.
  (2) `test_robot_calibration.c`의 wrap-unwrap 테스트가 `HumanJointTarget.valid`/`gripper_norm`을
  초기화하지 않고 `motion_control_validate_target()` 없이 바로 unwrap을 호출 → 구조체를
  designated initializer로 완전히 초기화하고 첫 unwrap 호출 전에 validate_target 통과를
  단언하도록 수정, "검증된 target만 unwrap에 전달" 계약을 테스트가 실제로 보여주게 함.
  두 수정 모두 `-Wall -Wextra -Wpedantic` 재빌드 + 4개 테스트 전체 재통과 확인.
- 근거: 사용자가 통합(2026-09-19) 전 "Codex 확인 필요" 플래그가 실제로 해소됐는지 점검을
  요청. Codex 자체 보고를 그대로 신뢰하지 않고 Claude가 두 발견 사항을 직접 파일에서
  재확인한 뒤에만 로그에 반영함.
- 사용자 확인 필요: no.
- Codex 확인 필요: no — 4개 항목 모두 해소.

### 2026-09-19 - 통합 glue(main + wrapper) 추가와 호스트 스모크 결과
- 작성자: Claude (테스트는 Codex가 작성하고 Claude가 재검증, 사용자 승인 하에 진행)
- 쟁점: XSA/xparameters 확정 전에, 원본 소스(Agent1/2/3, uart_pose)를 수정하지 않고 Agent 간
  연결이 매끄러운지 확인.
- 결정: 신규 파일만 추가했다. `include/integration/{agent_pipeline,input_pose,platform}.h`,
  `src/integration/agent_pipeline.c`, `src/integration/main_integration.c`(통합 진입점),
  `tests/integration/test_integration_smoke.c`. `src/main.c`(Agent3의 HAL 데모)와 CMakeLists.txt는
  건드리지 않는다. platform/input_pose는 선언만 두고 구현은 Vitis workspace 확정 후에 붙인다(호스트 테스트는
  test double 제공). 프레임 경로(가변 주기)와 20ms 틱 경로를 분리하고, Agent1 다음 단계 진행은
  반환값이 아니라 `valid`로 판단한다. 부팅은 홈 부트스트랩 → apply → enable 순서다. 직전과 같은
  명령(HOLD 프레임)은 set_target을 생략한다.
- 결과: 호스트 스모크 S1~S7 통과(UART→Agent1→2→3, 틱당 속도제한, 안전검사 거부 시 유지,
  HOLD/dropout 복구, unwrap 경로). 기존 Agent2 테스트 4개도 통과. 팀 클론으로 이식한 뒤에는 새
  servo_hal의 mock 쓰기 로그로 부팅(shadow 6개 → UPDATE → ENABLE)과 매 틱(shadow 6개 → UPDATE)
  순서도 검증한다.
- 발견 1(중요): 임시 캘리브레이션(모든 관절 사람각+90)에서 Agent1의 측면 자세는 FLOOR로 전부
  거부되고(shoulder 78.6도 -> 168.6도 -> 160도 clamp), 정면 자세도 elbow가 170도 한계에 포화된다.
  Agent1 각도 규약, `robot_calibration_config.c`의 offset/direction, `safety_check.c`의 FK 중립
  가정이 서로 맞춰지지 않은 상태다. 실측 offset이 필요하다.
- 발견 2: Agent1의 HOLD 경계가 float 누적 오차(0.05f x 7 = 0.350000024 > 0.35f)로 350ms 프레임이
  한 프레임 일찍 무효가 된다. 영향은 미미하고 테스트는 350/400ms를 모두 허용한다.
- 발견 3(팀 원본 문제, 통합과 무관): dev/robot 기준으로 `test_pose_mapping.c`는
  `finite_target` 중복 정의(41, 226줄)로, `test_output_control.c`는 삭제된 record/mode 헤더
  (`motion_record.h` 등) 참조로 빌드가 안 된다.
- 사용자 확인 필요: yes — 홈 자세 실측값(RTL reset 값과 일치), 관절별 캘리브레이션 실측 offset.
- Codex 확인 필요: no.

### 2026-09-19 - 폴더 구조 정리: 팀 레포 하나로 통합
- 작성자: Claude (사용자 결정)
- 쟁점: 로컬에 폴더가 두 개 있었다. `robot-arm-2d-control`(옛 단독 레포, 원격 lsy49055089)과
  `zynq-cnn-motion-robot-soc`(현재 팀 레포, 원격 realisshoon)는 서로 다른 레포이고, 팀이 옛 레포를
  `robot_arm/`으로 옮겨 왔다. 통합 산출물은 옛 폴더에만 있었다.
- 결정: 팀 클론 하나만 남기고 통합 산출물을 그 클론의 `feat/robot/robot_calibration` 브랜치에 이식한다
  (새 브랜치를 만들지 않는다). PR #33이 이 브랜치를 squash로 병합했으므로 먼저 `origin/dev/robot`을
  병합해 다음 PR의 기준점을 맞춘다. 옛 폴더는 백업한 뒤 정리한다.
- 반영 사항: Agent3의 HAL/Driver 병합(#30/#31)에 맞춰 `servo_hw`를 `servo_hal`로 바꿨다. 홈 자리표시자의
  gripper는 0.5(=1500us)로 두어 `servo_hal_startup()`의 안전 자세(6채널 1500us)와 맞췄다. 통합 진입점은
  `src/main.c`를 교체하지 않고 `src/integration/main_integration.c`에 두었다. 팀 CMake의
  `robot_arm_2d_control` 실행파일이 `src/main.c`(HAL 데모)를 빌드하므로, 교체하려면 CMake 수정이 필요하다.
- 주의: mock 서보 드라이버의 쓰기 로그는 128건이 차면 쓰기가 실패한다. 부팅 8건, 틱당 7건이라 테스트는
  매 틱 로그를 검증하고 비운다.
- 사용자 확인 필요: no (사용자와 논의해서 결정).
- Codex 확인 필요: no.

### 2026-09-22 - 실제 수평축과 차렷 idle 기준 Agent2 보정
- 작성자: Codex (사용자가 축/방향/범위를 확인하고 Agent2 수정 승인).
- 범위: 운영 코드 변경은 `include/robot_calibration`, `src/robot_calibration` 내부.
  관련 A2/통합 테스트와 이 기록을 갱신했다. A1, A3, UART, 통합 운영 코드는 변경하지 않았다.
- 확정한 물리 기준:
  - 오른팔 해부학적 방향을 따른다. 모든 회전 서보 90도/1500us는 팔을 곧게 내린 차렷.
  - base는 수평축으로 앞뒤 들기이며, 90→95도는 앞으로 간다.
  - shoulder는 다음 로컬 축으로 옆 벌리기이며, 90→95도는 바깥으로 간다.
  - elbow 90→95도는 굽히기. 회전 서보 5개 범위는 모두 20..160도.
  - wrist pitch/roll은 방향과 기준 미확정이므로 A2 매핑에서 90도로 고정한다.
    gripper 의도값은 기존처럼 그대로 전달한다.
- 변환: A1 base는 방위각, shoulder는 고도각이므로 서보 각도로 직접 +90하지 않는다.
  방위각 az, 고도각 el로 Body 방향 `(right, up, forward)`를 복원한다:
  `right=cos(el)*sin(az)`, `up=sin(el)`, `forward=cos(el)*cos(az)`.
  로봇 직렬축에 맞춰 `b=atan2(forward,-up)`,
  `a=atan2(right,hypot(up,forward))`로 분해한 뒤 base=90+b, shoulder=90+a.
  순수 측방향에서는 b를 0으로 정한다. 마지막으로 20..160 범위를 적용한다.
  elbow는 `270 - A1 내각`: 내각 180→서보 90, 내각 150→서보 120.
  따라서 현재 상한 160은 최대 굽힘 70도를 뜻하며, 더 굽힌 사람 자세는 포화된다.
  기존 unwrap API는 유지하되 삼각함수 입력을 주기 범위로 접어 누적 회전 포화를 막는다.
- FK/안전검사: base를 yaw로 취급하던 평면 모델을 3D로 변경했다.
  로봇 좌표는 어깨 원점, +X 바깥/오른쪽, +Y 앞, +Z 위이고,
  중립 아래 방향에 `Rx(b) Ry(-a) Rx(e)` 순서로 회전한다.
  기존 2D API는 호환을 위한 앞/위 투영으로 남겼으며 안전검사는 3D 선분 거리로 한다.
  링크 11/13/8cm와 기존 충돌 여유 2/1cm는 잠정값을 유지한다. 3cm 견봉 오프셋은 제외.
  마지막 8cm가 실제 그리퍼 끝까지의 길이인지 추후 실측이 필요하다.
  직선 자세/최대 도달 거리만으로 reject하던 조건을 제거하여 idle과 팔 편 동작을 허용한다.
  자기 충돌 검사는 유지한다. 기존 바닥 검사 비활성화는 유지하며,
  복구할 경우 실측 어깨 높이 H에 대해 바닥 z=-H로 해야 한다.
  현재 추정 FK에서 idle 끝점 z=-32cm라는 사실 자체가 바닥 실측값은 아니다.
- 초기 속도 설정: 기존 틱당 제한을 5개 축 모두 0.6도로 낮췄다(20ms 고정 틱에서 30도/초).
  정지 후 출발은 기존 smoothstep, 이동 중 새 목표는 동기화된 선형 추종으로 한다.
  매 50ms 목표 갱신마다 smoothstep의 속도 0부터 다시 시작해 거의 움직이지 않던 문제를
  회귀 테스트로 재현하고 수정했다. 출발점은 실측 각도가 아닌 직전 출력 명령이다.
  틱당 속도는 제한하지만 재목표 시 가속도/속도 연속성이나 토크를 보장하지 않는다.
- 남은 경계: A1 Body +Z는 양어깨로 추정한 기준 방향으로, 실제 몸통 앞방향의 실측값이 아니다.
  A2 변환은 이 입력을 따르며 단안 깊이/앞뒤 모호성을 해결하지 않는다.
  정적 하중/전류/온도 보호는 질량·무게중심 등 측정값이 없어 추가하지 않았다.
  호스트 검증만 수행했으며 보드 빌드/업로드 및 실물 동작 확인은 수행하지 않았다.
- 검증: `python robot_arm/tests/robot_calibration/run_tests.py`로 GCC C99,
  `-Wall -Wextra -Wpedantic -Werror` 9개 테스트 묶음 통과.
  차렷/단일축/복합축 방향, elbow 내각, 한계값, 직선 허용, 3D 자기 충돌,
  연속 재목표와 역방향 속도 제한, A1 회귀, 통합 HOLD/unwrap/trace/PWM 흐름을 확인했다.
  저장소 `etc/uart_pose_stream.bin` 522프레임을 UART→A1→A2→mock A3로 재생:
  522 승인/0 거부, 최대 출력 변화 0.599907도/20ms.
  목표 base 23.559..84.982도, shoulder 29.996..102.325도(둘 다 한계 포화 없음),
  elbow 99.747..160도(152/522프레임 상한), 손목은 모두 90도/1500us.
  재현 스크립트는 실행 파일/CSV를 임시 폴더에 저장한다.
- 사용자 확인 필요: no — 위 확정 사항으로 구현. 손목/하중 후속 기능은 실측 이후 별도 작업.
- Codex 확인 필요: no.

---

## 2. 1차 UART 로그 분석과 수정 순서 인계 (2026-09-22, Codex)

원본: `claude_handoff_20260922.md`. 레거시 6축 경로 기준 첫 UART 로그(`uart_debug_20260922_094849_357326.log`) 분석과 다음 수정 순서 인계.


작성: Codex, 2026-09-22. 인계 작성 시 HEAD: `a944a65`.
이 대화에서는 분석만 수행했으며, 이 인계 파일 외 소스 수정·보드 배포는 하지 않았다.
아래 내용은 분석 당시 코드 기준이다. Agent1 담당자가 별도로 수정했을 수 있으므로 현재 diff와 관련 지침을 먼저 확인할 것.

### 사용자 의도와 우선순위

- 우선 Agent1 수정 결과를 확인하고 A2 각도 변환을 맞춘 뒤, 작은 범위에서 테스트한다.
- 정적 부하 제한 등 추가 제약은 기본 동작을 확인한 뒤 단계적으로 도입한다.
- FK도 실제 중립 자세·각도 변환과 일치시켜야 한다. 변환만 고치고 기존 FK를 그대로 두면 안전검사가 계속 잘못될 수 있다.
- 사용자가 말한 "어깨 roll 반대"는 **base의 좌우 회전**을 의미한다.
- MG996R 공급 전압은 **6V로 할 예정**이다. 실제 공급 상태를 확인한 것은 아니다.
- 링크 질량, 무게중심, 물체 질량, 실제 중립 자세는 아직 측정·확정되지 않았다. 임의값을 실측값처럼 사용하지 말 것.

### 입력 로그와 집계

파일: `C:\Users\kccistc\Downloads\uart_debug_20260922_094849_357326.log`

- A1/P3/A2 각각 522개, TK 1,319개. 기록된 fid/tick 번호 누락 없음.
- A1 전부 `pv=1, vm=63, rc=1, ov=1`. P3 전부 `pm=63, fl=7, age_ms=0`.
- A2: `R,0x4` 474개(90.8%), `N,0x0` 47개, `S,0x0` 1개.
- 프레임 1~339 거부, 340~387 승인, 388~522 거부.
- 첫 승인: fid=340, t=21389ms. 재거부: fid=388, t=23788ms. t는 부팅 기준.
- 마지막 승인 fid=387의 명령은 `[39.1,114.2,163.9,155.7,108.8,1.00]`.
  마지막 TK도 이 명령에 도달한 상태다. 거부 시 마지막 승인 목표로 계속 진행한다.
- 기록된 SM에서 servo error / tick overrun / UART crc,fmt,rng / frame overwrite / trace drop 모두 0.
- trace 버퍼 high-water 최대 424B. A1 실행시간 평균 777.33us, 최대 883us.
- TK 간격은 19~21ms. 이 로그는 소프트웨어 명령/쓰기 성공을 보여주며 실제 축 피드백은 아니다.

### A1: 몸 Y축 반전 재현

대상: `src/human_target_angle/pose_math.c`
`pm_build_body_frame()` 및 `pm_update_stable_body_frame()`.

현재 흐름:

1. X = normalize(shoulder_r - shoulder_l)
2. Y = camera_up을 X에 수직으로 투영 후 정규화
3. Z = X cross Y
4. Z가 camera_forward 반대이면 Z만 반전
5. Y = Z cross X로 다시 계산

4번에서 Z를 뒤집으면 5번의 Y도 원래 위쪽과 반대가 된다.
"해부학적 좌→우 X", "영상 위쪽 Y", "항상 camera_forward 쪽 Z", "오른손 좌표계"를
이 입력에서 모두 강제할 수 없다. 유지할 축 규약을 명시하고 초기 생성과 필터 갱신 양쪽을 맞춰야 한다.
출력 shoulder만 부호 반전하는 방식으로 덮지 말 것. Z 반전 삭제 역시 base/roll 기준에 미치는 영향 검토가 필요하다.

첫 P3:

```text
shoulder_l = ( 0.298, 0.750, 7.253)
shoulder_r = (-0.585, 0.750, 6.783)
elbow      = (-0.754, 0.015, 6.766)
```

활성 팔은 `POSE_ARM_RIGHT`. 이 좌표로 현재 계산을 재현하면:

```text
body_x = (-0.88274,  0, -0.46986)
body_y = ( 0,       -1,  0)
body_z = (-0.46986,  0,  0.88274)
base = 67.72deg
shoulder = +76.99deg
```

로그 A1의 base=67.7, shoulder=77.0과 일치한다.
카메라 Y는 위쪽 양수인데 팔꿈치가 어깨보다 아래임에도 양의 고도각이 나온다.
같은 첫 프레임에서 위쪽 Y를 유지한 고도각은 약 -77deg다.
수정 후 전체 출력이 단순 부호 반전과 동일하다는 뜻은 아니다.
손목 roll은 body_y로 reference normal을 만들므로 함께 검증해야 한다.

base 실물 역회전 원인은 이 문제만으로 확정하지 못했다.
A2 base는 현재 scale=1, direction=+1, offset=90이며 `clamp(A1 base+90,10,170)`이다.
실물 방향만 반대라면 base.direction이 조정 지점이지만, A1 새 좌표 규약과 실제 축 방향을 먼저 확인한다.

### A2: 팔꿈치 각도 규약 불일치

- A1 `pose_joint.c`: elbow는 내각, 팔을 펴면 180deg.
- A2 `robot_calibration_config.c`: 모든 관절에 기본적으로 사람각+90deg 적용.
- FK `safety_check.c`: elbow/wrist_pitch 서보 90deg가 이전 링크와 일직선이라고 가정.
- 따라서 펴진 사람 팔이 180+90=270 → clamp 170으로 매핑된다.
- 로그 447/522개에서 elbow 명령이 상한 170deg에 포화한다.

사람 내각에서 굽힘량(180-내각)을 구하고 실제 서보 중립·방향에 맞게 매핑해야 한다.
물리적 조립 규약을 확인하지 않고 최종 offset/direction을 확정하지 말 것.
A1의 내각 정의 자체를 A2 임시값에 맞춰 변경할 문제는 아니다.
이 규약 불일치는 기존 `docs/agent2_design_log.md`의 2026-09-19 통합 기록에도 언급되어 있다.

### 바닥 충돌: 현재 계산과 실제 설치 기준

A1 카메라는 +Y up, A2 FK는 독립적인 로봇 평면 +Z up이다. A1 좌표를 A2에 직접 넣는 구조가 아니다.
현재 FK는 어깨 z=0, 바닥도 z=0으로 간주하고 elbow/wrist/tip 중 하나라도 z<0이면 거부한다.
현재 방향식:

```text
upper = shoulder_servo
forearm = upper + elbow_servo - 90
hand = forearm + wrist_pitch_servo - 90
```

첫 프레임 매핑 후 sh/el/wp=160/170/144.8deg, 계산 높이는:
elbow=+3.762cm, wrist=-7.496cm, tip=-14.758cm → FLOOR_COLLISION.
522개 로그의 반올림된 명령각으로 FK를 재계산했을 때 바닥 판정과 실제 승인/거부가 모두 일치했다.
즉 현재 모델대로 거부하는 것이지만 모델이 실제 설치와 맞는지는 별도다.

사용자 사진에서:
- 베이스는 책상 위에 놓인 구조.
- 어깨→팔꿈치 11cm, 팔꿈치→손목 13cm 표기.
- 손목 이후 8cm 표기의 끝은 사진상 실제 집게 끝보다 안쪽으로 보인다. 정확한 끝점은 미확정.
- 3cm 표기는 사용자가 무시해도 되는 견봉 같은 오프셋이라고 설명했다. 상완 길이에 더하지 않는다.
- 사진 자세가 모든 서보 90deg/1500us 상태인지는 답변받지 못했다.

**바닥은 어깨 회전축에서 실제 책상까지의 수직 높이 H를 측정해 z_floor=-H로 정한다.**
대화 중 -32cm가 제시된 적 있으나, 이는 "아래로 완전히 편 손끝이 바닥에 닿는다"는 조건부 설명이었다.
사진의 실제 설치에 -32cm를 그대로 적용하면 안 된다. 총 링크 길이와 어깨 지상고는 서로 다른 값이다.
현재 코드에서는 모든 서보 90deg일 때 위로 일직선(+32cm)이다. 실제 중립이 무엇인지 측정해 FK에 반영할 것.

### 기존 특이점·도달거리 제한

- `abs(elbow_servo-90)<=5` AND `abs(wrist_pitch_servo-90)<=5` → NEAR_SINGULARITY(0x08).
- 어깨→손끝 거리 >= 32*0.98=31.36cm → REACH_BOUNDARY(0x10).
- 두 조건은 실제 야코비안 계산이나 모터 부하 기반 보호가 아니다.
- 펴진 팔로 어깨만 회전하는 동작도 막힌다. 매핑을 정상화하면 새로 이 거부가 드러날 수 있다.
- 현재 제어는 사람 관절각→서보 목표각 직접 변환이며 Jacobian 역산 제어가 아니다.
- 사용자는 사이드 레터럴 레이즈처럼 편 팔 동작도 의도한다. 해당 동작을 허용할 정책과 이 두 거부 조건을 함께 검토한다.
- 사용자와 무조건 모든 보호를 해제하기로 합의한 것은 아니다. 진단 기록 유지와 제한된 테스트가 제안됐다.

### 속도 제한은 이미 존재

`robot_calibration_config.c`의 `max_delta_deg`는 20ms 틱당 명령각 변화 상한:

| 관절 | deg/tick | deg/s |
|---|---:|---:|
| base/shoulder | 3 | 150 |
| elbow | 4 | 200 |
| wrist_pitch/wrist_roll | 5 | 250 |

`robot_calibration_set_target()`이 공통 이동 시간을 정하고,
`robot_calibration_step()`에서 smoothstep 및 `motion_limits_step_toward()`로 제한한다.
명시적인 가속도 상한은 없다. 실제 축 속도 센서 피드백도 아니다.
초기 테스트는 기존 가동범위/속도 제한을 유지하고 낮은 명령 속도에서 시작하는 방향이다.
구체적인 새 속도값은 아직 확정하지 않았다.

### 정적 부하 제한은 후속 작업

아직 구현하지 않는다. 먼저 동작·좌표 규약을 맞추고 측정 자료를 확보한다.

- 상완/전완/손목·그리퍼를 강체 묶음으로 나눠 모터·브래킷·볼트 포함 질량과 무게중심을 측정한다.
- 부품은 실제 고정된 링크 묶음에 포함하며 중복 계상하지 않는다. 추가 물체의 질량·위치도 필요하다.
- MG996R 공식 6V 스톨 토크는 11kgf·cm(약 1.08N·m). **연속 허용 토크가 아니다.**
  https://towerpro.com.tw/product/mg996R/
- 연속 정적 토크 허용값은 아직 정하지 않았다. 임의 비율을 검증된 안전값처럼 사용하지 않는다.
- 후속 구현 구상: 질량/무게중심/허용 토크 설정 + 정적 토크 계산 모듈을 A2 safety_check에 연결.
- 목표 자세뿐 아니라 보간 경로와 각 틱 후보 자세를 검사해야 한다. 상태 갱신 전에 검사할 것.
- 현재 홈 초기화는 일반 safety_check를 우회하므로 홈도 따로 검토해야 한다.
- 현재 자세가 이미 과부하인 상황에서는 마지막 목표 유지가 해결책이 아니다. 별도 대응 정책이 필요하다.
- 속도를 낮추는 것은 정적 중력 부하 자체를 줄이지 않는다.

### 다음 세션의 권장 진행

1. 현재 코드와 Agent1 담당자의 변경사항을 확인하고 Y축 문제의 수정·회귀 테스트를 검증.
2. 실제 중립각/방향을 확인해 A2 변환과 FK를 일치시킴. 어깨 높이와 손끝 길이는 미확정 상태로 명시.
3. 펴진 팔 동작에 대한 특이점/도달거리 검사 정책 검토.
4. 호스트에서 로그 재생/대표 자세 테스트 → 실물 각 축 작은 동작 → 좁은 범위 통합 확인.
5. 예상 서보각, 실제 자세, FK가 일치하면 부하 제한 등의 추가 보호를 단계적으로 설계.

현재 미확정값 때문에 임의의 하드웨어 자세·토크 한계를 사실로 채우지 말 것.
실물 테스트는 물체 없이 시작하더라도 팔 자체 무게가 있으며, 수평 완전 신전 장시간 유지는 첫 시험으로 삼지 않는다.

---

## 3. Agent1 BodyFrame 수정 검토 프롬프트 — 레거시 6축 (2026-09-22)

원본: `agent2_handoff_prompt.md`. Agent1 body-frame 수정을 검토하기 위해 작성한 작업 지시 프롬프트(레거시 6축 대상). 5축 전환 이후로는 다음 절(4)로 대체됐다.


> 아래는 이전 6축 BodyFrame 검토 프롬프트입니다. 새 5축 작업에는
> [agent2_forearm_handoff_prompt.md](agent2_forearm_handoff_prompt.md)를 사용하세요.

너는 C 기반 2D 로봇팔 프로젝트의 “2D Kinematics / Motion Engineer (Agent2)”다.
Agent1 body-frame 수정이 반영된 현재 코드를 기준으로 A2 mapping/FK/safety를 분석하라.
이번 요청은 분석·호스트 재현·수정안 제안까지다. A2 운영 코드, calibration,
servo limits, floor threshold, A3/PWM, Agent1 내부를 임의 변경하지 않는다.
보드 구동이나 flash도 하지 않는다. 재현용 분석 파일/결과는 별도로 만들 수 있다.

### 1. 먼저 확인할 자료

- `docs/coordinate_system.md`: 확정된 Agent1 좌표/각도 계약.
- `docs/agent1_axis_review.md`: 선택 근거, 수정 범위, 테스트와 한계.
- `src/human_target_angle/pose_math.c`, `pose_joint.c`, `pose_hand.c`.
- `src/robot_calibration/`와 `src/integration/agent_pipeline.c`의 실제 호출 순서.
- `tests/human_target_angle/run_axis_tests.sh`: 호스트 검증 재실행 명령.
- 입력: `etc/example_pose2d_1280x720_20hz.csv`, `etc/uart_pose_stream.bin`.

현재 diff와 실행 결과를 직접 확인하라. 과거 답변의 “수정 완료”만 믿거나
아래 관측값을 현재 보드 실측 결과로 인용하지 않는다.

### 2. 반영된 Agent1 수정

기존 코드는 Z를 camera +Z로 반전한 뒤 Y=Z×X로 재계산하여 Y도 아래로 뒤집었다.
초기 생성과 stable 갱신 모두 다음 공통 규약으로 수정했다.

```text
U = camera up = (0,1,0)
X = anatomical left shoulder → right shoulder (후속 프레임은 시간 필터 적용)
Y = normalize(U - dot(U,X)*X)
Z = normalize(X × Y)
```

오른손 cross-product 규약과 Y-up을 유지한다. Z를 camera forward로 강제하지 않는다.
화면상 어깨 순서로 anatomical label을 바꾸지 않는다.
raw/filtered X가 up과 거의 평행하여 projected-up 길이가 0.01 미만이면
major 계산 실패로 전달하고 기존 bounded HOLD/invalid 정책을 사용한다.
오래된 frame을 새 관측으로 무한 유지하지 않는다. 정확한 pole 관통의 연속성은 보장하지 않는다.

주어진 반올림 첫 P3 좌표:

```text
SL=(0.298,0.750,7.253), SR=(-0.585,0.750,6.783), E=(-0.754,0.015,6.766)
기존 Y=(0,-1,0), shoulder=+76.9878°
수정 Y=(0,+1,0), shoulder=-76.9878°, base=112.2811°
```

동일 UART binary, right arm, dt=0.05, A1 초기화 후 호스트 재생 첫 출력:

| 값 | 수정 전 | 수정 후 |
| --- | ---: | ---: |
| base | 67.714409 | 112.285576 |
| shoulder | 76.963593 | -76.963593 |
| elbow | 165.819260 | 165.819260 |
| wrist pitch | 54.848621 | 54.848621 |
| wrist roll | 65.560059 | 65.560059 |

이 첫 roll이 같다고 전체 roll이 유지되는 것은 아니다. Body Y reference와
X/Z fallback, normal history, zero calibration 때문에 프레임마다 영향이 다르다.
전체 shoulder/roll에 고정 부호나 offset을 적용하여 새 결과를 추정하지 않는다.

### 3. A1 출력 계약 — 로봇 servo 각도가 아님

| 필드 | 확정 의미 |
| --- | --- |
| base_deg | `atan2(upper·X,upper·Z)`. +Z=0, +X=+90, -X=-90. [-180,+180]. 수직 상완에서 방위각 불확정. |
| shoulder_deg | Body XZ plane=0, +Y쪽 양수/-Y쪽 음수. [-90,+90]. |
| elbow_deg | human 내각: straight=180, folded=0. [0,180]. A1 의미 변경 금지. |
| wrist_pitch_deg | F=전완 단위방향, H=손목→finger center 단위방향, S=finger1→finger2 단위방향. A=normalize(S의 F 수직 투영), 실패 시 normalize(Z×F). `atan2(A·(F×H),F·H)`; F=H이면 0, A축 오른손 방향 양수. [-180,+180]. |
| wrist_roll_deg | R=Y의 F 수직 투영 후 정규화, 품질 부족 시 X→Z fallback. N=손 평면 normal을 F에 투영한 후 부호 이력/EMA 적용. `atan2(F·(R×N),R·N)`. raw zero=R=N, F축 오른손 방향 양수. calibration offset 차감/필터 후 [-180,+180]. |
| gripper_norm | 0=CLOSE, 1=OPEN; 현재 이진 의도. 그대로 전달. |
| valid | 사용 가능한 major target. 손목/그리퍼의 최신 관측 여부와 동일하지 않음. |

정확한 수식·fallback·필터 순서는 좌표 문서와 코드를 기준으로 하라.
±180 양 끝은 모두 허용되며 A1 내부 unwrap과 외부 wrapped 출력을 구분한다.
A2에서 shortest-angle을 처리해도 유한 범위 servo가 360° 연속 회전 가능한 것은 아니다.

### 4. 검토할 항목

#### Shoulder

현재 `human * scale * direction + zero_offset` 및 clamp 호출 순서를 코드로 확인하라.
기존 +90이 실제 servo zero, 링크 수평, FK의 0°와 어떻게 연결되는지 표로 작성하라.
기계적 근거 없이 +90을 유지/제거하거나 A1 부호 오류를 A2 offset으로 상쇄하지 않는다.

#### Elbow

straight=180인 A1 내각이 현재 A2 +90과 결합해 170° limit에 붙는지 실제 값을 추적하라.
robot servo angle과 FK relative bend의 정의를 분리하고 direct/보각/offset/direction
중 필요한 변환을 기계 구조 근거와 함께 제안하라. A1 elbow 의미를 바꾸라고 요청하지 않는다.

#### Base

영상과 실물 회전이 반대라는 사용자 관찰을 다음 세 단계로 분리하라:

1. A1 +Z zero / +X positive / wrap.
2. A2 direction, offset, scale, unwrap와 clamp 순서.
3. 실물 장착 방향, 링크 회전 방향, 영상 mirror 여부.

이번 Y 수정만으로 역회전 원인을 확정하지 않는다.

#### Wrist

Pitch 부호는 finger 순서와 계산축 기준이다. 단순 위/아래 의미로 가정하지 않는다.
Roll zero/normal history/reference fallback과 robot servo neutral을 구분하라.
보고된 -117°→unwrap 약 242°→180° 포화 사례는 해당 원본/상태를 확보한 경우 재현하라.
숫자를 맞추려고 A1에 보정 부호를 요구하지 않는다.
unwrap 누적각을 clamp에 넣어 한쪽 limit에 고정하는 문제가 있는지 확인하라.

#### FLOOR_COLLISION / FK

사용자가 보고한 이전 로그: 522프레임 중 474 reject, 모두 0x04.
이는 제공된 보고값이며 이번 작업에서 해당 이름의 원본 전체 로그를 검증한 값은 아니다.
수정 후 전체 A2 reject 횟수는 아직 확정하지 않았다.
통합 테스트의 측면 6/6 승인과 전체 522프레임 통계를 혼동하지 않는다.

다음 원인을 각각 검토하라:

- human target 의미 / robot mapping / clamp.
- servo command와 FK 수학 관절각 사이 변환.
- shoulder 원점의 높이, floor 원점, link 길이와 단위.
- 검사 대상이 tip만인지 각 링크까지인지, 실제 불가능한 자세인지.
- floor threshold 및 실제 servo zero/direction.

각 대표 reject에서 `raw A1 → unwrapped → mapped before clamp → after clamp
→ FK joint/tip 좌표 → floor 값/기준 → flags`를 추적하라.
거부 횟수를 줄이기 위해 안전 기준을 먼저 완화하지 않는다.
사용자는 servo PWM 설정이 실측값이라고 확인했다. 이를 임의의 기본값으로 되돌리지 않는다.
PWM endpoint 실측만으로 FK zero/장착 방향까지 검증되었다고 보지도 않는다.

### 5. 재현 조건과 상태

가능하면 같은 원본 입력을 구버전 A1과 수정 A1에 각각 넣고 A2/A3 코드는 고정한다.
사용자 worktree를 reset/checkout해서 수정사항을 없애지 않는다. 별도 임시 사본으로 재현한다.
구버전이나 원본 로그가 없으면 전후 A2 통계를 추정하지 말고 확보 가능한 범위와 누락을 표시하라.

- 입력 파일/hash, 좌표 반올림·UART 양자화, frame 순서, active arm, dt를 기록.
- CSV 도구는 첫 dt가 1/15초이고 UART 도구는 지정 dt=0.05다. 동일 조건 비교 시 맞출 것.
- A1 reset, roll zero calibration, A2 unwrap, 이전 command, motion state,
  Agent3 target/실제 servo 시작 자세를 구분.
- 초기화된 fresh run과 상태가 남은 두 번째 run을 별도 실험으로 취급.
- CSV 재전송 자체가 보드의 모든 state reset을 뜻하지 않는다. 실제 reset 경로를 확인.
- A1 UPDATE/HOLD/INVALID, A2 validate/safety 거부, 승인된 동일 target/새 target을 구분.
- PWM write 성공은 실물 위치 피드백이 아니다. 거부 중에도 이전 승인 target으로
  진행하던 motion ramp가 남을 수 있으므로 “거부=즉시 물리 정지”라고 단정하지 않는다.

프레임별로 모든 A1 각도, mapped/unwrapped/clamped 값, clamp 발생 여부,
FK/floor/flags, accept/reject를 남겨 동일 522프레임의 집계를 비교하라.
clamp 발생은 입력과 출력이 달라졌는지로 세고, 단순 limit 도달 횟수와 구분한다.

### 6. 검증과 보고

기존 mapping/limit/unwrap/safety 테스트와 동일 입력 재생을 실행한다.
추가 분석 테스트는 0/±90 shoulder, elbow 0/90/180,
base/roll 양방향 ±180 경계, reset 유무, floor 대표 승인/거부를 포함한다.
테스트만으로 확인한 것과 실물 확인이 필요한 것을 구분한다.

최종 보고 순서:

1. 현재 joint별 raw→unwrap→scale/direction/offset→clamp→FK 변환과 코드 근거.
2. 새 A1 규약과 compatible / needs remap / needs physical measurement 구분.
3. Shoulder +90, elbow 포화, base 방향, wrist wrap/reference 영향.
4. 같은 조건의 수정 전/후 승인·거부·clamp 통계와 대표 frame 추적.
5. 근거가 있는 A2 수정 제안(아직 적용하지 않은 안임을 명시).
6. 실제 변경 파일과 테스트 명령/결과, 입력·초기화 조건.
7. 해결된 A1 Y축 문제 / 남은 A1 추정 한계 / A2 mapping·FK 문제 /
   실물 확인 필요 정보 / collision parameter 검토 필요 여부를 각각 정리.

실물 정보가 부족하면 필요한 측정값과 수식의 미정 항목을 제시하라.
이번 분석 요청을 mapping/calibration/safety 변경 승인으로 해석하지 않는다.

---

## 4. Agent2 5축(forearm) 전환 작업 지시 프롬프트 (2026-09-22)

원본: `agent2_forearm_handoff_prompt.md`. 교정된 Agent1 HumanForearmTarget 인터페이스를 기준으로 Agent2를 5축(elbow_roll/elbow_pitch/wrist_pitch/wrist_roll/gripper)으로 전환하라는 Codex 작업 지시 프롬프트.


아래 블록은 교정된 Agent1의 실제 인터페이스와 호스트 테스트 결과를 기준으로 작성했다.
앞선 고정 table 기준 Agent1 계약을 대체한다.

```text
너는 이 C 로봇팔 프로젝트의 Agent2(Kinematics / Motion Engineer)다.
교정된 Agent1 HUMAN angle 출력을 사용해 5축 수평 설치 로봇의 A2 전환을 구현·검증하라.
현재 working tree/diff와 의존성을 먼저 확인하고 기존 변경을 보존하라.
Agent1 알고리즘·사람 각도 부호, 실측 PWM calibration, A3 PWM을 임의 변경하지 마라.
reset/pull/전체 작업본 복사, 별도 승인 없는 보드 flash/서보 구동을 하지 마라.
미확정 기구/설치/서보 측정값은 추측하지 말고 필요한 값을 명시하라.

[먼저 읽을 파일]
README_AGENT1_TEST.md
docs/agent1_forearm.md
include/common/robot_types.h
include/human_target_angle/forearm_mapping.h
include/human_target_angle/agent1_forearm_stage.h
src/human_target_angle/forearm_mapping.c 및 pose_hand.c
현재 src/robot_calibration/, src/integration/, include/output_controller/

[역할]
Agent1: HumanPose2D → relative XYZ → 양 어깨 기반 Human Body Frame → HUMAN angles.
Agent2: HUMAN angles → 설치/서보 보정/FK/safety → ROBOT angles.
Agent3: ROBOT angles → PWM 및 actuator/record/playback.
Agent1에서는 TableFrame이나 robot mounting 정보를 사용하지 않는다.
설치 방향/table orientation은 Agent2에서 처음 적용한다.
입력 6-landmark HumanPose2D와 36-byte UART packet은 유지된다.

[실제 공유 타입 — include/common/robot_types.h]
typedef struct {
    float elbow_roll_deg, elbow_pitch_deg;
    float wrist_pitch_deg, wrist_roll_deg, gripper_norm;
    uint32_t frame_id;
    uint8_t valid, elbow_roll_observable, hand_fresh;
} HumanForearmTarget;

기존 HumanJointTarget은 legacy 6축 A2/integration 때문에 남아 있다.
새 타입을 cast/alias하거나 base/shoulder/elbow 필드에 억지로 대입하지 마라.
현재 보드 pipeline은 아직 기존 agent1_stage_* 경로다.
새 agent1_forearm_stage_init(void), run(pose, side, dt), output()으로 명시적으로 연결하라.
init에는 설치값이 없다. 필요 시 forearm_mapping_init/update context API를 사용할 수 있다.
Vitis linked source 등록/ROBOT_TRACE getter 및 새 trace field 순서를 확인하라.
기존 getter와 PC 자동 로그 저장을 보존하라.

[새 로봇 매핑]
Motor0 elbow_roll  <- HumanForearmTarget.elbow_roll_deg
Motor1 elbow_pitch <- HumanForearmTarget.elbow_pitch_deg
Motor2 wrist_pitch <- wrist_pitch_deg
Motor3 wrist_roll  <- wrist_roll_deg
Motor4 gripper     <- gripper_norm
기존 base/shoulder/anatomical elbow 매핑을 재사용하지 마라.
예전 elbow_deg는 펴진 팔≈180°인 내각이다. 새 elbow_pitch는 다른 물리량이다.

[BodyFrame과 elbow 각도]
Body X = normalize(Shoulder_R-Shoulder_L), 기존 stable frame 필터 사용.
Body Y = CameraUp(0,1,0)을 X에 수직 투영·정규화.
Body Z = X×Y. Z를 Camera forward 쪽으로 강제 반전하지 않는다.
zero인 +Body Z를 항상 실제 인체 정면이라고 단정하지 마라.
Body Y는 CameraUp 기반 추정이므로 몸 숙임의 완전한 3D orientation을 관측하지 못한다.
공급된 Frame+팔을 같은 3D 회전으로 돌리는 수학적 불변성과 실제 Frame 추정은 구분한다.

F=normalize(Wrist-Elbow); fx=F·BodyX, fy=F·BodyY, fz=F·BodyZ.
elbow_roll=atan2(fx,fz):
  Body +Y 주위 방위각, +Z=0, +X 방향 양수, [-180,180), wrap.
  전완 자체의 비틀림이나 실제 servo angle이 아니다.
elbow_pitch=atan2(fy,hypot(fx,fz)):
  Body XZ 평면=0, +BodyY 위쪽 양수, [-90,90], no wrap.
A1은 이 라디안 계산을 도 단위로 변환하고 기존 EMA를 적용한다.

[손목]
H=normalize(finger midpoint-Wrist), S=normalize(Finger2-Finger1).
A=normalize(S를 F에 수직 투영).
wrist_pitch=atan2(A·(F×H),F·H), [-180,180), wrap.
전완과 손이 같은 방향이면0, A축 오른손 회전이 양수다.
기존6축 HUMAN wrist 부호를 보존했다. 항상 위쪽 굽힘이 양수라고 가정하지 마라.

wrist_roll은 F주위 오른손 회전, [-180,180), wrap.
N은 H×S를 F에 수직 투영한 normal에 부호 연속화/기존 EMA를 적용한 것이다.
h=sin(raw_elbow_roll)BodyX+cos(raw_elbow_roll)BodyZ,
R=normalize(project_perpendicular(cos(raw_elbow_pitch)BodyY-sin(raw_elbow_pitch)h,F)).
roll=atan2(F·(R×N),R·N), R과 N이 같으면 zero.
일반 자세에서는 R이 BodyY의 수직 투영이며, pole 근처는 마지막 raw elbow_roll을 사용한다.
기존 unwrap/raw spike 완화/roll zero/EMA를 사용한다.
Normal 부호180° 모호성, 라벨 교환, 손 geometry 퇴화의 한계가 남아 있다.
A1의 기존35° raw spike 완화를 물리적 motor rate limit으로 사용하지 마라.

gripper_norm: 0=CLOSE, 1=OPEN. 접촉/압력 보호는 A3 책임이다.

[유효성/특이점/시간]
return1=fresh major, 0=HOLD/duplicate, -1=invalid.
valid는 major geometry 목표 유효성이지 robot safety 승인/손 최신성 보장이 아니다.
frame_id는 마지막 fresh major 목표의 입력ID이며 HOLD는 이전ID를 유지한다.
major dropout/재구성/BodyFrame 실패는 실제 dt 누적0.35초까지 HOLD, 이후 invalid.
중복 frame은 재필터링/시간 aging하지 않는다. 입력 중단의 실제 timeout은 상위에서 처리하라.
finger missing이면 major는 계속 fresh, 손목/gripper만 유지한다.
손 초기값0/0/OPEN, hand_fresh=0을 새 측정으로 해석하지 마라.

q=hypot(fx,fz), q<0.02에 방위각 특이점 진입, q>=0.04에 이탈.
특이점에서 elbow_roll과 raw heading은 HOLD, pitch는 계속 갱신한다.
elbow_roll_observable=0. 시작부터 수직이면 roll0은 임시값이고 heading 관측 전 hand도 미갱신.
duplicate/major HOLD에서도 elbow_roll_observable=0, hand_fresh=0.
관측 기준 밖 near-pole 및 이탈 후 큰 angle 변화는 남아 있다.
A2에서 관측 불가/재진입/hand stale 정책을 명시하라.
yaw/손목 shortest-angle를 사용하되 실제 유한 서보 범위를 우회하지 마라.
elbow_pitch에는 circular unwrap을 적용하지 마라.

[A2 구현 범위]
physical zero/direction/offset/scale/min/max, rate limit, 새 FK/링크/축/workspace/
table-floor collision을 실제 새 수평 설치에 맞춰 검토·구현하라.
vertical installation assumptions must be reviewed.
기존 수직 설치 FLOOR_COLLISION/FK를 그대로 적용해도 되는지 검증하라.
안전검사를 끄거나 사람 각도를 왜곡해 거부를 피하지 마라.
servo 방향 때문에 Agent1 부호를 변경하지 마라.
실측값이 없으면 필요한 측정 목록을 보고하고 해당 실물 적용은 보류하라.

[현재 테스트·522 replay]
PC Debug -Werror 빌드와 CTest7/7, 기존 axis suite, viewer3/3 PASS.
신규 Vitis 전체/실물 구동은 NOT VERIFIED.
right-arm522프레임: fresh/valid522, invalid0, major HOLD0,
elbow roll 미관측1(frame171), hand HOLD9, gripper 전환23.
범위와 최대 shortest-angle frame delta(도):
  elbow_roll  -176.24~179.11, max59.65(frame32)
  elbow_pitch -84.59~16.69,  max12.85(frame509)
  wrist_pitch   5.29~81.18,  max10.96(frame511)
  wrist_roll -179.86~179.25, max23.59(frame34)
elbow roll wrap:36,38,90,96; wrist roll wrap:93,96,232,233.
31→32 BodyFrame 변화0.45°, 전완 방향 변화5.25°이며
수평 성분이 작아 방위각 변화가 크게 증폭된다. 단순360° wrap 버그로 처리하지 마라.
171→172 수직 이탈/raw jump 및 이후 필터 추종도 회귀 사례로 사용하라.
카메라 깊이 합성 교란에 민감하지만 실제 오차와 사람 움직임을 로그만으로 분리할 수 없다.

robot_arm에서:
cmake -S . -B build/agent1 -DAGENT1_ONLY=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/agent1 -j
ctest --test-dir build/agent1 --output-on-failure
./build/agent1/test_pose_csv etc/example_pose2d_1280x720_20hz.csv build/agent1/forearm_result.csv
python3 tools/analyze_forearm_replay.py build/agent1/forearm_result.csv --expect-frames 522
bash tests/human_target_angle/run_axis_tests.sh

AGENT1_ONLY는 A2 검증 빌드가 아니다.
A2 단위/통합 replay를 추가해522프레임의 거부 사유/saturation/제한 후 delta/FK/
collision/초기·종료·dropout·재진입/양쪽 wrap/수직/끝단 자세를 검증하라.
입력 timestamp는 약33/67ms로 파일명20hz라도 고정50ms가 아니다.

[Agent3 인계]
SERVO_COUNT는 현재6이다.
ROBOT_MOTION_JOINT_COUNT=5는 기존 gripper 제외5관절이며 새 총5서보와 다르다.
새 구성은 비-gripper4관절+gripper1이다.
JointCommand/PwmCommand, enum/index/config/HAL/PL wiring/startup,
trace/record/playback의 채널과 저장 포맷 변경을 담당자에게 전달하라.
실제 존재하는 파일을 확인하고 실측 PWM 값을 보존하라.

최종 보고: 확정 매핑/축 규약, 수정 파일, 실제 테스트와522 replay 통계,
미확정 실측값, 보드 검증 여부, Agent3 인계사항을 구분하여 보고하라.
```

---

## 5. Agent2 5축(forearm) 독립 검증 리포트 (2026-09-22)

원본: `agent2_forearm_crosscheck_20260922.md`. 위 4절 지시에 따른 Codex 작업 결과를 Claude가 독립적으로 재현·검증한 리포트. FK yaw 부호 오류와 중간 램프 충돌 누락을 이때 발견/수정했다.


결론: Claude의 12개 테스트 PASS, A1 replay 통계, A2 170 승인/352 거부는 재현했다.
그러나 이를 새 기구의 정상 추종/실물 안전 검증으로 해석할 수 없다. FK yaw 부호 오류와
중간 램프 충돌 누락을 수정했다. 유한 서보 범위에서의 unwrap 복귀 문제와 실측/소비 정책은
미해결이다. 보드 연결·구동 준비 완료 판정이 아니다.

검증 기준 HEAD는 `400d830`. 검증 시작 당시 tracked 변경은 `run_tests.py` 하나,
새 forearm 헤더 4개/소스 4개/테스트 3개는 untracked였다. 기존
`claude_handoff_20260922.md`는 별도로 존재했고 건드리지 않았다.

### 1. 주장별 판정

| 주장 | 독립 확인 결과 |
|---|---|
| 기존 legacy/A1/A3/integration을 변경하지 않았다 | 이번 A2 작업의 HEAD 대비 diff 기준 맞음. 보호 대상 25개 파일은 검증 전후 SHA-256도 동일 |
| runner에 새 3개 케이스만 추가했다 | 맞음. 추가 블록을 제거하면 HEAD의 runner와 문자열 전체가 동일. 검증자는 runner를 추가 수정하지 않음 |
| 새 12개 suite가 `-Werror`로 PASS | 수정 전/후 모두 직접 재현. 최종 GCC 16.1.0, C99, `-Wall -Wextra -Wpedantic -Werror` |
| 링크 길이와 손목 roll/pitch 합성이 보존된다 | 가정한 모델에서는 맞음. 다만 원래 yaw는 +Z 오른손 방향과 반대여서 수정 |
| legacy base/shoulder와 같은 회전 모델이다 | 부정확. 현재 legacy는 수평축 차렷 모델이고, 새 모델은 +Z yaw/수평 기준 elevation 모델 |
| 24cm=전완, 10cm=손이 실제 기구와 맞다 | 확인 불가. 수치 배정은 가정으로 유지 |
| 기본값이 확실히 보수적이다 | 입증 불가. 서보 방향/중립/끝단, 테이블 높이, 링크 두께/하중이 미측정 |
| A1 522 fresh와 yaw/pitch 범위가 문서와 일치 | 문서가 표시한 소수 둘째 자리까지 일치 |
| 352 TABLE 거부는 데모와 새 기구의 정상 불일치다 | 임시 모델에서의 수학적 거부는 맞음. 실제 기구에 대한 원인 판정은 미확정. 동시에 yaw 포화 버그 존재 |
| replay PASS가 움직임 검증이다 | 원래 테스트로는 아님. 승인된 yaw 목표가 모두 160도여도 PASS. 원래 테스트는 승인 프레임에서만 1틱 진행 |

현재 HEAD에는 앞서 병합된 Agent1 변경이 이미 포함되어 있다. 위의 '미변경'은 저장소 역사
전체가 아니라 **이번 Claude A2 작업과 이 검증 작업**의 경계를 뜻한다.
`HumanJointTarget`, `JointCommand`, `servo_config.h`, `servo_control.h`는 그대로이며
현재 `SERVO_COUNT`는 여전히 6이다. integration은 `agent1_stage_*` legacy 경로를 호출한다.
새 `agent1_forearm_stage_*` 연결이나 PWM 채널 변경은 하지 않았다.

### 2. FK 재유도와 부호 수정

서보 각에서 90도를 뺀 물리 모델 각을 q=elbow_roll, p=elbow_pitch,
r=wrist_roll, w=wrist_pitch라 한다. 계산에서는 radians를 쓴다.
새 FK의 중립 전완은 robot +Y, +Z는 위, +X는 오른쪽이다.
A1 Table의 (+X,+Y,+Z)를 robot (+Y,-X,+Z)로 옮기는 것은 반사가 아닌 회전이다.

수정 후 수식:

```text
F = (-sin(q) cos(p),  cos(q) cos(p), sin(p))
N = ( sin(q) sin(p), -cos(q) sin(p), cos(p))
S = ( cos(q),         sin(q),        0) = F × N
Nr = cos(r) N + sin(r) S
H = cos(w) F + sin(w) Nr

elbow = (0,0,0)
wrist = 24 F
tip   = 24 F + 10 H
```

F/N/S는 단위벡터이며 서로 수직이다. 따라서 Nr는 F에 수직인 단위벡터이고
|H|=1이므로 두 링크의 길이는 임의 각도에서 24/10cm로 유지된다.
r은 F 주위 오른손 회전으로 굽힘 평면을 바꾼다. w=0이면 손 중심선에는 roll 영향이 없다.

독립 기준은 전개한 위 수식을 복사하지 않고 Cartesian 회전행렬
`Rz(q) Rx(p) Ry(r) Rx(w)`를 중립 손 방향 `(0,1,0)`에 적용했다.
테스트에는 Rodrigues 회전을 이용한 625개 조합을 추가했고, 별도 Python/ctypes 도구로
실제 C FK를 임의 자세 10,000개와 비교했다.

- 수정 전: 길이는 보존되지만 독립 RH 회전행렬과 최대 위치 차이 약 63.05cm.
- 예: q=+30도에서 원래 wrist=(+12,20.78461,0), RH 기준은 (-12,20.78461,0).
- 수정 후: 최대 위치 오차 `8.21e-6cm`, 최대 링크 길이 오차 `3.21e-6cm`.
- 중립 q=p=0에서 w=r=+90이면 손 방향은 +X: roll이 굽힘 평면을 실제 회전시킨다.

`forward0`와 `right0`의 yaw 부호를 고쳤다. 서보 보정 direction/offset 자체는 바꾸지 않았다.
손목이 실제로 roll→pitch 순서인지, 두 축이 모델처럼 같은 위치에서 만나는지는 미확정이다.
서보 채널 번호 M2=pitch/M3=roll은 기구학적 장착 순서의 증거가 아니다.

### 3. 수정한 중간 경로 충돌 문제

원래는 최종 target만 안전검사하고 `forearm_calibration_step()` 출력은 검사하지 않았다.
다음 두 명령은 모두 [20,160] 범위이고 원래/현재 정적 안전검사를 통과한다.
순서: elbow_roll, elbow_pitch, wrist_pitch, wrist_roll.

```text
시작 = (90,90,20,20)   tip.z = -3.21394cm
목표 = (90,90,20,160)  tip.z = -3.21394cm
중간 = (90,90,20,90)   tip.z = -9.39693cm   (가정 테이블 -5cm 아래)
```

실제 원래 motion API를 400틱 실행하면 224틱에서 unsafe 명령을 출력했다.
수정 후에는 각 틱의 후보를 안전검사한 뒤에만 상태를 갱신한다. 거부되면 직전 출력과
trajectory 시간을 유지하고 `ForearmMotionState.blocked_flags`에 사유를 기록한다.
새 목표 설정 시 이 상태를 초기화한다. 다른 경로를 자동 탐색하지는 않는다.

동일 반례에서 unsafe 출력은 0틱, 가장 낮은 출력 tip.z=-4.96730cm였다.
테스트는 충돌 직전 유지뿐 아니라 안전한 새 목표로 다시 이동하는 것도 확인한다.
이것은 **20ms 표본 중심선 자세 검사**이다. 틱 사이 swept volume, 실제 서보 비동기 운동,
링크 두께, 추종 오차, 가속도/토크까지 보장하지 않는다.
최초 set_target은 여전히 스냅하므로, 통합 시 실측/승인한 시작 명령을 먼저 시드해야 한다.

### 4. 수정하지 않은 중요한 문제: unwrap 누적과 유한 서보

`forearm_motion_control_unwrap_target()`의 누적 값을 `map_target()`이 그대로 선형 변환한
뒤 clamp한다. 한 번 경계를 넘어 누적된 회전수 때문에 사람이 다시 도달 가능한 방향으로
돌아와도 서보가 한계에 붙는다. 단순 ±179 경계 연속성 테스트로는 발견되지 않았다.

실제 API 재현(pitch/wp/wr=0, 각 target은 valid):

| 입력 yaw | unwrap yaw | 최종 elbow_roll |
|---:|---:|---:|
| 0 | 0 | 90 |
| 100 | 100 | 160 |
| 170 | 170 | 160 |
| -170 | 190 | 160 |
| -100 | 260 | 160 |
| 0 | 360 | 160 |

마지막 입력 0에서 원래 중립 명령 90으로 복귀하지 않는다. 같은 문제가 주기적 손목 입력에도
가능하다. 실 replay 첫 승인 frame337의 raw yaw=57.468872는 임시 범위 내 방향인데,
unwrap=417.468872라 목표 160이 된다(회전수 없는 매핑이면 147.468872).
승인 170프레임의 yaw 목표가 전부 160인 것은 추종 성공 근거가 아니다.

수정 보류 이유: 무조건 wrap해 clamp하면 ±180 부근에서 두 끝단을 오갈 수 있다.
연속회전이 불가능한 서보에 대해 도달 불가 방향을 hold/reject할지, 재진입 때 어느
동치각을 선택할지 정책이 필요하다. 단순 부호 반전/필터 강화로 숨기지 않았다.
현 모듈은 이 문제가 해결되기 전 정상 추종 완료로 볼 수 없다.

또한 `calibrated`, `yaw_observable`, `hand_fresh`는 validator가 소비하지 않는다.
호스트 데모(calibrated=0)를 검산하는 것은 가능하지만, 실물 enable 차단, 초기 yaw 미관측,
장기 손 stale, 재진입의 명시적 소비 정책은 아직 완성되지 않았다. 통합은 스코프 밖이라
연결하지 않았다. 최초 스냅, 가속도 제한 없음도 유지했다.

### 5. Replay와 테이블 거부의 판정

실제 `forearm_mapping_update()`에 원본 CSV와 실제 timestamp dt를 넣었다.

| 항목 | 직접 재현 |
|---|---:|
| rows/fresh | 522/522 |
| invalid/major HOLD | 0/0 |
| yaw 미관측/hand HOLD | 1/9 |
| yaw 최소/최대 | -178.546692 / 179.479950도 |
| pitch 최소/최대 | -84.553047 / 16.484871도 |
| wrist pitch 최소/최대 | -81.178513 / -5.289444도 |
| wrist roll 최소/최대 | -179.479904 / 179.758606도 |
| A2 승인/거부 | 170/352 |
| 거부 사유 | 352개 모두 TABLE_COLLISION(0x4) |
| 손목 자체가 테이블 이하 / 손끝만 이하 | 349 / 3 |
| clamp 한계 프레임 yaw/pitch/wp/wr | 497 / 132 / 23 / 435 |

문서의 소수 둘째 자리 범위와 일치한다. 문서가 더 정밀한 수치를 제공하지 않으므로
비트 단위 동일성 주장은 하지 않는다. 출력값으로 독립 계산한 높이와 C FK 차이는
출력 반올림까지 포함해 최대 `3.79e-6cm`였다.

현재 전완 24cm, 테이블 -5cm에서는
`wrist.z=24 sin(pitch)`이므로 pitch가 약 -12.0247도 이하이면 손목부터 충돌한다.
거부 349개는 이 사실로 설명된다. 남은 3개는 손끝 때문이다.
수평 무한 평면 모델에서 yaw 부호는 높이에 영향을 주지 않아 FK 부호 수정 전후
170/352는 같다. 그렇다고 yaw 오류가 없었다는 뜻은 아니다.

기존 테스트는 A1 count만 assert하고 각도 범위/승인 수/속도는 출력만 했다.
또 reject 프레임에서는 step을 전혀 진행하지 않아 실제 50Hz 제어를 재현하지 못했다.
수정한 테스트는 다음을 확인한다:

- 문서의 각도 극값(0.01도 허용), 170/352 및 각 거부의 flags를 assert.
- CSV 시간과 독립된 20ms 틱, 거부 중에도 마지막 승인 목표 추종, 알려진 중립 시작.
- 4축 모두 범위/틱당 변화/각 출력의 정적 안전검사, 입력 종료 후 400틱 추가.
- 실제 1703틱, blocked=0, 최대 변화 0.598171도/20ms.
- 선택적 argv[2] CSV에 raw/unwrap/목표/높이/승인 사유 기록.

**판정:** 이 가정 모델 안에서의 거부 계산은 정상이다. 다만 데이터가 실제 새 기구와
안 맞는다고 확정할 실측 근거는 없으며, 추종 경로에는 별도의 unwrap 문제가 있다.

### 6. 미확정값과 기본값 평가

| 값/구조 | 현재 가정 | 검증 상태 |
|---|---|---|
| 팔꿈치→손목 / 손목→그리퍼 끝 | 24cm / 10cm | 배정 미확정. 새 구조 사진/실측으로 확인할 근거가 이번 검증에 없음 |
| 서보 중립 / 방향 / scale | 90도 / +1 / 1 | 미실측. 인간 팔의 일반 비율이나 기존 조립 관례로 확정할 수 없음 |
| 4축 기계적 가동범위 | 20..160도 | 기구 간섭/케이블/하중 기준으로 검증되지 않음 |
| 속도 제한 | 0.6도/20ms | 명령 제한 동작은 확인. 실물 토크/가속도 안전성은 미확정 |
| 팔꿈치 원점 높이 | 테이블 위 5cm | 미실측. -5cm가 보수적이라는 근거 없음 |
| 손목 장착 순서/축 오프셋 | roll 후 pitch, 중심 일치 | FK 모델만 검증, 물리 순서·축 간 거리 미확정 |
| 두께/그리퍼 형상/하중 | 중심선과 점만 사용 | 실물 충돌/하중 보호 검증 아님 |

예: pitch=-5도, 손목 일직선이면 tip.z=-2.9633cm로 현재 -5cm 검사에는 통과한다.
실제 팔꿈치가 테이블 위 2cm라면 테이블 z=-2cm이므로 이미 충돌한다.
따라서 TABLE_SURFACE_Z_CM=-5를 '확실히 보수적'이라 부를 수 없다.
상수를 임의로 다른 값으로 교체하지 않고, 근거 없는 보수성 주석만 바로잡았다.

독립 수치 민감도(현재 mapped 명령 재사용, 실제 설치 예측 아님):

| 전완/손 길이 | 테이블 0cm | -2cm | -5cm | -10cm |
|---|---:|---:|---:|---:|
| 24/10cm | 승인 162 | 165 | 170 | 174 |
| 10/24cm | 승인 36 | 72 | 167 | 173 |

현재 자기충돌의 팔꿈치-손 선분 2cm 검사는 24/10 배정에서 거리 하한이
24-10=14cm이므로 발동할 수 없다. wrist 내각 25도 검사는 실제로 동작하지만
현재 ±70도 clamp 안에서는 발동하지 않는다. 따라서 기존 self-collision PASS가
새 하우징/인접 브래킷의 간섭 부재를 입증하지는 않는다.

실측 후 `forearm_calibration_config.c`만 바꾸면 끝나는 구조도 아니다.
FK가 물리 servo zero=90/방향=+를 별도로 고정하므로, 실측 zero/direction을 반영할 때
명령 매핑과 물리 FK를 함께 맞춰야 한다. scale은 사용자 추종 배율일 수도 있어
calibration 수식을 무조건 역산해 물리 FK로 쓰는 방식도 채택하지 않았다.

### 7. 변경 내역과 테스트 평가

수정한 신규 파일:

- `src/robot_calibration/forearm_safety_check.c`: RH yaw 부호 수정, 임시 테이블 설명 정정.
- `include/robot_calibration/forearm_safety_check.h`: Table↔robot 축 대응 명시.
- `src/robot_calibration/forearm_calibration.c`: 틱 후보 안전검사, 거부 시 출력/시간 유지.
- `include/robot_calibration/forearm_calibration.h`: blocked_flags 및 시작/정지 계약 명시.
- `src/robot_calibration/forearm_calibration_config.c`: '안전값' 표현 정정만. 수치 미변경.
- 신규 테스트 3개: 독립 회전 합성, clamp 경계, 비유한값, 네 축 속도,
  테이블 접촉/손끝만 충돌, 안전 endpoint 사이 충돌/복구, 실제 시간 replay 추가.

원래 near 허용오차 0.002는 각도 검사에서 0.002도, FK에서 0.002cm로서 의미 있는
엄격도였다. 문제는 허용오차보다 기준값/커버리지였다. yaw +90 테스트가 원래 잘못된
부호를 정답으로 고정했고, 링크 길이는 그 반사를 검출할 수 없었다.
원래 자기충돌 유발 각은 내각 24도로 유효한 단위 반례지만 실운영 clamp 밖이며,
원래 테이블 테스트도 ±90도 고도만 검사해 접촉 경계/손끝 충돌을 놓쳤다.
기존 ramp는 yaw/pitch 위주로 확인했고 replay는 yaw 변화만 출력했다.

run_tests.py의 기존 9개 케이스/컴파일 명령은 그대로다. 새 3개를 포함한 최종 실행:

```powershell
cd D:\Working\zynq-cnn-motion-robot-soc\robot_arm
python tests/robot_calibration/run_tests.py
## PASS: 12 suites
```

최종 실행 파일 폴더: `C:\Users\kccistc\AppData\Local\Temp\robot-a2-tests-803y82ne`.
검증 당시 원본 사본/독립 probe/상세 replay CSV/최종 로그:
`C:\Users\kccistc\AppData\Local\Temp\forearm-crosscheck-0dv63i0h`.
probe는 표준 Python, ctypes, host GCC를 사용했고 보드나 UART에 연결하지 않았다.

커밋/푸시/보드 빌드/flash/실제 서보 구동 없음. 실물 안전·추종 정확도, 토크,
링크 배정과 손목 기구 순서, 새 A1→A2→A3 보드 배선은 검증하지 않았다.

---

## 6. Agent3 5축 인터페이스 변경 핸드오프 (2026-09-22)

원본: `agent3_forearm_handoff_prompt.md`. Agent2가 자기 파일 범위 안에서 구현한 새 5축 출력 타입(`ForearmJointCommand`)을 Agent3에 전달하기 위해 작성한 계약 제안 문서.


Agent1→Agent2 계약은 이미 확정돼 있다(`HumanForearmTarget`, Agent1이 정의·구현·
호스트 검증 완료: `docs/agent1_forearm.md`, `docs/agent2_forearm_handoff_prompt.md`).
이 문서는 그 다음 단계, **Agent2→Agent3 계약**을 정리한다. Agent2(Claude)가
자기 파일 범위 안에서 새 5축 출력 타입과 보정 로직을 이미 구현·검증했지만,
**Agent3 쪽은 아직 아무것도 바뀌지 않았고 Agent2도 Agent3 파일을 건드리지 않았다.**
지금 보드는 100% 기존 6축 legacy 경로로 동작한다 — 이 문서는 다음 단계를 위한
계약 제안이지, 지금 당장 Agent3가 뭔가 고쳐야 한다는 뜻이 아니다.

### 1. 지금 상태

- `src/integration/agent_pipeline.c`, `main_integration.c`: 안 바뀜. 여전히
  `agent1_stage_run()` → `robot_calibration_apply()`(legacy `JointCommand`) →
  Agent3로 흐른다.
- `include/output_controller/*`, `src/output_controller/*`, `src/drivers/*`:
  Agent2가 전혀 안 건드렸다. `SERVO_COUNT=6`, `JointCommand` 6필드 그대로다.
- Agent2가 새로 만든 것(`include/robot_calibration/forearm_*`,
  `src/robot_calibration/forearm_*`)은 지금 어디에서도 호출되지 않는
  독립 모듈이다 — 파이프라인 연결은 이번 스코프 밖(사용자 지시로 보류)이었다.

### 2. 기존 계약 (변경 없음, 참고용)

```c
/* include/output_controller/servo_config.h */
typedef enum {
    SERVO_BASE = 0, SERVO_SHOULDER, SERVO_ELBOW,
    SERVO_WRIST_PITCH, SERVO_WRIST_ROLL, SERVO_GRIPPER,
    SERVO_COUNT   /* = 6 */
} ServoChannel;

/* include/common/robot_types.h */
typedef struct {
    float base_deg, shoulder_deg, elbow_deg, wrist_pitch_deg, wrist_roll_deg;
    float gripper_norm;
    uint8_t valid;
} JointCommand;   /* robot_calibration -> output_controller, 필드 순서 = ServoChannel 순서 */
```

`servo_control.c`/`servo_config.c`/`output_control.c`는 이 6필드/6채널을 그대로
가정하고 짜여 있다(`servo_configs[SERVO_COUNT]` 배열 등). **이번 문서가 제안하는
변경 전까지는 그대로 둘 것.**

### 3. 새 Agent2 출력 계약 (제안)

```c
/* include/robot_calibration/forearm_motion_control.h (신규, Agent2 소유) */
typedef struct {
    float elbow_roll_deg;   /* M0 */
    float elbow_pitch_deg;  /* M1 */
    float wrist_pitch_deg;  /* M2 */
    float wrist_roll_deg;   /* M3 */
    float gripper_norm;     /* M4, 0=CLOSE, 1=OPEN */
    uint8_t valid;
} ForearmJointCommand;
```

- `include/common/robot_types.h`의 `JointCommand`와 **별개 타입**이다. 공유
  헤더는 건드리지 않았으므로 기존 legacy 빌드/Agent3에는 영향이 없다.
- 값은 전부 "보정 이후 서보 목표각(도)". 90=중립으로 가정했지만 **미실측**
  (아래 6절).
- 채널 수가 6→5로 줄었다: `base`+`shoulder` 2축이 `elbow_roll`+`elbow_pitch`
  2축으로 대체된 게 아니라, 예전 3관절(base/shoulder/elbow)이 2관절
  (elbow_roll/elbow_pitch)로 합쳐졌다. `wrist_pitch`/`wrist_roll`/`gripper`는
  이름은 같지만 **새 물리 관절이다**(전완 길이/방향 기준이 다름 — 예전
  wrist 캘리브레이션을 재사용하면 안 된다. `docs/agent1_forearm.md` 4절).

### 4. Agent3가 결정해야 할 것 (Agent2가 대신 정하지 않음)

1. **채널 표현**: `ServoChannel`/`SERVO_COUNT`를 5로 줄일지, 6을 유지하고
   1개(base 또는 shoulder에 해당하던 채널)를 미사용으로 둘지. PL(AXI/PWM IP)
   배선을 바꿀 수 있는지에 달려있다 — 이건 실물/Vivado 쪽 결정이라
   Agent2가 판단할 수 없다.
2. **M0~M4 실제 배선**: 어느 PWM 채널이 elbow_roll인지 elbow_pitch인지 등
   물리 배선 확인 필요.
3. **PWM 보정**: 새 관절 4개(elbow_roll/elbow_pitch/wrist_pitch/wrist_roll)는
   전부 새 서보거나 재배치된 서보라, `servo_configs[]`의 기존 us 범위를
   그대로 재사용해도 되는지 실측 확인 필요. 기존 실측 PWM calibration을
   임의로 덮어쓰지 말 것(Agent1 프롬프트에서도 명시).
4. **변환 함수**: 기존 `output_control.c`의 `JointCommand -> ServoPwmCommand`
   함수를 그대로 둔 채 `ForearmJointCommand -> ServoPwmCommand`를 새로
   추가할지(legacy 경로 보존, Agent1/Agent2가 해온 "새 파일 병행" 패턴과
   일관됨), 아니면 다른 방식으로 통합할지.
5. **trace/record 자료형**: `docs/agent1_forearm.md` 12절이 이미 지적한 대로
   trace/record-playback 채널 길이·저장 포맷도 5채널 기준으로 다시 봐야
   한다. 실제 record 소스가 있는지부터 확인할 것(존재하지 않는 소스를
   추측하지 말 것).

### 5. Agent2가 이번에 한 일 / 안 한 일 (재확인용)

했음: `forearm_calibration_config.c/.h`, `forearm_motion_control.c/.h`,
`forearm_safety_check.c/.h`, `forearm_calibration.c/.h` 신규 4쌍 +
`tests/robot_calibration/test_forearm_*` 3개 + `run_tests.py` 확장.
`python tests/robot_calibration/run_tests.py` 12개 스위트 PASS(기존 9개
legacy 무변경 회귀 포함).

안 했음: `agent_pipeline.c`/`main_integration.c` 연결, Agent3 파일 전부,
보드 빌드/실물 서보 구동.

### 6. 실측 전 미확정값 (Agent3 작업 전 필요)

- elbow_roll/elbow_pitch/wrist_pitch/wrist_roll: 실제 회전 방향, 90도가
  진짜 중립인지, 실제 가동범위(현재 `forearm_calibration_config.c`에
  scale=1,direction=1,offset=90,[20,160] 임시값).
- 링크 24cm(팔꿈치-손목)/10cm(손목-그리퍼) 배정: 사진 기반 추정, 확인 필요
  (`forearm_safety_check.c` 상수 2개).
- 팔꿈치 원점의 테이블면 대비 높이: 현재 `TABLE_SURFACE_Z_CM=-5`(보수적
  가정, 실측 아님).
- 손목 기구 순서(roll이 먼저 비트는지 pitch가 먼저 굽히는지).

### 7. Agent3 담당 Codex/작업자 전달용 프롬프트 (복사해서 사용)

```text
너는 이 C 로봇팔 프로젝트의 Agent3(Output Controller / PWM) 담당이다.
Agent1(HumanForearmTarget)과 Agent2(ForearmJointCommand, 아직 파이프라인
미연결)가 새 5축 수평 설치 구조로 이미 전환했다. 이 문서
(docs/agent3_forearm_handoff_prompt.md) 2~6절이 계약과 미확정값 목록이다.

먼저 읽을 파일:
- docs/agent3_forearm_handoff_prompt.md (이 문서)
- docs/agent1_forearm.md, docs/agent2_forearm_handoff_prompt.md
- include/robot_calibration/forearm_motion_control.h (ForearmJointCommand 정의)
- include/output_controller/servo_config.h, servo_control.h, output_control.h
- src/output_controller/*.c, src/drivers/servo_pwm_driver.c

지금 하지 말 것:
- 기존 legacy 6축 경로(SERVO_COUNT=6, JointCommand)를 깨거나 삭제하지 말 것 -
  보드는 아직 그 경로로만 동작한다.
- 실측 안 된 PWM 값을 임의로 만들지 말 것(위 6절 목록). 안전 승인/보드 flash도
  별도 지시 없이 하지 말 것.
- Agent1/Agent2 파일(include/human_target_angle/*, include/robot_calibration/
  forearm_*)을 고치지 말 것 - 인터페이스 문제를 발견하면 Agent2에 보고하고
  이 문서에 기록하되, 직접 바꾸지 말 것.

할 일:
- 4절의 5개 결정 사항에 대한 제안(채널 표현, M0~M4 배선, PWM 보정 범위,
  변환 함수 추가 방식, trace/record 자료형)을 문서로 정리해 보고하라.
- 가능하면 ForearmJointCommand -> ServoPwmCommand 변환 함수의 초안을 새
  파일로 작성하되(기존 output_control.c는 건드리지 않는다), 6절의 미확정
  값은 명시적으로 TODO/미보정으로 표시하고 실제 서보에 연결하지 마라.
- 결과를 이 문서 8절(신설)에 "Agent3 처리 결과"로 추가하라.
```

---

## 7. 2차 UART 로그·실물 사진 기반 코드 리뷰 (2026-09-23, Codex)

원본: `uart_review_20260922_234404.md`. 새 5축 경로가 실제로 연결된 뒤(`uart_debug_20260922_234404_521146.log`)의 두 번째 UART 로그 + 실물 사진 기반 리뷰. 보드에서 구버전 ELF가 돌고 있었을 가능성(P0), 사람→로봇 매핑 의미 미정, unwrap-clamp 고착 문제 등을 지적했다.


작성: 2026-09-23, Codex. 검토 HEAD: `d6c79cc40d0531bef2121e5a64c49c93e4803674`.

**결론: 손목 과굴곡은 스무딩보다 좌표/보정/unwrap 문제를 먼저 해결해야 한다.**
현재 소스는 1500µs에서 손목이 90° 굽혀진다는 사실을 FK에 반영했지만,
사람 각도를 서보로 바꾸는 보정은 여전히 모든 회전축에 `입력+90°`다.
로그에는 wrist pitch의 +360° 누적과 wrist roll의 -360° 누적이 실제로 나타나며,
두 손목 축이 서로 반대쪽 끝각에 붙어 있다. 또한 로그의 안전검사 결과는 현재 HEAD와 재현되지 않는다.
추가 사진에서는 **굽힘 뒤의 roll 축이 손/그리퍼 방향인 구조**로 보여, 전완 축 주위로 손 전체를 돌리는 현재 FK도 실물과 다를 가능성이 높다.

이번 작업은 분석이다. 소스·헤더·테스트를 수정하지 않았고 커밋, 푸시, 보드 빌드/flash, 실제 서보 구동도 하지 않았다.
사진을 받기 전의 해석보다 아래 사진 기반 해석을 우선한다.

**추가 사용자 질문: idle을 맞추느라 분해·재조립을 반복하는 문제**

회전 4축(M0~M3)은 현재 PWM 체계의 중앙인 **1500µs를 공통 중립 기준**으로 유지하는 것이 적절하다.
이것은 모든 기구에 유일하게 안전하거나 토크가 최소인 자세라는 뜻이 아니라, 보정의 기준점을 정하는 것이다.
기계적 간섭이 없고 필요한 가동범위를 확보했다면, 사진 모양이나 “물리적 90°”에 맞추기 위해 혼을 계속 분해할 필요는 없다.
현재 조립에서 1500µs에 해당하는 각 링크 방향을 기록하고 물리 중립·서보 방향·범위를 코드에 반영한다.
손목의 **1500µs=물리 굽힘 90°**는 그대로 사용한다. 집게는 별도이며 원하는 벌림과 기계적 끝점을 기준으로 정한다.
현재 pipeline의 gripper home=0.5→1500µs도 실측한 최적 집게 자세는 아니다.
사진의 실제 PWM은 여전히 확정하지 않았다. 사용자는 사진을 모든 축 idle로 만들려는 상황이라고 설명했다.

### 1. 자료와 검토 범위

- 로그: `C:\Users\kccistc\Downloads\uart_debug_20260922_234404_521146.log`
- 로그 SHA-256: `ccaa2a1780c636ccea89f630e09b165ccf60cbd5f0484b169f4ea85aa3da47b3`
- 추가 사진: 받침대 위에 전완이 서 있고, 상단 굽힘 관절 뒤로 손목 roll 서보와 그리퍼가 이어진 새 구조.
- 사용자 확인: **손목은 물리적으로 90° 굽힌 상태가 1500µs**.
- 검토 경로: UART → Agent1 2D/3D·BodyFrame·손목 계산 → Agent2 보정·unwrap·FK·안전검사·램프 → Agent3 PWM·HAL·드라이버 → 통합 루프·trace·관련 테스트/빌드 설정.
- 실제 사용 중인 ELF, Vitis의 linked source 경로, 보드 레지스터/전류/실제 관절각은 확인하지 못했다. CNN/FPGA RTL 전체 검증이나 모든 PC 시각화 도구의 실행 검증은 범위에 포함하지 않는다.

현재 `agent_pipeline.c`는 새 5축 경로를 사용한다. 이전 문서의 “아직 legacy 6축만 연결됨”이라는 안내는 현재 HEAD에 적용되지 않는다.
기존 `docs/claude_handoff_20260922.md`는 다른 로그·이전 HEAD에 대한 인계로, 이번 결과와 섞지 않았다.

### 2. 사진으로 다시 해석한 기구

사진상 관절 연결은 다음과 같이 보인다. 모터 이름은 현재 코드 채널명에 대응시킨 추정이며 실제 배선까지 사진으로 확정한 것은 아니다.

```text
M0 elbow_roll : 아래 받침대 쪽 방위 회전
  → M1 elbow_pitch : 아래 가로 회전축, 긴 전완을 기울임
    → 긴 전완 링크
      → M2 wrist_pitch : 상단 굽힘 관절
        → M3 wrist_roll : 굽힘 뒤 손/그리퍼 방향의 축을 비틂
          → M4 gripper
```

핵심은 **긴 전완을 따라 올라가는 축과, 굽힌 뒤 그리퍼를 향하는 축이 다르다**는 점이다.
사진처럼 전완과 손이 거의 직각일 때 이 두 축은 특히 명확히 구분된다.
사진이 모든 회전축 1500µs인 idle인지, 재생 중인지, 수동 자세인지는 작성 시점에 확인되지 않았다.
따라서 사진의 보이는 기울기를 PWM 각도나 특정 로그 프레임과 직접 대응시키지 않았다.

**현재 FK와의 차이 — 우선순위 높음**

위치: `src/robot_calibration/forearm_safety_check.c:149`, `:154`, `:155`.

현재 코드는 먼저 전완 방향 `F`와 굽힘 기준 `B`를 만들고:

```text
H_before = F cos(beta) + B sin(beta)
H        = rotate(H_before, axis=F, angle=wrist_roll - 90)
tip      = wrist + 10 H
```

즉 **roll이 전완 F 축을 중심으로 손끝의 위치까지 바꾼다.**
하지만 사진에서 보이는 roll이 굽힘 뒤 손의 길이 방향 축이라면, roll은 손/그리퍼의 자세를 비틀며
그 축 위의 그리퍼 중심선 끝점은 움직이지 않아야 한다. 집게 날개 등 축에서 벗어난 점은 회전하므로 별도 형상 모델이 필요하다.

실제 현재 C 함수를 실행한 반례:

| 서보 er/ep/wp/wr | wrist(cm) | 현재 FK tip(cm) |
|---|---|---|
| 90/90/90/90 | (0,0,24) | (0,10,24) |
| 90/90/90/180 | (0,0,24) | (-10,0,24) |

180° 행은 회전축 판별용 FK 단위 실험이며 구동 허용각 제안이 아니다.
roll을 바꿨더니 손 전체가 전완 주위를 돌고 있다. 사진상 distal roll 구조라면 이 동작은 맞지 않는다.

**필요 수정:** 실제 각 관절의 부모/자식 프레임과 회전축을 정한 뒤 직렬 변환을 작성한다.
사진 해석이 맞다면 손 중심선 방향은 굽힘으로 계산하고, 그 뒤 손 자체 축 회전을 그리퍼의 자세/형상에 적용한다.
브래킷·축 사이 오프셋도 필요한 정확도로 포함한다. “pitch 먼저 계산한 다음 roll을 적용했다”는 문장만으로 실제 기계 순서가 보장되지 않는다.
현재 독립 회전 테스트(`test_forearm_safety_check.c:85`)도 같은 전완축 roll 모델을 기준으로 삼으므로 실물 일치를 증명하지 않는다.

또한 A1의 `wrist_roll`은 **전완축 F 주위의 손 normal 회전**이다(`docs/agent1_forearm.md` 손목 절).
사진의 M3가 손축 H를 도는 경우, 같은 이름이라고 바로 전달해도 동일한 물리 회전이 되는 것은 아니다.
완전한 자세 복사를 원하는지, 굽힘/비틀림을 독립적인 조작 신호로 사용할지 정해야 한다.
고정된 전완 방향에서 distal roll만으로 손의 굽힘 평면을 자유롭게 바꿀 수는 없다.

### 3. 로그에서 직접 확인한 것

| 항목 | 결과 |
|---|---|
| A1 / A2 / P3 | 각 332개, fid 1~332 누락·중복 없음 |
| A1 결과 | 332개 모두 fresh/valid |
| A2 결과 | 신규 N 304개, 동일 S 28개, 거부 0개 |
| A2 안전 flags | 전부 0x0 |
| 제어 TK | 844개, tick 1192~2035 누락 없음 |
| TK 간격 | 19~21ms |
| wrist pitch 목표 | 152~160°, **298/332개가 160°** |
| wrist pitch 출력 | **157.1~160°, 2245~2277µs** |
| wrist roll 목표·출력 | **20° 고정**, 출력 **722µs 고정** |
| A1 wrist pitch | -177.6~160.8° |
| unwrap 후 wrist pitch | 62~274.2° |
| A1 wrist roll | -179.5~176.6° |
| unwrap 후 wrist roll | -358.1~-149.8° |
| SM의 UART CRC/format/range, overwrite, trace drop, tick overrun, servo error | 기록 구간 모두 0 |

현재 PWM 변환의 `90° → 1500µs` 자체는 맞다. 과굴곡 구간에는 **중립 PWM을 주는 것이 아니라 약 2250µs 이상을 명령**하고 있다.
TK의 값은 소프트웨어 명령과 쓰기 결과다. 서보의 실제 각도·토크 측정값은 아니다.

첫 입력 프레임의 계산 경로:

```text
A1 wrist_pitch = -144.3°
A2 unwrapped  =  215.7° (= -144.3 + 360)
서보 보정      =  305.7° (= 215.7 + 90)
clamp         =  160° → 2277µs

A1 wrist_roll =  126.5°
A2 unwrapped  = -233.5° (= 126.5 - 360)
서보 보정      = -143.5° (= -233.5 + 90)
clamp         =   20° → 722µs
```

입력과 unwrap 출력의 차이가 pitch +360°인 프레임은 261개, roll -360°인 프레임은 295개다.
이는 로그에 직접 드러나는 값이며 추측이 아니다.

**이 로그는 초기화 직후 기록이 아니다.** 첫 SM(24000ms)에 이미 입력 200개/승인 200개가 누적돼 있고,
첫 A1의 dt는 2365ms이며 파일 첫 TK부터 손목이 160°/20°다. BOOT 이벤트도 없다.
이전 동작에서 만들어진 unwrap·필터·목표 상태를 물려받은 구간이다.
332개 프레임만 초기 상태에서 재생해 실제 보드와 같은 상태가 나온다고 볼 수 없다.

### 4. 먼저 처리할 코드 문제

**P0 — 실행 중인 펌웨어와 현재 소스의 안전 판정이 불일치**

위치: `forearm_safety_check.c:167`, `agent_pipeline.c:135`.
현재 HEAD는 wrist pitch 서보값을 굽힘량으로 보고 내각 `180 - bend < 25°`, 즉 이 범위에서 `wp > 155°`를 거부한다.

로그의 반올림된 명령을 실제 C 함수에 다시 넣은 결과:

| 입력 집합 | 현재 d6c79cc 안전검사 | 직전 681b33b 안전검사 |
|---|---|---|
| A2 목표 332개 | 승인 10 / 자기충돌 거부 **322** | 승인 **332** / 거부 0 |
| TK 출력 844개 | 자기충돌 거부 **844** | 승인 **844** / 거부 0 |

A2 로그의 unwrap 입력을 현재 `forearm_calibration_apply()`로 처리해도 승인 10 / 거부 322다.
직전 커밋의 safety 소스는 임시 디렉터리에 추출해 별도 빌드했다. 작업 트리 파일을 되돌리지 않았다.

이 결과는 **이전 빌드가 보드에 남아 있을 가능성을 강하게 뒷받침**한다.
다만 로그에 build ID가 없어 681b33b ELF라고 특정할 수는 없다. 다른 checkout/linked source, 부분 갱신, 별도 변경된 펌웨어도 가능하다.
커밋 시각 22:46과 로그 파일명 23:44만으로 최신 ELF가 실행됐다고 판단하면 안 된다.

필요 조치: Vitis가 실제 참조하는 source와 ELF 경로/빌드 시각을 확인하고, clean build와 다운로드 대상을 일치시킨다.
BOOT 및 주기적 schema에 git SHA·기구 모델·보정 버전을 남긴다. 이번에는 보드에서 이 작업을 실행하지 않았다.
최신 소스를 적용해도 아래 mapping 문제가 해결되는 것은 아니며, 과굴곡 대신 거부/이전 목표 유지가 나타날 수 있다.

**P1 — 90° 굽힘 중립은 FK에만 반영됐고, 사람→로봇 매핑 의미가 정리되지 않음**

위치: `forearm_calibration_config.c:30`, `forearm_motion_control.c:9`, `forearm_safety_check.c:138`, `pose_hand.c:134`.

- A1 wrist pitch: 전완과 손이 일직선이면 0°. 부호는 투영된 Finger1→Finger2 축 기준이다.
- A2 보정: `servo = human_pitch + 90`.
- 현재 FK: `physical_bend = servo`라는 모델. 따라서 servo 90은 물리 굽힘 90°.

| 사람 pitch | 보정·clamp 후 서보 | 현재 FK가 가정하는 굽힘 |
|---|---|---|
| 0° | 90° / 1500µs | 90° |
| +30° | 120° | 120° |
| +90° | 160° | 160° — 현재 안전검사는 거부 |
| -90° | 20° | 20° |

절대 굽힘을 복사하려는 목적이면 사람 0°→로봇 90°는 불일치다.
반대로 사람 중립을 로봇의 안정된 90° 굽힘 idle에 대응시키는 **상대 조작**이 목적이면 +90 자체가 틀렸다고 할 수 없다.
이 경우에도 입력 기준 자세, 부호, gain, 허용 굽힘 범위를 명시해야 한다.

수정 설계에서는 다음 두 변환을 분리한다:

```text
물리 모델: beta = beta_idle + s * (servo - 90)
사람→목표: beta_desired = g(human_pose, reference_pose)
역보정:    servo = 90 + (beta_desired - beta_idle) / s
```

`beta_idle`의 굽힘 크기 90°는 사용자 확인값이다. 굽힘 면/부호와 `s`는 별도 확인 대상이다.
현재 코드처럼 beta_idle=+90, s=+1, 절대 목표 beta_desired=human_pitch를 택하면 산술상 offset은 0이지만,
**이것을 실측 없이 바로 적용하면 안 된다.** A1의 부호·범위·기구의 가동범위가 아직 맞지 않는다.
PWM 중앙값 1500µs를 바꾸어 상위 보정 문제를 덮을 이유는 없다.

**P1 — 무한 회전 unwrap을 유한 가동범위 서보에 직접 clamp**

위치: `forearm_motion_control.c:32`, `:93`, `agent_pipeline.c:132`.

현재 unwrap은 최단 각도 차이를 누적하므로 입력이 한 바퀴 돌아오면 0° 대신 360°를 유지한다.
그 값을 +90 후 [20,160]에 clamp하면 원래 입력으로 돌아와도 서보가 중앙으로 돌아오지 않는다.

실제 C 함수로 재현한 wrist roll 입력 순서:

```text
입력:     0, 100, 170, -170, -100,   0
unwrap:   0, 100, 170,  190,  260, 360
서보:    90, 160, 160,  160,  160, 160
```

이 실험은 모든 단계가 현재 safety 검사에 승인된다. 충돌검사로 해결되는 문제도 아니다.
필요 수정: 인간 각도 표현의 wrap 처리와 로봇이 실제 갈 수 있는 관절 분기를 분리한다.
동치각/기준 자세·제한각 안의 목표 선택, 도달 불가능할 때의 hold/제한 정책을 정한다.
매 프레임 무조건 wrap180만 하여 ±180 경계에서 반대쪽 끝각으로 튀게 만드는 수정도 피한다.
새 세션/장기 중단 후에는 A1 필터와 A2 unwrap의 재기준 정책을 함께 적용하되 현재 서보 명령을 갑자기 home으로 초기화하지 않는다.
안전검사에 거부된 입력도 지금은 unwrap 이력을 갱신한다는 점을 테스트해야 한다.

**P1 — wrist 상한 160°와 안전검사 상한 155°가 충돌**

위치: `forearm_calibration_config.c:31`, `forearm_safety_check.c:167`, `agent_pipeline.c:134`.
보정은 [20,160]을 허용하지만 wp 155° 초과는 전체 명령 거부다.
따라서 손목 한 축의 과도한 목표가 팔꿈치 등 다른 축의 새 목표까지 막고, 마지막 승인 목표로 계속 이동하게 한다.
전체 명령 거부 자체는 일관된 정책이지만 “손목만 제한하며 나머지 축 추종”과는 다른 동작이다.
실측 굽힘 범위와 실제 충돌 여유로 보정 한계를 정하고, 그 안에서도 기하 검사를 별도로 유지한다.
단순히 자기충돌 검사를 끄거나 155를 임의로 올리는 해결책은 근거가 없다.

**P1 — elbow pitch에도 수평 고도각과 수직 idle의 기준 차이**

위치: `forearm_mapping.c:34`, `forearm_calibration_config.c:28`, `forearm_safety_check.c:136`, `:149`.

A1은 Body XZ 수평면에서 전완이 올라간 고도각이며 수평 0°, 위쪽 90°다.
반면 현재 FK는 elbow pitch 서보 90°에서 전완이 수직이다. 보정은 여전히 A1+90이다.
그러므로 A1 수평 0°→로봇 수직, A1 위쪽 90°→clamp 160°→모델상 수평에서 약 20° 위가 된다.
사진은 전완이 서 있는 조립 형태와는 부합하지만, 사진의 PWM 상태가 미확정이므로 중립 각도를 추가 확정하지는 못한다.
절대 방향 복사라면 설치 프레임·중립·부호를 포함해 변환을 고쳐야 한다.
상대 조작이라면 이를 명시해야 하며 현재 숫자를 “사람과 같은 고도각”이라고 해석하면 안 된다.

### 5. A1 입력도 별도로 점검해야 함

손목 과굴곡을 A2 offset 하나로 모두 설명할 수 없다.
P3의 전완 `F = wrist-elbow`, 손 방향 `H = finger_midpoint-wrist`를 직접 계산하면:

- F와 H의 부호 없는 사이각: **62.89~141.10°**.
- **251/332개에서 90° 초과**, 첫 프레임 약 **138.14°**.

즉 로그에 복원된 3D 손 방향부터 이미 전완 반대쪽으로 상당히 접힌 프레임이 많다.
이 숫자는 필터링된 A1 signed wrist pitch와 동일한 정의/시점의 값은 아니므로 정확히 같아야 하는 것은 아니다.
실제 사람이 그렇게 굽혔다는 증거도 아니다. 같은 프레임의 사람 영상과 2D/3D 손 점을 대조해야 한다.

위치: `pose_reconstruction.c:594`의 손가락 ray/sphere 해 선택, `pose_hand.c:99`의 손 방향과 `:114`의 pitch 축.
단안 depth의 앞/뒤 해, 손가락 라벨/손 중심, 이전 세션의 필터 이력, hand plane 품질을 확인한다.
raw hand pitch, hand_fresh, 선택한 depth branch를 trace에 추가하면 구분하기 쉽다.
임의 abs() 또는 부호 반전만으로 큰 각도를 감추지 않는다.

### 6. 전체 제어 경로에서 추가로 발견한 사항

**입력 무수신 timeout이 통합 계층에 없음.**
`main_integration.c:27`은 새 pose가 있을 때만 A1을 호출한다.
A1의 0.35초 HOLD timeout은 호출에 전달된 dt로 진행되므로 UART가 아예 멈추는 동안에는 진행되지 않는다.
제어 tick은 기존 목표를 계속 수행한다. Agent1 문서도 무수신 timeout을 통합 계층 책임으로 명시한다.
실시간 마지막 수신/유효 관측 시각을 tick에서 검사하고, 목표 추종 중단·현재 명령 유지·재개 정책을 정해야 한다.
중복 frame ID도 A1 내부에서는 age를 늘리지 않으므로 외부 watchdog이 필요하다.
무조건 전원을 끄는 것을 정책으로 가정하지 않는다. 중력 하중이 있는 기구에서는 별도 판단이 필요하다.

**손만 미관측일 때 기본/오래된 값을 사용할 수 있음.**
`forearm_mapping.c:146`은 손을 못 얻으면 이전 wrist/gripper를 유지하고, 최초에는 0/0/OPEN을 넣는다.
`forearm_motion_control.c:44` 및 통합은 hand_fresh를 별도로 처리하지 않는다.
major fresh와 hand fresh를 구분하여 초기 손 미관측 시 임의 OPEN으로 바꾸지 않도록 하고,
장기 손 누락의 처리와 복귀 시 램프를 정의해야 한다. 이번 로그 schema만으로 모든 프레임의 hand_fresh를 확인할 수 없다.

**속도 제한은 있고, 가속도 연속성은 없음.**
`forearm_calibration.c:69`, `:95`, `:124`: 정지 출발은 smoothstep, 이동 중 재목표는 선형이다.
회전축은 0.6°/20ms, 명목 30°/s 제한이다. 실제 TK 최대 변화도 elbow 두 축 0.6°, wrist pitch 0.4°, wrist roll 0°였다.
현재 이상은 “제한 없이 손목을 급격히 꺾는 계산”보다는 잘못된 끝각 목표를 계속 주는 상태다.
기준각·축·unwrap을 맞춘 후에도 방향 전환 충격이 남으면 이전 속도를 보존하는 가속도 제한을 검토한다.
gripper는 램프 대상에서 제외돼 0/1 변경이 바로 500/2500µs로 전달된다.
`platform_tick_due()`는 밀린 tick을 몰아서 출력하지 않고 버리는 방식이며 이번 로그의 overrun은 0이었다.

**중간 경로 차단 사유가 TK에 없음.**
`forearm_calibration.c:134`는 매 tick 후보의 안전검사를 하고 unsafe이면 기존 명령/시간을 유지한다. 이 로직은 확인했다.
하지만 `trace.c:503`의 TK에는 `motion.blocked_flags`가 없다.
목표 A2는 승인됐는데 중간에서 멈출 때 쓰기 성공만 보일 수 있다. TK에 차단 이유, 보정 전 포화 여부,
hand_fresh·관측 age·session/build ID를 추가하는 것이 좋다.

**부팅 home 검사가 한계각만 확인함.**
`agent_pipeline.c:53`은 home이 clamp 안인지 확인하고, 초기 set_target은 현재 명령을 home으로 바로 시드한다.
현재 home은 현재 모델에서 안전하지만, 기구/FK 변경 때는 home 자체의 기하 안전검사를 추가해야 한다.
`servo_pwm_driver.c:201`의 실물 init은 FPGA 출력을 disable하지 않는다. mock만 레지스터를 0으로 초기화한다.
따라서 soft restart 때 FPGA가 이미 enabled이면 “home을 쓴 뒤 enable”이 실제 비활성 상태에서 시작된다는 보장이 없다.
시작 시 실제 자세/출력 상태 정책과 mock·실물 초기화 가정을 맞춰야 한다. 이번 로그에는 BOOT가 없어 부팅 동작은 판정하지 못했다.

### 7. FK 수학과 실제 안전성은 구분

현재 FK의 수학 자체는 직교 단위 프레임과 Rodrigues 회전으로 구성된다.
`F = U cos(p)+D sin(p)`, `B = D cos(p)-U sin(p)`에서 U와 D가 직교 단위벡터이므로
F/B도 단위 길이이고 서로 수직이다. H의 길이도 보존된다.
임의 각도 10,000개를 실제 C FK에 넣어 링크 길이를 따로 검사한 최대 오차는
24cm 링크 **2.59×10^-6cm 미만**, 10cm 링크 **4.82×10^-6cm 미만**이었다.
수학적으로 정상인 회전식이어도 회전축/링크 배정이 실물과 다르면 충돌검사는 틀린 위치를 검사한다.

현재 24/10cm와 elbow pitch [20,160] 모델에서는:

```text
wrist.z >= 24 cos(70°) ≈ 8.2085cm
tip.z   >= wrist.z - 10 >= -1.7915cm > table(-5cm)
```

따라서 **clamp 안에서는 테이블 충돌이 발생할 수 없다.** 위 부등식은 정확한 최솟값을 찾는 격자 탐색이 아니라 모든 손 방향에 성립하는 하한이다.
테이블 충돌 테스트는 elbow pitch 170° 또는 -13° 등 clamp 밖의 직접 명령으로 기능을 검사한다.
그 테스트가 통과했다고 정상 파이프라인 영역에서 실물 충돌을 막는다는 뜻은 아니다.
또한 원점에서 24cm 떨어진 손목으로부터 길이 10cm인 손 구간은 원점에 최소 14cm보다 가까워질 수 없어,
현재 2cm base-clearance 검사도 이 링크 배정에서는 발동하지 않는다. 정상 clamp 범위의 주요 거부는 wrist 내각 제한이다.

현재 코드 주석은 테이블을 팔꿈치 원점보다 5cm 아래라고 “사용자 확인”으로 기록한다.
이번 사진에는 로봇 받침·그 아래 장비·책상이 있으므로 **어느 표면을 장애물로 정의했는지** 다시 구분해야 한다.
사진만으로 원점 높이 5cm를 재측정하거나 다른 수치로 대체하지 않았다.
테이블을 실제보다 너무 낮게 놓으면 충돌을 놓치고, 높게 놓으면 과도하게 거부한다.
링크/서보 케이스/브래킷 두께와 받침대 형상은 현재 중심선 모델에 없으며 토크·전류·열 보호도 없다.

추가로 확인할 실측/계약:

| 항목 | 현재 확인 수준 |
|---|---|
| wrist 1500µs에서 굽힘 크기 90° | 사용자 확인 |
| 사진이 모두 1500µs인 idle인가 | 작성 시 미확인 |
| wrist 증가 방향, 굽힘 면, straight servo 값 | 미확정. 90° 한 점만으로 servo 0°=straight까지 증명되지 않음 |
| elbow pitch 90°=수직 | 현재 소스에 사용자 확인으로 기록됨. 이번 사진의 PWM 상태는 미확인 |
| roll이 distal 손축인지 | 추가 사진상 강하게 시사됨. 축/부모-자식 연결 확인 필요 |
| 24cm=elbow→wrist, 10cm=wrist→tip | 코드의 가정. 사진의 원근/스케일로 확정 불가 |
| 측정 끝점 | 각 회전축 중심과 그리퍼 기준점이어야 함. 은색 봉만의 길이와 링크 길이는 다름 |
| servo 방향·gain·[20,160] 가동범위 | 새 기구에서 미실측 임시값 |
| table z=-5cm, 받침대/장비 높이 | 코드 가정 기록은 있으나 이번 사진으로 확인 불가 |

### 8. 실행한 검증과 발견한 테스트 문제

컴파일러: `C:\msys64\ucrt64\bin\gcc.exe`. 호스트 임시 디렉터리에 빌드했고 GCC DLL 검색 경로를 맞췄다.

| 검증 | 결과 |
|---|---|
| `python tests/robot_calibration/run_tests.py` | **12/12 PASS**, C99 `-Wall -Wextra -Wpedantic -Werror` |
| A1 `test_forearm.c` + 실제 UART 522 frame replay | **PASS**, C99 strict |
| A1 `test_agent1_trace.c`, `-DROBOT_TRACE` | **PASS**, 두 getter compile/link/run |
| A3 `test_servo_hal.c` | **PASS**, 제공 runner와 같은 C11 strict |
| A3 `test_servo_control.c` | **PASS**, C11 strict |
| A3 `test_output_control.c` | **컴파일 실패**, 구형 `JointCommand*`를 `ForearmJointCommand*` API에 전달 |
| Xilinx define의 test translation-unit syntax 검사 | servo_hal PASS / output_control 같은 타입 오류로 실패 |
| 실제 A2 C 코드에 이번 로그 명령 입력 | 현재/직전 안전검사 불일치 재현 |
| 유한 범위 unwrap 복귀 사례 | 현재 코드의 끝각 고착 재현 |
| 실제 C FK 임의각 10,000개 길이 검사 | PASS, 위 수치 참고 |

참고: A3 테스트에 처음 C99 strict를 적용하면 `_Static_assert` 때문에 실패한다. A3 제공 스크립트는 C11이며,
그에 맞춰 다시 실행했다. **output_control의 타입 오류는 C11에서도 발생**한다. BSP/보드 빌드가 성공했다는 뜻으로 해석하면 안 된다.

Agent3 실패 위치는 `tests/output_controller/test_output_control.c:8`, `:19`, `:36` 등이다.
내용도 “Agent2 interface pending / legacy rejection” 테스트여서 현재 5축 API와 맞지 않는다.
필요 수정: 새 타입의 정상 변환·invalid/NaN 시 출력 불변·실패 시 HAL 미호출을 검증하도록 갱신하고 통합 runner에 포함한다.

현재 CMake 전체 경로는 삭제된 `kinematics_2d.c`, `motion_record.c`, `robot_mode.c`를 여전히 참조한다(`CMakeLists.txt:118`, `:131`).
이번 환경 PATH에서 CMake를 찾지 못해 configure/build를 실행하지는 않았다. 파일 참조가 깨진 것은 소스/파일 목록으로 확인했다.
호스트 12개 PASS를 전체 CMake·Vitis 빌드 PASS로 확대해서 보고하면 안 된다.

개별 테스트의 `near()` 오차 0.002, clamp 경계, 155/156° 자기충돌, 손끝만 테이블에 닿는 사례는 산술 회귀로 의미 있다.
하지만 `human 0→servo 90`을 기대값으로 둔 테스트는 그 매핑이 실물에 적합하다는 검증이 아니다.
현재 522 CSV replay는 BodyFrame 개정 후 수치와 승인 **480 / 거부 42(자기충돌)**를 검사한다.
통합 binary replay는 승인 **481 / 거부 41**이었다. 이전 TableFrame 문서의 170/352와는 모델/입력 경로가 달라 같은 기준으로 비교하면 안 된다.
통합 binary replay에서도 elbow roll은 472/522, wrist roll은 494/522 프레임이 끝각 포화인데 테스트는 PASS한다.
안전 범위/쓰기만이 아니라 기준 자세 복귀와 추종 오차·포화율도 검사해야 한다.

임시 재현 자료:
`C:\Users\kccistc\AppData\Local\Temp\robot-wrist-review-1bwkkjul`
(`stats.txt`, `probe.py`, `probe_results.txt`, 테스트 실행파일).
12-suite 산출물: `C:\Users\kccistc\AppData\Local\Temp\robot-a2-tests-qwsj6z2c`.

### 9. 내일 수정·검증할 순서

1. **빌드 식별부터 해결:** 현재 ELF/source 경로와 HEAD를 맞추고 build/calibration ID가 찍히는 시작 로그를 확보한다.
2. **사진의 기구 축을 확정:** M2 굽힘 뒤 M3가 어떤 축을 도는지, 사진의 PWM 상태, 링크 측정 끝점을 정리한다.
3. **공통 기준표 작성:** 불필요한 재조립을 멈추고 현재 조립에서 각 모터의 1500µs 물리 자세, 증가 방향, 실제 범위와 A1 입력 기준을 표로 만든다. 절대 모사/상대 조작 목표를 명시한다.
4. **A2 보정과 FK를 함께 수정:** 특히 wrist 90° 물리 굽힘, elbow 수직 idle, distal roll을 같은 계약에 맞춘다. A1 wrist roll의 의미와 맞지 않는 부분은 별도 변환/제한 정책으로 다룬다.
5. **unwrap·세션·미관측 정책 수정:** 끝각에서 돌아오는 입력, ±180 경계 왕복, 장기 중단/재생 재시작, 손 누락을 호스트 테스트로 검증한다.
6. **기준 자세 회귀 추가:** 알려진 물리 자세→명령→FK 일치, 손축 roll에서 중심선 불변, 90° 손목 중립, 허용 범위 복귀, 목표 및 중간 안전 판정 일관성을 검사한다.
7. **A1 손 방향 검증:** 이번 영상의 실제 손과 P3의 큰 굽힘이 일치하는지 확인한다. 필요하면 depth branch/손 자세 추정 문제를 별도로 수정한다.
8. **그 뒤 부드러움 조정:** 정상 목표로도 남는 덜컥거림을 보고 가속도 제한·gain·필터를 조정한다. 기존 0.6°/tick을 더 낮추는 것만으로 잘못된 목표는 고쳐지지 않는다.

확인된 것은 로그 포화·unwrap 이력·현재/직전 안전검사 결과·현재 코드의 변환식과 테스트 상태다.
사진으로 새롭게 드러난 distal roll 축 문제는 우선 확인할 기구 불일치이며, 실측값이나 정확한 실행 ELF로 확정한 사항과 구분했다.
이번에는 이 보고서만 추가하고 발견된 소스/테스트 문제는 수정하지 않았다.
