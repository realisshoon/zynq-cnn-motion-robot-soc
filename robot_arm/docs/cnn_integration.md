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

## Agent4 리뷰 결과

검토일은 2026-09-27이며, 기준 작업 트리는
`D:\zynq-cnn-motion-robot-soc\robot_arm`이다. 요청서에 적힌
`D:\Working\vitis_cnn_gimbal_workspace`는 이 PC에 존재하지 않았다. 사용자의 후속
지시에 따라 Git 작업 트리를 경로 기준으로 삼고, 실제 보드 앱이 있던
`D:\vitis_cnn_gimbal_workspace\cnn_robot_platform_gimbal`은 원본 대조에만 읽기
전용으로 사용했다. 이 리뷰에서는 보드 다운로드, 카메라/CNN/PWM 구동, flash 작업을
하지 않았다.

현재 상태는 요청서의 "커밋 전 working tree" 설명과 다르다. 통합 코드는 이미
`58fb624`(`feat: CNN 자세 입력 + 카메라 짐벌 통합 (dev/integration)`)에 커밋되어 있고,
현재 HEAD는 리뷰 요청 문서를 추가한 `fd711ad`다. 리뷰 시작 전 working tree는 clean이었다.

### 판정 요약

| 항목 | 판정 | 근거 수준 |
|---|---|---|
| CNN 펌웨어 원본 포팅 | 조건부 일치 | 원본 파일 직접 비교 |
| 초기화 및 가중치 실패 동작 | 일치 | 원본 `main.c`와 코드 직접 비교 |
| 비차단 추론 변환 | 조건부 승인 | 코드 직접 비교 + 호스트 테스트 |
| 결과 레지스터 해석 | 펌웨어 기준 일치 | 원본 드라이버 직접 비교, RTL 직접 증거 없음 |
| 카메라 pan/tilt PWM | 원본과 일치 | 원본 소스 및 XSA HWH 직접 비교 |
| CNN/Timer IRQ 배선 | 일치 | XSA HWH와 생성 `xparameters.h` 직접 비교 |
| XSA identity/구성 | 일치 | 저장소 XSA 직접 해시 및 내부 HWH 검사 |
| DDR 메모리 분리 | 주소상 안전 | 코드/링커 설정 계산, 실보드 부하 측정 없음 |
| UART 공유 | 조건부 승인 | 코드 검토, 포화/장시간 실측 없음 |
| 통합 최종 승인 | **REWORK REQUIRED** | 아래 P1 동시성 문제와 RTL 근거 부재 해결 필요 |

### 1. 원본 소스 직접 대조

`src/cnn_firmware/`의 C/H 파일 44개를 원본 workspace와 줄바꿈을 정규화해 비교했다.
40개는 내용이 동일했다. `vitis/reference/cnn_original_main.c.txt`도 원본 `main.c`와
줄바꿈을 제외하면 완전히 동일했다. 차이가 있는 파일은 다음 4개뿐이다.

- `cnn/cnn_bringup.c`, `cnn/cnn_bringup.h`, `cnn/cnn_types.h`: `CNN_PENDING`과
  `cnn_bringup_start/service()`를 추가해 완료 IRQ 대기를 foreground 상태 기계로
  바꿨다.
- `camera_tracking/camera_tracking_app.c`: `ROBOT_TRACE` 빌드에서 기존 추적기
  `xil_printf` 한 줄을 억제했다. 추적 계산과 PWM 쓰기는 바뀌지 않았다.

따라서 카메라, VDMA, HDMI, overlay, CNN 레지스터, SD/FatFs, CSV, tracker와 PWM
드라이버 본체는 원본 그대로다. 비동기 추론 경계만 새 코드이므로 그 부분은 별도
검증 대상으로 남는다.

### 2. 초기화 순서와 가중치 실패

원본 `main()`의 실제 순서는 요청서에 적힌 "가중치 먼저"가 아니다. 실제 순서는
카메라 GPIO/MIPI/OV5640/VDMA, HDMI, GIC, overlay/CNN IRQ/tracker, 메뉴 출력,
그 다음 자동 `w -> CSV -> u -> a`다. 통합 코드도 이 순서를 유지한다. 차이는
`platform_init()`이 공유 GIC와 20 ms Timer를 카메라 초기화보다 먼저 시작한다는 점이다.
초기화 중 누적된 Timer tick은 `platform_tick_due()`의 첫 호출에서 의도적으로 버리므로
코드상 로봇 램프가 몰아서 실행되지는 않는다.

`0:/WGT_V4.BIN` 로드 또는 SHA 검증이 실패하면 `cnn_auto_start()`만 중단되고
`cnn_app_init()`은 성공으로 돌아가므로 메인 루프와 UART 메뉴는 남는다. 원본 동작과
일치한다. 가중치 로드, CSV open/write/sync/close는 원본과 같이 foreground 동기 호출이다.

### 3. 비차단 서비스 변환

`cnn_bringup_start()`의 prepare/configure/IRQ arm/start 순서와
`cnn_bringup_service()`의 DONE/ERROR 확인, 결과 snapshot, overlay commit,
DONE clear, frame ID 증가 순서는 원본 blocking 경로와 일치한다. ISR은 status와 완료
시각을 latch하고 CNN level IRQ를 내린 뒤 event flag만 세운다. 정상 추론을 기다리는 동안
20 ms 로봇 tick을 막지 않는 구조다.

색 margin `m`과 카메라 설정 `j`는 원본의 blocking UART 입력에서
`cnn_console_poll()` 상태 기계로 바뀌었다. 항목 순서, 범위 검사, 마지막 일괄 적용은
유지됐지만, 여러 main-loop iteration 동안 추론과 동시에 진행된다는 점은 원본과 다르다.

다음 문제는 수정이 필요하다.

1. **P1 — 실행 중 메모리 변경 가능:** continuous 추론 중에도 메뉴가 먼저 실행되므로
   `w`가 `0x10000000`의 가중치를 다시 쓰거나 `g`가 `0x11200000`의 SG descriptor를
   다시 쓸 수 있다. 원본은 한 프레임 호출이 blocking이라 이 명령들이 프레임 사이에만
   실행됐다. `cnn_app.c`에서 `cnn_ctx.running`일 때 `w`와 `g`를 거부하거나 현재 프레임
   완료 뒤로 queue해야 한다.
2. **P2 — 오류 진단 축소:** 원본 blocking 경로는 timeout/HW fault/비정상 DONE에서
   즉시 `cnn_diag_dump()`를 실행했다. 새 service 경로는 TRACE와 오류 문자열만 남기고
   연속 모드를 정지한다. 오류 snapshot을 보존하려면 foreground 오류 분기에서 진단을
   호출하거나 명시적인 경량 snapshot을 남겨야 한다.
3. **P2 — `s` 동작의 중간 프레임 의미:** continuous frame이 이미 실행 중일 때 `s`를
   누르면 새 단일 프레임을 시작하는 대신 현재 실행 중인 프레임을 single 결과처럼
   출력한다. 원본에서는 blocking 때문에 이 입력 시점 자체가 없었다. UI 계약을 정하고
   문서화하거나 pending 요청을 다음 프레임에 적용해야 한다.

### 4. CNN 결과 포맷과 로봇 입력 매핑

원본 `cnn_hw.c/h`와 포팅본은 동일하다. firmware가 사용하는 결과 형식은 다음과 같다.

- 관절 17개는 COCO 순서다. 어깨 5/6, 오른쪽 팔꿈치 8, 오른쪽 손목 10이다.
- 관절 word는 `x=[11:0]`, `y=[23:12]`, signed score=`[31:24]`이고, valid는 별도
  `JOINT_FLAGS[16:0]`이다.
- 색 marker word는 `x=[10:0]`, `y=[20:11]`, found/valid=`[31]`이다.
- 좌표는 정규화 값이 아닌 1280x720 표시 영상의 pixel 좌표다. `input_pose_cnn.c`는
  x `<1280`, y `<720`을 다시 검사한다.
- `input_pose_cnn.c`는 red marker를 `finger1`, blue marker를 `finger2`로 전달한다.
  자세 전체 valid는 양쪽 어깨와 오른쪽 팔꿈치/손목 4점이 모두 valid일 때만 1이다.

이 매핑은 원본 보드 firmware 및 보존된 헤더와는 일치한다. 그러나 Git 저장소와 XSA에는
CNN AXI register RTL 또는 기계 판독 가능한 register specification이 없으므로, 이
리뷰만으로 RTL bit slice까지 독립 재증명할 수는 없다. XSA HWH는 AXI 주소와 IP identity는
제공하지만 내부 register field는 제공하지 않는다. release 근거로 쓰려면 RTL SHA와
register-map 문서를 저장소에 연결해야 한다.

### 5. 카메라 짐벌 PWM

카메라 tracker/PWM 소스는 원본과 동일하다. XSA/xparameters의
`servo_pwm_camera_0` base는 `0x43C90000`이고, 드라이버는 ch0 pan, ch1 tilt,
`CONTROL=0x18`, `UPDATE=0x1C`, enable bit 0을 사용한다. 초기화 직후 출력을 끄고,
자동 시작의 `u`에서 tracker와 camera PWM을 함께 켜는 순서도 원본과 같다.

XSA에는 `servo_pwm_0`과 `servo_pwm_camera_0`가 각각 별도 instance로 들어 있고 주소는
각각 `0x43C70000`, `0x43C90000`이다. `ROBOT_ARM_PWM_ENABLE`은 기본 Vitis 스크립트에
정의되지 않아 로봇 팔 PWM 적용 경로는 비활성 상태를 유지한다. 이 리뷰에서는 실제
pan/tilt 방향, pulse 폭, 물리 이동을 재측정하지 않았다.

### 6. XSA와 IRQ

`vitis/xsa/cnn_camera_gimbal.xsa`의 SHA-256은
`CF19EB89BEBDF960EEF33622464BF3C40AEA2CEC6133EAFCC099ED97BFA5D5AF`이며,
기존 보드용 export와 byte-identical하다. 내부에 `cnn_camera_gimbal.bit`가 있어
bitstream 포함 export다. 메타데이터는 Vivado 2020.2 CL 3064766, 생성 시각
2026-09-26 15:56:18, Zybo Z7-20 `xc7z020clg400-1`을 가리킨다.

HWH에서 확인한 custom IP는 CNN accelerator 4.1, keypoint overlay 1.0,
robot `servo_pwm` 1.0, camera `servo_pwm` 1.0이다. 주소는 CNN `0x43C60000`,
overlay `0x43C50000`, robot PWM `0x43C70000`, AXI Timer `0x43C80000`, camera PWM
`0x43C90000`, VDMA `0x43000000`이다.

IRQ concat의 `In0`은 CNN level-high IRQ, `In1`은 AXI Timer level-high IRQ다. 동일 XSA로
생성된 `xparameters.h`에서 CNN은 GIC ID 61, Timer는 62다. 양쪽 모두 priority `0xA0`,
level-high 설정이며 하나의 `XScuGic`를 공유한다. 같은 priority라 서로 preempt하지는
않지만 두 ISR 모두 짧고 소스가 level 방식이므로 코드상 유실 조건은 발견하지 못했다.
실제 동시 발생 stress test는 수행하지 않았다.

XSA 자체에는 원본 Vivado project와 XDC 파일의 SHA가 없으므로 "어느 제약 파일에서
생성됐는지"까지 Git만으로 재현할 수는 없다. 현재 증명 범위는 지정한 보드 export와
byte identity, HWH topology, 포함 bitstream이다.

### 7. DDR 경계

이 XSA의 PS DDR base는 `0x00100000`이다. 따라서 코드의
`DDR_BASE + 0x0A000000` frame base는 실제로 `0x0A100000`이다. 1280x720 RGB888
frame은 `0x002A3000` bytes이며 3개 buffer의 끝은 `0x0A8E9000` exclusive다.
가중치는 `0x10000000..0x1013A600`, feature map은 `0x11000000`과 `0x11100000`,
SG는 `0x11200000`에서 시작한다.

링커 영역 `0x00100000` + `0x09F00000`은 `0x0A000000` exclusive에서 끝나므로 첫
frame buffer 전까지 `0x10000` bytes의 간격이 있고 주소상 겹치지 않는다. 다만
`setup_vitis.ps1` 111행의 "frame buffer는 0x0A000000" 주석은 실제 주소
`0x0A100000`과 다르므로 공유 경계 담당자가 수정해야 한다. 이 리뷰에서는 Vitis link를
재실행하지 않았으며, 기존 문서에 기록된 링크 성공 결과만 확인했다.

### 8. UART 공유와 실행 증거

UART RX는 CNN 메뉴 전용이고 pose는 CNN 결과에서 들어온다. `m`/`j` 입력 상태 기계는
foreground polling 방식이라 정상 입력 중에는 로봇 tick을 기다리게 하지 않는다.
TRACE는 공간이 없으면 쓰기를 거부하는 ring API를 사용한다. 반면 `xil_printf`의
`outbyte()`는 32 KiB ring이 가득 차면 TX 공간이 날 때까지 polling하므로 모든 UART
출력이 엄밀히 비차단인 것은 아니다. 긴 진단 출력이나 낮은 baud/host 정지 상황에서는
20 ms foreground 처리를 지연시킬 수 있다. 이 코드는 수정 제한 대상인
`platform_vitis.c`에 있으므로 여기서는 보고만 한다.

이번 리뷰에서 현재 HEAD로 실행한 host 전체 테스트 결과는 CNN 관련
`test_input_pose_cnn`, `test_cnn_trace`, `test_robot_pwm_disabled`가 PASS였다. 전체
runner에서는 `test_integration_smoke`, `test_trace`, `test_axis_replay`,
`test_forearm_replay` 4개 실패가 재현됐다. 이 4개가 통합 전에도 동일했다는 판단은 이
문서의 기존 검증 기록을 인용한 것이며, 이번 리뷰에서 과거 commit을 별도로 checkout해
재실행하지는 않았다. CNN 리뷰 범위 파일은 건드리지 않았다. Vitis build와 실제 보드
실행도 이번 리뷰에서 새로 수행하지 않았다.

### 후속 조치

1. `cnn_app.c`에서 실행 중 `w`/`g`를 금지하거나 frame 완료 뒤로 지연한다.
2. 비동기 오류 경로에 원본 수준의 진단 snapshot을 복구한다.
3. `s` 입력의 실행 중 의미를 확정한다.
4. 공유 경계 담당자가 `setup_vitis.ps1`의 frame 주소 주석과 UART 포화 시 blocking
   설명을 고친다.
5. CNN register-map/RTL SHA와 Vivado XDC provenance를 release 문서에 연결한다.
6. 수정 후 새 XSA/ELF로 CNN+Timer 동시 IRQ, SD logging 중 20 ms tick overrun,
   amera pan/tilt를 실보드에서 확인한다.
