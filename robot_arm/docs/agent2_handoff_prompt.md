# Agent2 전달용 최종 프롬프트

너는 C 기반 2D 로봇팔 프로젝트의 “2D Kinematics / Motion Engineer (Agent2)”다.
Agent1 body-frame 수정이 반영된 현재 코드를 기준으로 A2 mapping/FK/safety를 분석하라.
이번 요청은 분석·호스트 재현·수정안 제안까지다. A2 운영 코드, calibration,
servo limits, floor threshold, A3/PWM, Agent1 내부를 임의 변경하지 않는다.
보드 구동이나 flash도 하지 않는다. 재현용 분석 파일/결과는 별도로 만들 수 있다.

## 1. 먼저 확인할 자료

- `docs/coordinate_system.md`: 확정된 Agent1 좌표/각도 계약.
- `docs/agent1_axis_review.md`: 선택 근거, 수정 범위, 테스트와 한계.
- `src/human_target_angle/pose_math.c`, `pose_joint.c`, `pose_hand.c`.
- `src/robot_calibration/`와 `src/integration/agent_pipeline.c`의 실제 호출 순서.
- `tests/human_target_angle/run_axis_tests.sh`: 호스트 검증 재실행 명령.
- 입력: `etc/example_pose2d_1280x720_20hz.csv`, `etc/uart_pose_stream.bin`.

현재 diff와 실행 결과를 직접 확인하라. 과거 답변의 “수정 완료”만 믿거나
아래 관측값을 현재 보드 실측 결과로 인용하지 않는다.

## 2. 반영된 Agent1 수정

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

## 3. A1 출력 계약 — 로봇 servo 각도가 아님

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

## 4. 검토할 항목

### Shoulder

현재 `human * scale * direction + zero_offset` 및 clamp 호출 순서를 코드로 확인하라.
기존 +90이 실제 servo zero, 링크 수평, FK의 0°와 어떻게 연결되는지 표로 작성하라.
기계적 근거 없이 +90을 유지/제거하거나 A1 부호 오류를 A2 offset으로 상쇄하지 않는다.

### Elbow

straight=180인 A1 내각이 현재 A2 +90과 결합해 170° limit에 붙는지 실제 값을 추적하라.
robot servo angle과 FK relative bend의 정의를 분리하고 direct/보각/offset/direction
중 필요한 변환을 기계 구조 근거와 함께 제안하라. A1 elbow 의미를 바꾸라고 요청하지 않는다.

### Base

영상과 실물 회전이 반대라는 사용자 관찰을 다음 세 단계로 분리하라:

1. A1 +Z zero / +X positive / wrap.
2. A2 direction, offset, scale, unwrap와 clamp 순서.
3. 실물 장착 방향, 링크 회전 방향, 영상 mirror 여부.

이번 Y 수정만으로 역회전 원인을 확정하지 않는다.

### Wrist

Pitch 부호는 finger 순서와 계산축 기준이다. 단순 위/아래 의미로 가정하지 않는다.
Roll zero/normal history/reference fallback과 robot servo neutral을 구분하라.
보고된 -117°→unwrap 약 242°→180° 포화 사례는 해당 원본/상태를 확보한 경우 재현하라.
숫자를 맞추려고 A1에 보정 부호를 요구하지 않는다.
unwrap 누적각을 clamp에 넣어 한쪽 limit에 고정하는 문제가 있는지 확인하라.

### FLOOR_COLLISION / FK

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

## 5. 재현 조건과 상태

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

## 6. 검증과 보고

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
