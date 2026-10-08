# 스테레오 3D One Euro 기본 필터 (2026-10-04)

이 문서는 A/One Euro 채택 당시의 이력이다. 2026-10-08 승열3은 운영 G/칼만과
로봇0 RIGHT·로봇1 LEFT 독립 제어를 정상 소스에 통합했다. 현행 계약은
[승열3 릴리스](seungyeol3_release.md), [Unity UART](uart_protocol_unity.md)를 따른다.

One Euro를 운영 소스에 통합했다. RIGHT 역할의 일반 Vitis 빌드에서도 적용되며,
외부 하네스의 소스 복사/주입 훅이 필요하지 않다. LEFT는 CNN 좌표 송신만 한다.

## 처리 순서

```text
CNN 원시 2D → 점 품질 검사 → 카메라별 2D EMA → 좌우 매칭/삼각측량
→ 원본 3D 기하·시간·JUMP150 승인/재획득 → 3D One Euro
→ 필터 출력 기하 검사 → A1 각도 계산/기존 EMA → A2 안전/20 ms 궤적 → PWM
```

필터는 팔꿈치·손목·빨강·초록의 XYZ 전체를 mm 단위로 처리한다. 어깨는 필터 대상이
아니며 몸통 좌표계 생성에도 사용하지 않는다. 2D One Euro는 추가하지 않았다.
기존 2D EMA(τ=0.10 s), 손 평면/각도 EMA, 서보 보정과 안전검사는 유지한다.

`input_pose_cnn_take_stereo()`가 승인된 3D 복사본에 한 번 적용한다.
`s_measured`의 원본 값은 수정하지 않으므로 다음 JUMP150의 비교 기준이 필터 지연에
따라 달라지지 않는다. 필터 출력도 depth 300..3000 mm, 전완 80..600 mm를 검사한다.
손가락은 손목과 250 mm 이내, 두 마커 사이 10..250 mm를 검사하며 실패 시 손가락
3D만 무효화한다. 한 영상의 2D 손목/빨강/초록으로 계산하는 독립 gripper는 유지한다.
필터 출력의 필수점 검사가 실패하면 새 A1 목표를 전달하지 않으며 admission reason은
`FILTER_GEOMETRY`다. 마지막 승인된 로봇 궤적/출력을 자동 취소하거나 PWM을 해제하지 않는다.

## 계산과 초기값

- 최소 cutoff: **0.5 Hz**.
- 속도 적응 계수 beta: **0.001/mm**.
- 미분 cutoff: **1 Hz**.
- 같은 점의 XYZ는 필터링된 속도 벡터의 크기로 계산한 동일 alpha를 사용한다.

```text
v = (현재 원본 위치 - 직전 필터 위치) / dt
v_filtered = alpha_d * v + (1 - alpha_d) * 직전 v_filtered
cutoff = 0.5 Hz + 0.001/mm * norm(v_filtered)
alpha = dt / (dt + 1 / (2*pi*cutoff))
position_filtered = alpha * position_raw + (1 - alpha) * position_previous
```

미분의 alpha도 같은 식을 쓰되 cutoff=1 Hz다. 2026-10-04 A안은 기존 최소 1 Hz,
beta 0.01/mm보다 강한 평활화를 시험하기 위해 위 기본값을 사용한다. 정지 시
흔들림 억제, 빠른 이동 시 지연 감소를 목표로
하지만 실측 정확도 향상이나 좌우 노출 동기화를 보증하지 않는다.

## 이력과 시간

- 첫 입력은 원본 좌표로 초기화한다.
- 새 좌우 입력의 나이는 각각 **250 ms 이하**, 수신 시각 차이는 **100 ms 이하**여야 한다.
  이 신선도 검사는 승인 기준과 별개이며 publish와 take에서 검사한다.
- 2026-10-04 사용자 요청으로 운영 펌웨어 전체를 외부 G 제작 당시 원본으로 복원했다.
  원본은 `captures/acg_build_20261004_acg_final/source/`이며 운영 A는 One Euro를 유지한다.
- 같은 세션에서 마지막 원본 승인 위치 대비 elbow/wrist 이동 ≤150 mm이면 승인한다.
  세션 변경 또는 150 mm 초과 시 첫 후보의 50 mm 이내인 서로 다른 3개 쌍으로 재획득한다.
  시간만 지났다고 재획득을 강제하지 않으며, 이동 예측 기반 재획득은 적용되지 않는다.
- 비동기 dt는 입력 승인 간격을 사용한다. 같은 세션·재획득 아님·간격 ≤250 ms이면
  실제 승인 간격을 전달하고, 그 외에는 0.1초를 전달한다. 250 ms 초과 승인 공백은
  filter epoch를 초기화한다. 마지막 실제 필터 갱신 시각과 승인 시각은 분리하지 않는다.
- 무효점은 해당 필터를 초기화하며, 필수점 출력 기하 탈락은 네 필터를 초기화한다.
  손가락 출력 기하 탈락은 손가락 두 필터를 초기화한다. 이전 좌표를 새 유효점으로 출력하지 않는다.
- 각 점의 필터 자체에는 기존 500 ms 초과 초기화가 있지만, 이는 최근 적용했다가 되돌린
  500 ms 승인 공백·이력 보존 정책과 다르다. A1·A2·150 mm·기하 안전검사는 유지한다.

복원 전 소스·테스트·문서는 `captures/restore_G_baseline_20261004/before/`에 보관한다.
SD의 기존 A BOOT은 소스 복원만으로 바뀌지 않는다. 실제 적용은 새 BOOT 설치 후 재부팅해야 한다.
회귀 테스트는 `test_stereo_pose_filter`, `test_stereo_reacquisition`, `test_stereo_filter_epoch`을 사용한다.

## UART 계수 조정

새 BOOT를 설치한 RIGHT에서는 모니터의 `r filter show`, `r filter default`,
`r filter 2d ema tau 0.15`, `r filter 3d min 0.5`, `r filter 3d beta 0.001`,
`r filter 3d derivative 1.0`을 사용한다. 2D tau의 단위는 초이고,
One Euro cutoff는 Hz, beta는 /mm다. 2D EMA는 기본 tau=0.10 s를 유지한다.

호스트는 `~F,SHOW\r`, `~F,DEFAULT\r`, `~F,EMA,<microseconds>\r`,
`~F,MIN,<milliHz>\r`, `~F,BETA,<millionths/mm>\r`,
`~F,DERIVATIVE,<milliHz>\r`으로 변환한다. 허용 범위는 tau 0.001..1 s,
cutoff 0.01..10 Hz, beta 0..0.1/mm다. 소수 입력은 정수 스케일로 정확히
표현할 수 있어야 한다. 형식/범위 오류, LEFT/MONO 요청, 열린 색상/카메라 메뉴에서의
요청은 설정을 변경하지 않는다. 프레임 내용은 기존 E/A/P 등 단일 키로 실행하지 않는다.
종료 문자는 CR이며 프레임 내부 LF는 오류로 처리하고 CR까지 나머지 내용을 버린다.
CR 뒤의 선택적인 LF만 무시한다.

성공 응답 `[FILTER] result=APPLIED`와 실제 설정을 확인한다. 변경은 기존 위치/속도
이력을 유지하고 다음 신규 샘플부터 적용한다. 이미 처리된 큐 항목을 재계산하지 않는다.
세션 재획득과 3D filter epoch 초기화에도 조정값을 유지하며, 재부팅/앱 초기화에서
빌드 기본값으로 복귀한다. SD 저장은 하지 않는다. `default`도 필터 이력을 지우지 않는다.
PWM/비동기 활성화, 마지막 승인 궤적, JUMP/안전검사 및 노출 동기화 계약은 변경하지 않는다.
강한 평활화는 추종 지연을 늘릴 수 있으므로 정지와 동작 시험을 모두 진행한다.
외부 G Kalman BOOT는 One Euro 조정을 거부하고 `one_euro_tunable=0`으로 표시한다.

## 빌드와 검증

운영 모듈은 `src/stereo_vision/stereo_one_euro.c`, `stereo_pose_filter.c`와 대응 public
헤더다. `config/stereo_filter_config.h`의 `ROBOT_STEREO_ONE_EURO_ENABLE` 기본값은 1이다.
비교용 baseline은 빌드에서 `ROBOT_STEREO_ONE_EURO_ENABLE=0`으로 명시적으로 끈다.
Hampel/조합 외부 하네스 빌드도 기본 필터를 꺼야 중복 적용되지 않는다.

Vitis의 기존 `vitis/setup_vitis.ps1 -StereoRole Right`는 두 소스를 일반 `src` 링크에서
자동 등록한다. LEFT는 필터 OFF 심볼을 사용한다. CMake와 GCC 테스트 실행기도 등록했다.

setup 스크립트의 출력은 ELF이며 BOOT 생성/SD 설치는 자동으로 하지 않는다.
BOOT를 만들 때는 같은 새 workspace의 다음 세 파일을 순서대로 Bootgen BIF에 넣는다.
`cnn_camera_gimbal`/`robot_testbench`는 setup의 기본 플랫폼/앱 이름이다.

```text
the_ROM_image:
{
  [bootloader] "<workspace>/cnn_camera_gimbal/export/cnn_camera_gimbal/sw/cnn_camera_gimbal/boot/fsbl.elf"
  "<workspace>/cnn_camera_gimbal/hw/cnn_rgby_pack77_dual_arm_uart0.bit"
  "<workspace>/robot_testbench/Debug/robot_testbench.elf"
}
```

`bootgen -arch zynq -image boot.bif -o BOOT.BIN -w on`으로 생성하고
`bootgen -arch zynq -read BOOT.BIN`의 세 partition과 파일 크기/SHA-256을 기록한다.
기존 captures/외부 하네스/다른 PC의 workspace는 이 일반 빌드의 입력이 아니다.
SD에 설치하기 전 해당 카드의 역할을 확인하고 기존 BOOT와 가중치 쌍을 백업한다.

```powershell
python robot_arm/tests/robot_calibration/run_tests.py --only test_stereo_one_euro --only test_stereo_pose_filter --only test_stereo_filter_integration --only test_stereo_filter_epoch
```

PACK_ID 0x77D4E3BB, XSA/비트스트림, SD의 WGT_V4.BIN + WGT_V4.SHA 검증 계약은 유지한다.
PWM/비동기 시험의 부팅 OFF와 E/A 명시적 활성화도 변경하지 않는다.
소스/호스트/빌드 검증과 실제 보드 부팅·추종 검증을 구분한다.

### 이번 통합 검증 결과

- GCC 전체 32 suites: 27 PASS, 기존 실패 5개. 신규 필터/연결/재획득 시험은 모두 PASS.
- 기존 실패: test_integration_smoke, test_trace, test_axis_replay,
  test_forearm_calibration, test_forearm_replay. 이전 기준에서도 동일 실패가 존재한다.
- UART 모니터 fake-port 54 tests PASS. 외부 비교 epoch/배포 계약 각 2 tests PASS.
- 저장소 src를 직접 링크한 새 LEFT/RIGHT Vitis workspace 모두 빌드 종료 코드 0.
- 앱 소스 경고 0. BSP의 기존 xil_io/servo_pwm_selftest 경고와 XSCT sysconfig 폐기
  경고는 별도 기록했다(역할별 컴파일 경고 반복 출력 24회, 폐기 경고 4회).
- RIGHT take 함수의 production 필터 호출 1회, LEFT 0회. 외부 FC 필터는 링크되지 않았다.
- Bootgen 생성/read 모두 종료 코드 0, LEFT/RIGHT 각각 3 partitions, 4,510,288 bytes.
- 로컬 BOOT/해시 기록: `vitis/boot/stereo_one_euro_source_20261004_174407_542689`.
  생성 BOOT와 실험 로그는 공유 Git 소스에 포함하지 않는다.
- **파일 검증 완료 / 실제 보드 부팅 미검증 / SD 설치 미실행**.

## 별도 미해결 리뷰 항목

장시간 손가락 3D 누락/손 평면 퇴화 후 과거 hand normal을 계속 사용하는 A1의
손목 roll 약 180도 방향 오류는 이번 필터 통합으로 수정하지 않았다. 별도 하네스에서
재현됐으며 손목 기준의 유효 나이/복구 정책을 별도로 수정해야 한다. One Euro가 이
오류를 해결하거나 A2 안전 검사가 잘못된 관측 방향을 판별하는 것은 아니다.
