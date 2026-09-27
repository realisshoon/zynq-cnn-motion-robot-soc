# Agent4(CNN/카메라 짐벌) 담당 전달용 — dev/integration 리뷰 요청 (2026-09-27)

Codex가 Agent4(CNN/카메라 짐벌 담당)가 제공한 원본 소스를 기반으로, `dev/robot`을 복사한
`dev/integration` 브랜치에 CNN+로봇 통합 작업을 진행했다(아직 커밋 안 된 working tree
상태, `robot_arm/docs/cnn_integration.md` 참고). Claude(Agent2)가 통합 경계 쪽(Agent1/2/3
코드, 로봇 PWM 기본 비활성 안전장치)은 이미 diff와 실행으로 직접 검증했지만,
`src/cnn_firmware/`(원본 포팅 코드) 내부와 하드웨어(XSA/IRQ/레지스터 포맷) 쪽은 Agent2
도메인 밖이라 "코드가 있다"는 것만 확인했고 원본과 실제로 같은지는 검증하지 못했다.
이 문서는 그 나머지를 Agent4가 확인하기 위한 것이다.

**주의**: 이 문서와 통합 작업 자체(Codex의 자체 설명)를 그대로 믿지 말 것. "포팅했다"는
주장과 "원본과 동일하게 동작한다"는 다르다 — 반드시 원본 소스와 직접 대조해서 검증한다.

## 1. 먼저 읽을 자료

- `robot_arm/docs/cnn_integration.md` — Codex가 남긴 통합 설명/검증 상태
- `robot_arm/vitis/reference/cnn_original_main.c.txt` — 원본 main() 보존본(비교용, 빌드 대상 아님)
- 원본 소스: `D:\Working\vitis_cnn_gimbal_workspace` (이번 리뷰의 읽기 전용 기준)
- 포팅된 코드: `robot_arm/src/cnn_firmware/`, `robot_arm/include/integration/cnn_app.h`,
  `robot_arm/src/integration/cnn_app.c`, `cnn_console.c/h`
- 통합 경계(공유): `robot_arm/src/integration/input_pose_cnn.c/h`, `main_integration.c`,
  `platform_vitis.c`
- 현재 `dev/integration` 브랜치의 working tree (커밋 전 상태 그대로)

## 2. 리뷰 주안점

1. **초기화 순서 보존**: 원본 main()의 SD 가중치 로드(`0:/WGT_V4.BIN`) → OV5640/MIPI/
   VDMA/HDMI/오버레이 → CNN IRQ → 카메라 추적기 초기화 순서가 `cnn_app_init()`에
   빠짐없이, 같은 순서로 옮겨졌는지. 가중치 로드 실패 시 원본과 동일하게 "메뉴만 남기고
   추론은 시작하지 않음"으로 동작하는지.
2. **서비스 루프 동등성**: 원본이 동기(blocking)로 처리하던 부분(SD 쓰기, CSV 기록, 일부
   진단 명령)이 `cnn_app_service()`로 옮겨지면서 그대로 동기로 남았는지, 혹시 비동기로
   바뀌면서 순서·타이밍 가정이 깨진 곳이 있는지.
3. **CNN 결과 레지스터 해석**: `input_pose_cnn.c`의 관절 인덱스
   (`CNN_JOINT_LEFT_SHOULDER`/`RIGHT_SHOULDER`/`RIGHT_ELBOW`/`RIGHT_WRIST`)와
   `red_marker`/`blue_marker` 워드의 비트 배치(x=bit0-10, y=bit11-20, valid=bit31,
   `CNN_IMAGE_WIDTH/HEIGHT`=1280x720)가 실제 CNN 출력 레지스터 포맷과 정확히
   일치하는지 — 이건 Claude가 코드만 읽고 그대로 믿은 부분이라, 원본 대비 직접 대조가
   꼭 필요하다.
4. **카메라 pan/tilt(`pwm_camera` IP) 동작 보존**: 게이트·레지스터 오프셋이 포팅 중 안
   바뀌었는지, gimbal 추적이 원본과 동일하게 동작할지.
5. **인터럽트 배선**: `platform_vitis.c`의 AXI Timer IRQ ID 주석이 기존 61(F2P[0])에서
   새 XSA 기준 62로 바뀌어 있다 — 새 block design의 실제 IRQ ID·우선순위(CNN IRQ와
   동일 우선순위 0xA0)가 맞는지, 인터럽트 경합/누락 가능성이 있는지.
6. **XSA/하드웨어 설계 확인**: `robot_arm/vitis/xsa/cnn_camera_gimbal.xsa`
   (SHA-256 `CF19EB89BEBDF960EEF33622464BF3C40AEA2CEC6133EAFCC099ED97BFA5D5AF`)가
   의도한 최신 block design/제약 파일에서 나온 게 맞는지, 카메라·CNN·로봇 PWM IP가
   전부 올바르게 배치됐는지.
7. **DDR 메모리 맵**: 앱 스택/힙 영역을 `0x00100000..0x09FFFFFF`로 제한했다(CNN 프레임
   버퍼 `0x0A000000`, 가중치/작업 버퍼 `0x10000000` 이상과 안 겹치게). 이 경계값이 실제
   프레임버퍼/가중치버퍼 크기와 맞는지, 이만큼 앱 DDR을 줄여도 실제로 부족하지 않은지.
8. **UART 공유**: 자세 입력용 UART 수신 코드가 없어지고 UART가 CNN 콘솔 명령(`m`, `j`)
   + TRACE 출력 전용으로 바뀌었다 — Agent4가 쓰던 콘솔 명령 처리(`cnn_console.c`)가 이
   구조에서도 그대로 동작하는지(공유 TX 링버퍼를 통한 비차단 출력으로 바뀜).

## 3. 수정 제한 범위

- **자유롭게 수정 가능**: `src/cnn_firmware/` 전체, `include/integration/cnn_app.h`,
  `src/integration/cnn_app.c`, `cnn_console.c/h`, `vitis/xsa/`, `vitis/reference/`.
- **문제 발견 시 보고만, 직접 수정하지 말 것**(공유 경계 — Agent2가 이미 diff/실행으로
  검증한 파일): `src/integration/main_integration.c`, `agent_pipeline.c/h`, `platform.h`,
  `platform_vitis.c/h`, `trace.c/h`, `input_pose.h`, `input_pose_cnn.c/h`,
  `vitis/setup_vitis.ps1`. 문제가 있으면 무엇을, 왜 바꿔야 하는지 적어서 알려달라.
- **절대 건드리지 말 것**(다른 에이전트 소유 — 로봇 팔 관련 전체):
  `src/human_target_angle/*`, `src/robot_calibration/*`, `src/output_controller/*`,
  `src/drivers/*`, `include/robot_calibration/*`, `include/human_target_angle/*`,
  `include/output_controller/*`, `include/common/robot_types.h` 및
  `forearm_calibration_config.c`/`servo_config.c` 등 캘리브레이션 값.
- **`ROBOT_ARM_PWM_ENABLE` 기본 비활성 유지**: 이 심볼이 로봇 서보를 실제로 켜는 유일한
  스위치다. 기본 빌드(`setup_vitis.ps1`)에서 정의하지 않는 현재 상태를 유지할 것 — 별도
  승인 없이 이 게이트나 기본값을 바꾸지 말 것.
- **보드 flash·실제 구동 금지**: 이번 리뷰는 분석·원본 대조까지다. 실제 보드에 올리거나
  카메라/CNN/서보를 구동하는 건 별도 승인 후에.
- **`D:\Working\vitis_cnn_gimbal_workspace`는 읽기 전용 대조 기준**으로만 쓰고, 그 폴더
  자체는 수정하지 말 것.

## 4. 보고

결과를 `robot_arm/docs/cnn_integration.md`에 `## Agent4 리뷰 결과` 절로 추가해라. 항목별로
원본과 일치/불일치, 불일치면 무엇이 다른지와 원본 근거, 실측·실행으로 확인한 것과 코드만
읽고 판단한 것을 구분해서 남겨라.
