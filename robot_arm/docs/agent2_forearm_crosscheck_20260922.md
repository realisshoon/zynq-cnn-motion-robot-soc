# Agent2 forearm 독립 검증 — 2026-09-22

결론: Claude의 12개 테스트 PASS, A1 replay 통계, A2 170 승인/352 거부는 재현했다.
그러나 이를 새 기구의 정상 추종/실물 안전 검증으로 해석할 수 없다. FK yaw 부호 오류와
중간 램프 충돌 누락을 수정했다. 유한 서보 범위에서의 unwrap 복귀 문제와 실측/소비 정책은
미해결이다. 보드 연결·구동 준비 완료 판정이 아니다.

검증 기준 HEAD는 `400d830`. 검증 시작 당시 tracked 변경은 `run_tests.py` 하나,
새 forearm 헤더 4개/소스 4개/테스트 3개는 untracked였다. 기존
`claude_handoff_20260922.md`는 별도로 존재했고 건드리지 않았다.

## 1. 주장별 판정

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

## 2. FK 재유도와 부호 수정

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

## 3. 수정한 중간 경로 충돌 문제

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

## 4. 수정하지 않은 중요한 문제: unwrap 누적과 유한 서보

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

## 5. Replay와 테이블 거부의 판정

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

## 6. 미확정값과 기본값 평가

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

## 7. 변경 내역과 테스트 평가

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
# PASS: 12 suites
```

최종 실행 파일 폴더: `C:\Users\kccistc\AppData\Local\Temp\robot-a2-tests-803y82ne`.
검증 당시 원본 사본/독립 probe/상세 replay CSV/최종 로그:
`C:\Users\kccistc\AppData\Local\Temp\forearm-crosscheck-0dv63i0h`.
probe는 표준 Python, ctypes, host GCC를 사용했고 보드나 UART에 연결하지 않았다.

커밋/푸시/보드 빌드/flash/실제 서보 구동 없음. 실물 안전·추종 정확도, 토크,
링크 배정과 손목 기구 순서, 새 A1→A2→A3 보드 배선은 검증하지 않았다.
