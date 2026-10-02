# 스테레오 절대 좌표 → 5축 사람 목표 각도

## 변경 이유와 범위

2026-10-02 점검에서 기존 `forearm_mapping_update_stereo()`가 실측 좌표를
어깨 폭으로 나눈 뒤, 어깨선에서 만든 BodyFrame으로 각도를 계산하는 것을 확인했다.
실측 depth를 입력받더라도 몸통 회전·어깨 검출에 출력이 종속되는 상태였다.
사용자 요청에 따라 **스테레오 경로만 고정 카메라 좌표계 기준**으로 변경했다.
단안 재구성·몸통 기준축·가상 Z 경로와 Agent2 보정/방향, Agent3 PWM은 변경하지 않았다.

## 입력 좌표 계약

- `HumanPose3D`는 캘리브레이션된 **왼쪽 카메라 원점 기준 mm 실측값**이다.
- X는 카메라 오른쪽, Y는 위쪽, Z는 카메라 전방이다. 광학 좌표의 아래쪽 Y는
  스테레오 입력 생산자가 위쪽으로 변환하며, Agent1은 다시 반전하지 않는다.
- 팔꿈치·손목은 valid, finite, Z > 0이어야 한다. 같은 frame_id의 유효한 이미지
  포즈와 새로운 팔꿈치·손목 2D 검출도 필요하다.
- 어깨는 선택 사항이다. 미검출, 서로 겹침, 회전, 위치/폭 변화가 각도나 gripper를
  바꾸지 않는다. 비정상 어깨값은 진단 저장값에서 invalid로 정리한다.
- `pose.elbow_3d`, `wrist_3d`, 관측된 손가락 3D 좌표는 입력 mm값을 그대로 보관한다.
  어깨 폭 정규화, 상대 링크 길이, 가상 Z 재구성을 호출하지 않는다.
- 위치 자체는 정규화하지 않는다. 각도 계산 중 방향 벡터를 단위벡터로 만드는
  수학 연산은 거리 단위를 없애는 입력 정규화와 다르다.

여기서 절대 기준은 **고정된 왼쪽 카메라 리그**다. 중력 좌표나 로봇 베이스 좌표로
등록했다는 뜻은 아니다. 외부 로봇-카메라 회전/이동 보정은 별도로 필요하며,
사람 어깨를 이용해서 그 변환을 추정하지 않는다.

## 각도 정의

`f = wrist_mm - elbow_mm`, `horizontal = hypot(f.x, f.z)`일 때:

```text
elbow_roll_raw  = wrap180(degrees(atan2(f.x, f.z)))
elbow_pitch_raw = degrees(atan2(f.y, horizontal))
```

즉 azimuth의 0도는 리그 +Z, +90도는 +X이며, elevation은 리그 XZ 평면의
위쪽 +Y가 양수다. 관절 이름은 기존 5축 API를 유지하며 해부학적 팔꿈치 내각이 아니다.
수평 방향이 소실되는 수직 자세에서는 기존 enter/leave hysteresis를 적용하고,
마지막 관측 azimuth를 유지한다. 처음부터 수직이면 yaw는 0으로 초기화하되
`elbow_roll_observable=0`이며 손목 roll의 임의 기준을 관측값으로 취급하지 않는다.

손목은 기존 엄지(finger1)→검지(finger2) 정의와 연속 필터를 유지한다.
`f_hat`은 팔꿈치→손목 단위벡터, `a`는 그 축에 수직으로 투영한 엄지→검지
단위벡터, `h`는 손목→두 손가락 중점 단위벡터다.

```text
wrist_pitch_raw = degrees(atan2(dot(a, cross(f_hat, h)), dot(f_hat, h)))
azimuth         = 마지막 관측된 raw elbow_roll
elevation       = 현재 raw elbow_pitch
heading         = (sin(azimuth), 0, cos(azimuth))
reference       = normalize(project_perpendicular(
                    (0, 1, 0) * cos(elevation) - heading * sin(elevation), f_hat))
normal          = normalize(cross(f_hat, a))
wrist_roll_raw  = degrees(atan2(dot(f_hat, cross(reference, normal)),
                               dot(reference, normal)))
```

위 식의 각도는 sin/cos 입력에서 radian으로 변환한다. 실제 구현은 기존 hand
normal 연속성/EMA, ±180 unwrap, spike 억제, 선택적 roll-zero 보정과 출력 필터를
유지한다. `reference`만 고정 리그의 up/heading에서 계산하며 BodyFrame을 사용하지 않는다.
손가락이 없거나 손 평면이 퇴화하면 팔꿈치 목표는 계속 갱신하고 마지막 손목/gripper를
유지한다. 손목 관측 이력이 없으면 `wrist_valid=0`으로 Agent2가 기존 명령을 유지한다.

gripper의 기존 2D 손가락 간격/손 길이 비율, median/filter 및 OPEN/CLOSE 정의는
유지한다. 손 크기 검사에 사용하는 2D 기준 길이만 **어깨 폭 대신 팔꿈치→손목
화면 길이(최소 기존 80 px)**로 바꿔 어깨 검출 의존성을 없앴다. 새 3D 손가락
관측이 없으면 gripper도 기존 스테레오 경로처럼 HOLD한다.

## 상태와 안전 경계

중복 프레임은 재필터링/시간 aging 없이 HOLD한다. 팔꿈치/손목 실패는 기존
0.35초 HOLD 후 invalid 정책을 유지한다. 단안/스테레오 또는 팔 선택이 바뀌면
각도·손목 기준 이력을 초기화한다. 이 모듈은 노출 동기화 증명, depth 범위,
길이/품질 제한, 실험 모드 허용 여부를 결정하지 않는다. 생산자/통합부가 승인한
measured pose만 소비하며 PWM을 켜거나 Agent2 제약을 우회하지 않는다.

## 검증

전용 `tests/human_target_angle/test_forearm_stereo_absolute.c`는 고정 리그 각도,
손목 부호, mm 저장, 어깨 변화/누락 독립성, depth 변화와 병진 불변성, yaw 연속성과
수직 자세, 손가락 HOLD, NaN/Infinity/퇴화/overflow 거부, 중복/실패 HOLD,
stage 연결 및 단안 모드 복귀를 검사한다.

빌드 소스는 `src/human_target_angle/pose_mapping.c`, `pose_math.c`,
`pose_tracking.c`, `pose_reconstruction.c`, `pose_joint.c`, `pose_hand.c`,
`forearm_mapping.c`, `agent1_forearm_stage.c`와 전용 테스트다.
호스트 GCC 옵션은 `-std=c99 -Wall -Wextra -Wpedantic -Werror -Iinclude -Iconfig -lm`이다.
기존 `test_forearm.c`(+ `src/uart_pose/uart_pose_protocol.c`)도 함께 실행한다.
이 수학/회귀 검증은 실측 손 추적 정확도나 로봇 좌표 등록을 증명하지 않는다.
