# 통합 미해결 문제

상태는 `PASS / FAIL / NOT RUN / BLOCKED`로 구분한다. 실행 실패와 실행 불가능의 원인, 원본 구현의 문제와 이번 통합 변경을 구분한다. 아래 문제를 통과시키기 위한 RTL·Firmware·UVM 기능 수정은 하지 않았다.

## B01 — RTL/Firmware 식별자 불일치: BLOCKED

| 값 | `dev/cnn` RTL 실제 read mux | 최신 Firmware 기대값 |
|---|---|---|
| VERSION | `0x00040003` | `0x00040004` |
| FEATURES | `0x0000000D` | `0x0000001D` |
| PACK_ID | `0xC9854BB2` | `0x77D4E3BB` |

`CNN/rtl/cnn_accelerator_top.v`와 `robot_arm/src/app/cnn/cnn_hw.h/.c`에 근거한다. 선택한 RTL로 구현한 하드웨어에서는 Firmware probe 비교가 실패한다. 실제 설치 XSA의 register 값을 물리적으로 읽은 결과는 아니다. 하드웨어 소유자가 승인한 RGBY RTL·pack·XSA 출처를 확정해야 한다. 숫자만 바꿔 일치시키면 안 된다.

## B02 — RGBY register map 차이: BLOCKED

Firmware는 Yellow `0x104/0x108/0x10C`와 4비트 color-enable을 사용한다. 선택한 RTL에는 해당 주소가 없으며 color-enable은 3비트다. 공통 register offset의 정적 대응과 RGBY 기능 호환성을 구분한다.

## B03 — XSA·BOOT·현재 소스 대응: BLOCKED

기본 `final_uart0.xsa`의 CNN 주소는 `0x43C60000`, 로봇 PWM 주소는 `0x43C70000/0x43CA0000`이다. 카메라 PWM IP는 없고 Firmware는 MMIO를 생략하도록 처리한다. XSA 안에 원본 RTL은 없다. 과거 BOOT의 bitstream과 현재 기본 XSA의 bitstream도 동일하지 않다.

현재 소스로 만든 ELF, 선택한 CNN RTL, XSA bitstream, SD의 BOOT·가중치가 같은 배포본이라는 증거가 필요하다. BSP/ELF 빌드 성공만으로 해결되지 않는다. 물리 FPGA 프로그램과 로봇 구동은 NOT RUN이다.

## B04 — Golden ZIP archive hash: FAIL

고정 환경 Python 3.10/NumPy 1.24.4의 실제 ZIP 입력 실행에서 `ValueError: version/hash mismatch`가 발생했다. packed binary의 크기·hash는 manifest와 일치하고, packed 경로의 frame 121~125 결과는 기존 관절·heatmap·offset과 일치한다.

manifest가 가리키는 원본 archive 출처를 확인해야 한다. hash 필드나 Golden 수치를 새 ZIP에 맞춰 임의 변경하지 않는다.

## B05 — UVM 실행기·입력 벡터 누락: BLOCKED

- `tb_depthwise_conv_pe.v`: `dw_taps.mem`, `dw_weights.mem`, `dw_params.mem`, `dw_expected_acc.mem`, `dw_expected_out.mem` 누락.
- downsample `+GOLDEN`: `image_bytes.hex`, `golden_rgb.hex` 누락. 기본 directed 모드와 별개다.
- DMA README의 `scripts/compile.sh`, `run_dma_stream_regression.py`, `report_coverage.sh`, `open_verdi_coverage.sh`는 Git에 없다. 원본 ignore가 `scripts/`를 제외한다.
- Golden UVM의 `final_expected.hex` 및 완전한 회귀 입력·기존 evidence 로그 집합이 없다.

제공된 weight/image 변환기는 scratch 출력에 실행했다. 누락된 expected를 임의로 작성하거나 원본 검증 기준을 바꾸지는 않았다.

## B06 — Native DLL 재현성: BLOCKED

현재 RIGHT/LEFT/XYZ DLL의 로딩과 읽기 전용 ABI·역할 조회는 PASS다. 기존 DLL은 그대로 보존했다. 하지만 포함된 과거 재빌드 스크립트는 `4acc02f...` revision과 옛 Robot 경로를 요구하며 현재 ABI 3 배포 DLL의 재현성을 증명하지 않는다. 현재 펌웨어와 LEFT gripper 보정의 수치 동일성도 미확인이다.

## B07 — Directed TB와 RTL 모듈명 차이: FAIL

`CNN/tb/tb_argmax_threshold.v:59`는 `argmax_threshold`를 인스턴스화하지만 선택된 RTL은 `argmax_offset_select`다. XSim elaboration에서 `VRFC 10-2063 Module <argmax_threshold> not found`가 발생했다. TB나 RTL의 이름·포트·expected를 이번 단계에서 수정하지 않았다.

## B08 — Color marker Directed TB 실패: FAIL

실제 XSim 실행에서 red/blue 결과, 나눗셈 횟수 및 MIN_COUNT 검사가 실패했고 600초 wall guard에서 종료됐다. 원본 TB와 현재 RTL의 포트·설정 계약을 소유자가 대조해야 한다. 관측 로그를 기준으로 실패를 보고하며 특정 원인을 입증 없이 단정하지 않는다.

## B09 — UVM 도구 호환성·VCS 회귀: BLOCKED

XSim 2020.2에서는 Control의 `checker` 식별자에 대한 컴파일 오류가 발생한다. DMA-Stream은 기본 timescale 지정 후 elaboration에서 `EXCEPTION_ACCESS_VIOLATION` 도구 오류가 발생한다. 일부 SystemVerilog cover 미지원과 RANDC→RAND 경고도 있다.

Common base smoke의 PASS를 Control/DMA/Golden 기능 회귀나 coverage 승인으로 확대하지 않는다. Windows에서 VCS/Verdi 원본 회귀는 NOT RUN이다. 검증 소스·assertion·timeout을 바꾸거나 checker를 비활성화하지 않았다.

## B10 — Firmware 표준 Vitis 빌드 환경: BLOCKED

RIGHT/LEFT 모두 새 workspace에서 실제 빌드를 시도했다. Windows PowerShell 시도는 `Get-FileHash` 로딩 문제로 중단됐고, PowerShell 7 시도는 `Timeout while establishing a connection with Vitis` 뒤 앱 `.cproject`가 없어 중단됐다. `Run-Xsct`가 이 Tcl 실패를 `[OK]`로 표시한 것과 빌드 성공을 구분한다.

IDE 연결과 스크립트 오류 검출을 별도 환경/빌드도구 작업으로 확인해야 한다. Native HSI BSP 생성과 Vitis ARM GCC 직접 RIGHT/LEFT compile/link는 PASS다. 각 역할 C 72개, compiler warning 0개, ARM ELF와 예약 DDR 아래 load 범위를 확인했다. 표준 IDE 스크립트 경로의 BLOCKED와 이 대체 경로의 PASS를 구분한다. source를 변경해 빌드를 가장하지 않는다.

## B11 — 실보드 UART와 물리 시스템 검증: NOT RUN

Firmware TK 17필드는 Unity 파서와 정적으로 대응하고, 순수 C# gateway fixture는 PASS다. A1/A2/P3 전체를 현재 Unity 런타임이 처리하는 것은 아니다. 실제 COM, 설치 BOOT, HDMI overlay, 물리 서보 출력·보정은 검증하지 않았다.

## Git 이력 보존 결정

STEP 2는 main의 단일 자식 snapshot commit이므로 승인된 source 이력의 commit 168개와 별도 feature 원본 102개, 합계 270개가 아직 통합 후보의 조상이 아니다. 현재 commit을 재작성하지 않는 후속 provenance merge와 GitHub merge commit 방식이 권고안이다. 이력 연결 merge 자체는 이번 단계에서 실행하지 않았다. 4개 source만 연결할 때는 3개 tip, 원격 36개 branch 전체 이력에는 비중복 18개 tip이 필요하다. 세부 SHA·작성자·위험은 `validation/step3/STEP3_HISTORY.json`에 기록했다.
