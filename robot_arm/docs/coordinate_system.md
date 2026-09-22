# Coordinate System / Agent1 → Agent2 계약

## 입력 영상과 복원 좌표

- 영상 pixel: +X 오른쪽, +Y 아래쪽.
- Agent1 relative camera 3D: +X 영상 오른쪽, +Y 영상 위쪽, +Z 카메라에서 멀어지는 방향.
- `pose_reconstruction.c`의 ray는 `((px-cx)/fx, -(py-cy)/fy, 1)`을 정규화한다.
- 단안 복원은 상대적 추정이다. 실제 로봇의 길이·바닥 높이를 이 좌표에서 직접 가져오지 않는다.

## Body frame: 확정 규약

`U=(0,1,0)`이라 할 때:

```text
X = normalize(shoulder_r - shoulder_l)
Y = normalize(U - dot(U,X)*X)
Z = normalize(X × Y)
Y = normalize(Z × X)    // 수치 직교화, 부호 보정 아님
```

Left/Right는 해부학적 라벨이다. 화면 좌우 순서로 재정렬하지 않는다.
초기 frame은 raw X, 후속 frame은 기존 시간 필터가 적용된 X를 사용한다.
필터 때문에 현재 관측 X와 안정화된 X는 일시적으로 다를 수 있다.

유효한 frame은 `dot(Y,U)>0`, 단위 길이, 서로 직교, `X×Y=Z`를 만족한다.
여기서 오른손 규약은 코드의 cross product에 대한 규약이다.
Z를 camera +Z 쪽으로 강제하지 않는다. X의 해부학적 방향, Y의 up 의미,
오른손 규약을 고정하면 Z 부호는 이미 결정된다. Z는 두 어깨와 camera up에서
구성한 축이며, 관측하지 않은 몸통의 실제 전방이라고 단정하지 않는다.

기존 코드의 `Z=-Z; Y=Z×X`는 Y까지 아래로 바꿨다.
이 수정은 출력 shoulder에 마이너스를 곱하는 보정이 아니다.

## 퇴화와 연속성

어깨가 겹치거나 비유한 값이면 frame 생성 실패다.
정규화된 X에 대해 projected-up 길이가 `0.01` 미만이면 raw/filtered 생성 모두 실패한다
(`PM_BODY_UP_MIN_PROJECTION`, 수직축 주변 약 0.57°).
이 값은 수치적으로 방향을 정하기 어려운 영역을 제외하는 Agent1 기준이며 실측 보정값이 아니다.

실패 시 기존 stable frame은 덮어쓰지 않고 major 계산이 실패를 상위 API에 전달한다.
`pose_mapping_update`는 마지막 target을 `PM_TARGET_HOLD_SEC`(현재 0.35초)까지 HOLD하고
그 뒤 invalid로 보낸다. 정상 이력이 없는 시작은 즉시 invalid다.
이전 Y를 새 X에 투영하여 새 관측처럼 계속 출력하는 방식은 채택하지 않았다.
그 방식은 camera-up projection이라는 정의를 바꾸거나 불확실한 자세를 계속 유효하게 만들 수 있다.

수직 pole을 관통하면 X를 즉시 추종하면서 projected-up Y와 전 구간 연속성을 동시에
보장할 수 없다. 이 영역의 복구 시 각도 변화를 없앴다고 주장하지 않는다.
일반적인 정면→측면→반대편 회전 및 화면 어깨 순서 변화는 회귀 테스트로 확인한다.
기존 normalized-X EMA의 정확한 반대 방향 입력 정체, 단안 depth 분기,
수직 상완에서 base가 정의되지 않는 문제는 별도 한계다.

## HumanJointTarget 의미

모든 각도는 degree다. 로봇 servo command나 PWM 값이 아니다.
`wrap180`은 [-180,+180]이며 두 끝점 모두 가능하다. 차이는 최단각으로 비교한다.

| 필드 | 0 / 양의 방향 / 범위 |
| --- | --- |
| `base_deg` | 상완의 Body XZ 투영에 대해 `atan2(ux,uz)`. Body +Z=0°, +X=+90°, -X=-90°. [-180,+180]. 화면 오른쪽 또는 실물 servo 정회전이라는 뜻은 아니다. 수직 상완은 방위각 불확정. |
| `shoulder_deg` | `atan2(uy,sqrt(ux²+uz²))`. Body XZ 평면=0°, +Y 쪽=양수, -Y 쪽=음수. [-90,+90]. 어깨가 기울면 Body 수평과 영상 수평은 다를 수 있다. |
| `elbow_deg` | 팔꿈치→어깨와 팔꿈치→손목의 내각. 완전히 편 팔=180°, 완전히 접힌 이상적 자세=0°. [0,180]. A2 편의를 위한 +90 또는 보각 변환을 A1에서 하지 않는다. |
| `wrist_pitch_deg` | 아래의 F,H,A 정의로 `atan2(dot(A,F×H),dot(F,H))`. F와 H가 일치하면 0°, A축 오른손 방향이 양수. [-180,+180]. 단순 영상 위/아래 부호가 아니다. |
| `wrist_roll_deg` | 아래의 R,N 정의로 `atan2(dot(F,R×N),dot(R,N))`. R=N이면 raw 0°, F축 오른손 방향이 양수. zero calibration 적용 시 offset을 뺀 뒤 필터링. [-180,+180]. |
| `gripper_norm` | 0=CLOSE, 1=OPEN. 현재 Agent1은 hysteresis를 적용한 이진 의도를 출력한다. 물리 접촉 압력이나 관절각이 아니다. |
| `valid` | 사용 가능한 major target이라는 뜻. 손 좌표가 없으면 wrist/gripper는 이전값 또는 초기값일 수 있다. 모든 관절의 최신 관측을 보장하지 않는다. |

손목의 정확한 정의:

- F = normalize(wrist − elbow), H = normalize((finger1+finger2)/2 − wrist).
- S = normalize(finger2 − finger1). Finger 라벨 순서가 부호에 영향을 준다.
- Pitch axis A = normalize(S를 F에 수직 투영). 실패 시 normalize(Body Z × F).
- Hand normal N = normalize(H × S)를 F에 수직 투영·정규화한 값.
  이후 이전 normal과 반대면 부호를 맞추고 EMA를 적용한다.
- Roll reference R = Body Y를 F에 수직 투영·정규화.
  투영 품질이 부족하면 Body X, 그다음 Body Z를 같은 방식으로 시도한다.
- Normal의 부호 이력, reference fallback, spike 제한, unwrap, EMA 때문에
  roll 출력 전체를 일정 부호/180° offset 하나로 변환할 수 없다.
  Y→X reference 전환 및 급격한 손 회전의 연속성은 이번 축 수정으로 보장되지 않는다.
- 사용자가 roll zero calibration을 수행했다면 새 frame 규약으로 다시 수행한다.
  `pose_mapping_reset()`은 roll zero와 모든 A1 history를 초기화한다.

## 재현과 Agent2 경계

주어진 반올림 P3 첫 frame의 계산 결과:

```text
SL=( 0.298,0.750,7.253), SR=(-0.585,0.750,6.783), E=(-0.754,0.015,6.766)
X=(-0.882740,0,-0.469862)
Y=(0,1,0)
Z=(0.469862,0,-0.882740)
shoulder: +76.9878° (기존) → -76.9878° (수정)
base: 112.2811° (수정)
```

Agent2는 위 human convention을 기계적 zero/direction/scale에 연결한다.
PWM 실측 범위, servo 장착 방향, FK 관절각 정의는 서로 다른 정보다.
Agent2가 elbow +90 offset, base 방향, floor 정의를 독립적으로 검토해야 한다.
이 문서는 A2 calibration/안전검사 또는 A3/PWM 변경을 포함하지 않는다.

과거의 순수 2D 제어 모드(Shoulder/Elbow/Wrist pitch/Gripper 가변,
Base/Roll neutral 고정)와 달리 현재 Agent1 API는 Base/Roll도 계산한다.
Robot 2D FK 좌표의 수평/+Z 위쪽 규약과 실제 바닥 원점은 Agent2에서 확인한다.
