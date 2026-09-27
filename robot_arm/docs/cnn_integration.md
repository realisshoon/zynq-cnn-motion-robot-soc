# CNN–로봇 통합 (dev/integration)

현재 실행 진입점은 `src/integration/main_integration.c` 하나다. `src/cnn_firmware/`는 CNN 팀 Vitis 앱의 드라이버와 기능 모듈을 옮긴 것이며, 원본 `main.c`는 빌드하지 않고 `vitis/reference/cnn_original_main.c.txt`에 비교용으로 보존했다. FPGA 하드웨어는 `vitis/xsa/cnn_camera_gimbal.xsa`를 쓴다 (SHA-256 `CF19EB89BEBDF960EEF33622464BF3C40AEA2CEC6133EAFCC099ED97BFA5D5AF`). 기존 `etc/`와 UART 자료는 삭제하지 않았다.

## 실행 경로

1. `platform_init()`이 로봇 HAL, PS UART, 공유 GIC, AXI Timer의 20 ms 인터럽트를 초기화한다. 로봇 PWM은 기본 빌드에서 비활성화한다.
2. `cnn_app_init()`이 OV5640/MIPI/VDMA/HDMI/오버레이, CNN IRQ, 카메라 추적기를 초기화한다. SD에서 `0:/WGT_V4.BIN`(또는 원본 로더의 긴 이름 대안)을 읽어 검증하고 CSV 기록·카메라 추적·연속 추론을 켠다. 가중치 로드가 실패하면 메뉴를 남기고 추론은 시작하지 않는다.
3. CNN 완료 인터럽트는 결과가 준비됐다는 플래그만 세운다. 메인 루프의 `cnn_bringup_service()`가 결과를 읽고 오버레이와 카메라 추적기에 전달한다. `input_pose_cnn_publish()`는 COCO 양쪽 어깨(5, 6), 오른쪽 팔꿈치(8), 손목(10), 빨강/파랑 마커를 `HumanPose2D`로 변환한다. 새 프레임 하나만 보관하고 중복 frame ID는 버린다.
4. 새 자세마다 기존 Agent1→Agent2 경로를 실행한다. 별도 20 ms 타이머 틱에서 Agent2 램프→Agent3 PWM 변환을 실행한다. 기본 빌드는 변환값을 TRACE로만 확인하고 로봇 PWM 레지스터에 적용하지 않는다. 카메라 `pwm_camera` IP의 pan/tilt 추적은 CNN 원본 동작대로 작동한다.

CNN 완료는 **인터럽트 기반**이다. 메인 루프의 `cnn_app_service()`는 인터럽트 플래그를 소비하고 명령·카메라 PWM을 서비스하는 함수이며, 완료를 기다리며 루프를 막지 않는다. 메뉴 `m`(색 margin), `j`(카메라 설정)는 여러 루프에 걸쳐 입력받는다. SD 가중치 로드/CSV 쓰기, 일부 진단 명령은 원본처럼 동기식이므로 실제 보드에서 20 ms 틱 지연을 측정해야 한다.

## 빌드와 첫 검증

Vitis 2020.2에서 저장소 루트 기준:

```powershell
powershell -ExecutionPolicy Bypass -File robot_arm\vitis\setup_vitis.ps1 -Workspace D:\vws_cnn_robot
```

스크립트는 새 XSA로 standalone 플랫폼을 만들고 `xilffs`를 BSP에 넣으며, `SERVO_PWM_DRIVER_USE_XILINX`와 `ROBOT_TRACE`를 정의한다. `src`는 Vitis에 소프트 링크된다. 독립된 `main` 세 개는 제외한다. 앱 링커의 DDR 영역은 `0x00100000..0x09FFFFFF`로 제한하여 `0x0A000000` 영상 버퍼 및 `0x10000000` 이상 CNN 작업 영역과 겹치면 링크 오류가 나도록 한다. Debug ELF 경로는 `<Workspace>\robot_testbench\Debug\robot_testbench.elf`다. 이 ELF가 **빌드 성공**했다는 것은 보드에서 카메라/CNN/PWM이 정상 작동한다는 뜻은 아니다.

UART TRACE 빌드는 921600 8N1이다. 기존 `A1/P3/A2/TK/SM/EV`에 `CN`(프레임·IRQ·시간), `CAM`(카메라 추적 PWM), `IN`(Agent1에 넘긴 2D 좌표), `CS`(CNN 누적 통계), `CE`(CNN 오류)가 추가됐다. `SM`의 서보 쓰기 수는 기본 빌드에서 0인 게 정상이다. 카메라 PWM은 별도 IP이므로 카메라 추적을 켜면 움직일 수 있다. `ROBOT_ARM_PWM_ENABLE`은 실제 로봇 서보 시험을 별도로 승인받은 빌드에서만 정의한다.

보드 확인 순서는 새 XSA/ELF와 SD 가중치 파일 일치, UART 부팅 및 CNN IRQ 증가, `CN`→`IN`→`A1`→`A2`와 20 ms `TK`/`SM` 확인, HDMI 오버레이/카메라 추적 확인, 그 뒤 로봇 PWM 시험이다. 카메라가 pan/tilt할 때 영상 좌표가 움직이는 효과를 Agent1 좌표계에서 보정하는 기능은 아직 없다. 물리 각도 일치와 제어 틱 지연은 실물에서 확인해야 한다.

## 검증 상태

- Vitis 2020.2 Debug, TRACE 포함, 새 XSA와 `xilffs`: ELF 링크 성공. 로봇 PWM은 기본 비활성.
- 새 CNN 결과→자세 변환, CNN TRACE, 로봇 PWM 비활성 호스트 테스트 통과.
- 기존 호스트 전체 러너의 실패 4개(`test_integration_smoke`, `test_trace`, `test_axis_replay`, `test_forearm_replay`)는 이 작업 전 HEAD에서도 동일하게 재현됐다. 이 통합 작업에서는 해당 기존 fixture를 바꾸지 않았다.
- 보드 실행, SD 가중치 로드, 실제 CNN IRQ/카메라 PWM/로봇 서보 구동은 수행하지 않았다.
