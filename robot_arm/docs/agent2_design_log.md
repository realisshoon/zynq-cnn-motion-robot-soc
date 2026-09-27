# Agent 2 설계 결정 로그

`robot_calibration`(Agent 2) 작업을 Claude Code와 Codex가 나눠서 진행하면서 남기는 공유 결정 로그입니다.
서로 모르는 상태에서 판단이 필요한 설계 결정(모호한 스펙, 예외 케이스, 하드웨어에 대한 가정 등)을
내릴 때는 조용히 혼자 결정하지 말고 이 파일에 기록합니다. 포맷:

```
## <날짜> - <짧은 제목>
- 작성자: Claude | Codex
- 쟁점: 무엇이 애매했는가
- 결정: 무엇으로 정했는가
- 근거: 왜 그렇게 정했는가
- 사용자 확인 필요: yes/no
```

최신 항목이 맨 아래에 추가됩니다.

---

## 2026-09-16 - FK 중립 자세(neutral-pose) 관례 (safety_check 작업에서 이어짐)
- 작성자: Codex
- 쟁점: 조립된 로봇팔에서 서보 각도 90도가 물리적으로 무엇을 의미하는가?
- 결정: 90도는 "이 링크가 이전 링크와 일직선으로 이어짐"을 의미한다고 가정했습니다.
  shoulder_deg는 +X축 기준 절대각, elbow_deg/wrist_pitch_deg는 이전 링크 기준 상대적인 굽힘 각도
  `(servo_angle - 90)`으로 처리합니다.
- 근거: `robot_calibration_config.c`의 모든 `zero_offset_deg` 값이 90인데, 이는 문서화되어 있진
  않지만 "중립"을 나타내는 자연스러운 신호입니다.
- 사용자 확인 필요: yes — 아직 아무도 실제 조립된 로봇팔에서 이 가정을 검증하지 않았습니다.

## 2026-09-16 - 자기 충돌(self-collision) 여유 임계값
- 작성자: Codex
- 쟁점: (CAD나 매뉴얼이 없는 상태에서) 자기 충돌 기준으로 "너무 가깝다"는 어느 정도인가?
- 결정: `BASE_CLEARANCE_CM = 2.0`, `LINK_CLEARANCE_CM = 1.0`, `MIN_JOINT_INTERIOR_DEG = 25`.
- 근거: 보수적인 임시값이며, `safety_check.c`에도 "하드웨어 실측 전까지의 임시값"이라고
  명시해뒀습니다.
- 사용자 확인 필요: yes — 실제 링크 두께/모터 하우징 치수를 측정해서 교체해야 합니다.

## 2026-09-16 - robot_calibration_apply()의 위험 명령 처리 방식
- 작성자: Claude
- 쟁점: `safety_check_apply()`가 문제를 보고하면 `robot_calibration_apply()`는 해당 명령을
  어떻게 처리해야 하는가?
- 결정: 0을 반환하고 `output->valid = 0`으로 설정합니다. 명령을 "안전한" 자세로 수정/clamp하지
  않고 그냥 무효로 표시만 합니다. 즉 호출자는 이 명령 대신 마지막으로 알려진 안전한 `JointCommand`를
  계속 유지해야 한다는 암묵적 계약입니다.
- 근거: "가장 가까운 안전 자세"를 계산하는 복구 알고리즘을 만드는 건 이번 작업 범위를 벗어나므로,
  대신 기본값으로 fail-safe(위험 쪽으로 움직이지 않음)를 택했습니다.
- 사용자 확인 필요: yes.

## 2026-09-16 - motion_limits의 관절 배열 순서 규칙
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

## 2026-09-16 - Motion Smoothing 방식: smoothstep + 시간 늘리기 vs. 사다리꼴 프로파일
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

## 2026-09-16 - 잘못된 motion-limit 입력값과 틱 수 오버플로 처리
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

## 2026-09-17 - 코드 주석은 한글로 작성 (팀 컨벤션)
- 작성자: Claude (사용자 지시)
- 쟁점: 지금까지 Claude와 Codex가 작성한 주석이 대부분 영어였음.
- 결정: 앞으로 이 저장소의 모든 새 코드 주석은 한글로 작성한다. 기존 영어 주석은 해당 파일을
  다른 이유로 수정할 때 같이 한글로 바꾼다.
- 근거: 사용자의 명시적 지시.
- Codex 확인 필요: no — Codex도 다음 라운드부터 이 컨벤션을 따라주세요.

## 2026-09-17 - gripper를 속도제한/동기화/스무딩 대상에서 완전히 제외
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

## 2026-09-16 - 동기화된 델타 값은 부호 없는 속도 크기(magnitude)
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

## 2026-09-17 - (해결됨) `motion_limits_synchronize()`의 미사용 `scaled_delta_per_tick` 제거
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

## 2026-09-17 - (해결됨, 4번 재작업) `motion_limits_synchronize()`를 아예 제거하고 관절 개수 상수 중복 자체를 없앰
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

## 2026-09-17 - (보류로 결정, 수정 안 함) 코드 리뷰 2·3번 항목 -- `set_target()`/`clamp_value()` 방어적 검증 누락
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

## 2026-09-17 - Agent1 wrap 각도(-180~180) shortest-angle unwrap 처리 추가
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

## 2026-09-18 - Codex 읽기 전용 확인: 120/154/180/229줄 4개 항목
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

## 2026-09-19 - 통합 glue(main + wrapper) 추가와 호스트 스모크 결과
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

## 2026-09-19 - 폴더 구조 정리: 팀 레포 하나로 통합
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

## 2026-09-22 - 실제 수평축과 차렷 idle 기준 Agent2 보정
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
