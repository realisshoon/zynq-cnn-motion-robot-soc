# Agent1 손목 수정 및 Agent2 보정 분석

## 범위와 결론

2026-09-23. example2의 332개 좌표 프레임을 수정 전/후 동일하게 replay했다.
손목 기하의 명확한 오류와 부모 이동에 따른 필터 지연을 수정했다.
다만 실제 손목각 ground truth가 없으며, **example2의 손목이 거의 고정되어야 한다는
요구를 완전히 충족하지는 못했다.** 단안 깊이 후보 모호성과 손 랜드마크 정확도는 남는다.
아래 감소 수치는 정확도 증명이 아니라 출력 안정성 지표다.
기존 사용자 변경인 CMakeLists.txt와 test_output_control.c는 수정하지 않았다.

## 수정

- 손가락 sphere root 선택과 EMA의 이전 위치를 현재 손목으로 평행이동한 뒤 사용한다.
  이전 absolute fingertip과 현재 wrist 사이의 부모 이동/깊이 지연이 손 방향으로
  잘못 들어가는 것을 막는다. 회전이나 실제 깊이를 알아내는 기능은 아니다.
- f=normalize(W-E), s=normalize(F2-F1), a=normalize(s-dot(s,f)f).
  a 정규화 전에 투영 길이 0.15 미만을 거부한다. caller는 손목값을 HOLD하고
  hand_fresh=0을 내보낸다. 임의 Body 축으로 굽힘축을 대체하지 않는다.
- Pitch는 기존 signed atan2(a·(f×h),f·h), h=normalize((F1+F2)/2-W)를 유지한다.
  midpoint 자체가 오류라는 근거는 없다. 시간 연속 Pitch에 unwrap 및 35도/frame
  estimator correction 제한을 추가했다. 이는 이상 깊이를 복구하지 않으며 빠른
  실제 동작도 지연시킨다. 로봇 속도 제한이 아니다.
- Roll은 기존 project(h×s, f)의 법선 대신 f×a를 사용한다.
  기존 방식은 90도 굽힘에서 투영 길이가 0이고 90도를 넘으면 방향이 반전한다.
  새 방식은 손가락 가로축이 전완에 평행하지 않으면 굽힘 90도에서도 정의된다.
  reference r은 기존 전완-local reference 그대로다.
  roll=atan2(f·(r×n),r·n), n=normalize(f×a).
- 이전 normal과 EMA 후 현재 전완 수직면으로 재투영한다.
  기존 normal sign continuity, roll unwrap/zero calibration은 유지했다.
  따라서 매우 빠른 반회전과 첫 프레임의 깊이 모호성은 여전히 구분할 수 없다.
- Elbow 각도와 Body/major reconstruction은 변경하지 않았다.

## example2 결과

| 지표 | 수정 전 | 수정 후 |
|---|---:|---:|
| hand_fresh 프레임 | 332/332 | 330/332 |
| Pitch 최대 프레임 변화 | 71.02° | 38.99° |
| Pitch 누적 절대 변화 | 425.88° | 442.32° |
| Roll 최대 프레임 변화 | 11.65° | 12.92° |
| Roll 누적 절대 변화 | 579.36° | 326.72° |

변화는 wrap180(next-prev)로 계산했다. ±180 표기 점프를 그대로 더하지 않았다.
Pitch는 큰 한 번의 점프만 줄었으며 누적 변화는 증가했다. Roll도 최대 step은 증가했다.
손목 전체가 안정화됐다고 해석하면 안 된다. Elbow 출력은 전 프레임 동일하다.
원본 CSV는 calibration_results/example2_before.csv와 example2_after.csv.

## 사진 4장: 수정 후 실제 Agent1 출력

기존 extract_real_person_pose_v3.py로 오른팔, 1280×720 좌표를 추출한 CSV를 사용.
각 사진마다 새 context로 첫 프레임 한 번 처리했다. 로봇각/idle에 맞춰 입력을
조정하지 않았다. 각도 출력은 필터를 포함한 public target이며 첫 프레임에는
이전 프레임 EMA/영점 보정이 없다. 4장 모두 hand_fresh=1.

| 자세 | 사람 ER | 사람 EP | 사람 WP | 사람 WR | 로봇 ER/EP/WP/WR |
|---|---:|---:|---:|---:|---|
| Idle | 38.400055 | 60.539146 | -74.371315 | -75.420486 | 70/110/100/87 |
| Straight | 112.061127 | 50.366566 | -24.579849 | -97.745689 | 70/45/0(추정)/87 |
| Elbow Roll | 149.220734 | 16.862156 | -5.087173 | -149.233963 | 110/30/30/87 |
| Wrist Roll | 110.154816 | 32.780769 | -46.016068 | -72.483292 | 70/45/40/160 |

Idle과 Wrist Roll 사진의 새 사람 WR 차이는 2.94°뿐인데 로봇 목표는 73° 차이다.
이 두 점만 맞추면 기울기 약 24.85가 되어 노이즈를 크게 증폭한다.
이것은 정상적인 calibration gain으로 채택할 수 없다. 손가락 두 점의 단안 깊이로
손 회전을 충분히 관측하고 있지 못할 가능성을 보여준다. 검출 성공 != 실제 3D 정확도.

## Idle와 Agent2 calibration

Idle는 사용자 지정 로봇 홈 자세와 대응할 사람 기준 자세다. Agent1의 물리적
각도 정의를 홈 서보 숫자로 변경하거나 매 부팅 첫 프레임을 자동 영점으로 삼지 않는다.
보정용 자세를 명시적으로 취하고 안정된 여러 프레임에서 유효한 사람 기준각 H0를 수집한다.
elbow_roll_observable와 hand_fresh를 확인하고 circular unwrap 후 기준을 정한다.
독립 사진의 초기 depth branch와 연속 영상의 branch는 다를 수 있다.

관절별 기본 모델:

    R = R0 + k * (H_unwrapped - H0)
    scale = abs(k), direction = sign(k)
    zero_offset_deg = R0 - k * H0

Pitch elevation은 wrap 대상이 아니며, 나머지는 reference와 같은 branch로 unwrap한다.
현재 Agent2의 단순 offset에 wrapped angle을 넣는 것만으로 ±180 문제는 해결되지 않는다.
회전 이력을 유지한 unwrap을 사용하고 Idle reference와 branch가 일치하도록 해야 한다.

Idle를 고정하고 여러 표본으로 최소제곱 기울기를 구하면:

    k = sum((Hi-H0)*(Ri-R0)) / sum((Hi-H0)^2)

사진 4장을 동등 가중치로 계산한 진단 결과(배포값 아님):

| 축 | k | 최대 목표 오차 |
|---|---:|---:|
| ER | 0.19395 | 18.51° |
| EP | 2.14247 | 43.21° |
| WP | -1.42641 | 28.98° |
| WR | 0.03600 | 72.89° |

따라서 이 데이터로 계수를 자동 적용하지 않았다. Straight WP=0은 추정값이므로
실제 보정 fitting에서는 낮은 가중치를 주거나 실측 전 제외해야 한다.
허용각/clamp, 충돌 모델까지 통과한 최종 로봇 출력에서도 오차를 확인해야 한다.
관절별 LUT를 만들어도 동일/비슷한 사람각에 서로 다른 로봇각을 요구하는 문제는 남는다.

## 검증과 다음 확인

- 기존 forearm geometry/wrist/temporal/pipeline 테스트 통과.
- 새 테스트: 89/90/91/120도 굽힘에서 Roll 유지, 손가락축 평행시 거부,
  부모 손목이 깊이 1 unit 이동해도 fingertip relative depth 유지.
- run_axis_tests.sh의 body frame, pose mapping, CSV, visual, UART suite 통과.
- 보드 실물 테스트는 수행하지 않았다. context 크기가 바뀌므로 Vitis 전체 Clean Build 필요.

남은 큰 변화는 필터 강도만 늘리거나 로봇 gain을 0으로 만들어 숨기지 않는다.
정확도를 개선하려면 example2의 고정 손목 구간을 명시하고 검출점 overlay와 비교해야 한다.
손 전체 landmark/손등 방향 단서, 카메라 보정 또는 깊이 측정 없이 두 손가락 2D만으로
모든 자세의 3D twist를 유일하게 결정할 수는 없다. 입력 정보를 확장하는 작업은 별도 범위다.
