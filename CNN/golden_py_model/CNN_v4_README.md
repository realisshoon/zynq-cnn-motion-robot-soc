# CNN-v4.0 설계 인계 패키지

Zybo Z7-20의 CNN 가속기를 각 담당자가 구현하기 위한 명세와 정수 기준 모델입니다.
작성 기준: 2026-09-15. 원본 노션의 담당 배정은 유지했습니다.

**현재 상태: 구현 기준 작성 및 Python 수치 검증 완료. 실제 RTL 일치 검증과 보드 합성 승인은 미완료입니다.**
실제 RTL, Vivado 프로젝트/BD, 원본 float 모델 소스가 첨부되지 않았습니다. 따라서 이 패키지는 기존 RTL과의 동일성이 이미 입증된 모델이 아니라, 앞으로 작성할 RTL이 따라야 할 정수 동작 기준입니다. AXI bus 전체의 cycle-accurate simulator는 아닙니다.

## 바로 사용할 파일

| 파일 | 용도 |
|---|---|
| `CNN_v4_Interface_Registers.xlsx` | 13개 모듈의 438개 포트, 44개 레지스터, descriptor, 레이어 표, 자원/시간 예산 |
| `model/cnn_model.py` | NumPy만 사용하는 정수 추론, 전처리, 좌표 복원, 색상 마커 검출, 가중치 pack/unpack |
| `generated/manifest.json` | 29개 연산의 shape, 순서, stride/dilation/padding, 정수 형식, DMA offset/length |
| `generated/weights_v4.bin` | 실제 DMA에 사용할 정렬된 가중치와 bias/M. 총 1,287,680bytes |
| `generated/interface.json` | 포트와 레지스터의 기계 판독용 원본 |
| `generated/layer_rom.hex` | 29개 256bit descriptor의 Verilog `$readmemh` 입력 |
| `generated/registers.h` | PS 소프트웨어의 레지스터 주소 상수 |
| `generated/cnn_common_params.vh` | RTL 공용 상수 |
| `notion_pages/` | 수정된 노션 페이지의 본문 |
| `notion_before/` | 변경 전 노션 조회 결과. 외부 페이지로 접근해 원본 이력도 확인 가능 |
| `validation/features/` | seed4 합성 입력에 대한 29개 연산의 HWC 출력 `.npy` |
| `validation/package_validation.json` | 가중치 재구성, ROM, 전체 추론 비교 결과 |
| `implementation/check_resources.tcl` | Vivado에서 실행할 자원/최악 slack 확인 보조 스크립트 |
| `source/` | 사용자가 제공한 가중치 archive와 변경하지 않은 이전 golden model |

## 구현 담당과 읽는 순서

| 담당 | 모듈 |
|---|---|
| 이영현 | top_level_fsm, feature_map_io, weight_bram_swap_fsm, 전체 통합 |
| 유지민 | downsample_module, input_conv_pe, line_buffer |
| 한승훈 | depthwise_conv_pe, pointwise_conv_pe, color_marker_detect |
| 조정민 | argmax_threshold, coord_restore, layer_param_rom |

공용 규칙 → 담당 모듈 → 통합/레지스터 페이지 순으로 읽습니다. 각 모듈 페이지에 전체 포트와 공통 descriptor, 수치 폭, 상태 전이, 저장 구조와 검증 항목을 포함했습니다. 구버전 포트 및 가중치 binary와 혼용하지 마세요.

## 모델 실행

Python 3와 NumPy가 필요합니다. 아래 명령은 압축을 푼 `cnn_v4` 디렉터리에서 실행합니다. 기본 실행은 torch와 인터넷 다운로드를 사용하지 않습니다.

```bash
python model/cnn_model.py run --packed generated/weights_v4.bin --manifest generated/manifest.json --image validation/input_seed4.npy --out run_output --dump
python -m unittest discover -s model -v
python validate_package.py
```

입력 `.npy`는 uint8 RGB의 `(720,1280,3)` 또는 이미 전처리한 `(256,256,3)`입니다. 카메라 원본을 넣으면 행/열 stride5, 상하 56행 검정 padding을 적용하고 같은 원본에서 두 색상 마커를 계산합니다. 256×256 입력에는 색상 마커 결과가 없습니다. 결과는 `heatmap.npy`, `offset.npy`, `results.json`, 그리고 `--dump` 지정 시 각 연산의 출력입니다. OpenCV BGR 입력은 RGB로 바꿔야 합니다.

`results.json`의 각 관절은 `x`, `y`, `valid`, signed `score_raw`, 32bit `word`를 포함합니다. score는 확률이 아닙니다. 실제 robot 제어에서는 `valid=1`을 확인하고 좌표를 사용합니다. 이 CNN은 17개 신체 관절과 두 색상 마커를 대상으로 하며, 손가락 전체 관절이나 3D 깊이를 직접 출력하지 않습니다.

## 실제 모델 geometry 확인 및 재생성

기본 DW dilation=1은 기존 문서 기반의 **구현 baseline**입니다. 가중치 shape만으로 dilation/padding을 알 수 없으므로 원본 모델의 정확도 보장을 뜻하지 않습니다.

실제 학습에 사용한 posenet 소스/체크포인트 환경에서 다음을 실행합니다. 제공 스크립트는 그 환경의 `posenet.load_model(75)`를 사용하므로 다른 로더를 사용했다면 해당 줄을 실제 로더로 변경해야 합니다.

```bash
python model/export_geometry.py --out geometry.json
python model/cnn_model.py pack --weights source/weights_export.zip --geometry geometry.json --out generated
python build_spec.py
python finalize_docs.py
```

`build_spec.py`는 로컬 문서와 ROM/JSON/C header를 생성합니다. 노션 서버에 자동으로 쓰지 않으며 XLSX도 자동 갱신하지 않습니다. geometry/포트/레지스터를 바꾼 경우 변경된 JSON으로 XLSX와 노션도 같은 버전으로 갱신해야 합니다. 검증에 사용한 checkpoint가 실제 배포 가중치의 출처와 같은지 함께 확인합니다. conv0는 d1/pad1/stride2, DW는 d1/d2만 허용하며 shape와 ring buffer 조건이 맞지 않으면 pack이 실패합니다.

## RTL 대조 절차

1. 각 모듈의 valid/ready, mask/tag 및 reset/error 조건을 독립적으로 검사합니다.
2. 테스트벤치는 `valid && ready`인 출력만 수집합니다. stall 중 반복된 valid를 중복 기록하지 않습니다.
3. 전체 코어의 각 연산 결과를 `validation/features`와 같은 파일명, HWC shape 및 dtype의 `.npy`로 변환합니다. body/heatmap은 int8, offset은 int16입니다.
4. 아래 비교는 모든 원소가 정확히 같아야 통과합니다. 오차 허용치는 0입니다.

```bash
python model/compare_dumps.py validation/features rtl_dump
```

Python의 7개 단위 시험 및 전체 29개 층의 packed/원본 archive 경로 비교는 실행했습니다. 실제 RTL 출력이 없으므로 위 RTL 비교 명령의 통과 결과는 제공하지 않습니다. 합성 입력 시험은 신경망 정확도 시험이 아닙니다. 실제 영상의 관절 GT 성능, offset saturation 비율, threshold 변화 영향은 별도로 평가해야 합니다.

## 자원 및 성능 승인

CNN 구조 배정은 DSP 193개/RAMB36 83개입니다. CNN 상한은 DSP 196개/RAMB36 90개이며 영상과 DMA에는 DSP 16개/RAMB36 40개를 배정했습니다. 전체 Z7-20 한계인 DSP 220개/RAMB36 140개/LUT 53,200개를 넘지 않아야 합니다. RAMB18 두 개를 RAMB36 한 개로 환산합니다.

PW 128 MAC은 유지하고 PW 전체 레이어 가중치 이중 저장소를 단일 저장소로 바꿨습니다. 재양자화 DSP도 예산에 포함했고, bias 변환은 PC pack으로 옮겼습니다. LUT가 DSP로 추론되지 않도록 `use_dsp=no` 및 합성 결과를 확인합니다. BRAM은 용량뿐 아니라 실제 폭/포트/bank 기준으로 확인합니다.

시간 예산은 DW/PW 2 pixel cache 중첩을 전제로 약 57.04ms의 연산, 입력 여유 1ms, DDR payload 8,230,400bytes, 제어 여유 5ms입니다. 유효 DDR 300MB/s 가정에서는 약 90.48ms, 200MB/s에서는 약 104.19ms입니다. 가정에 따른 계산이며 10fps 실측 보장이 아닙니다. 카메라/HDMI 동시 부하에서 유효 대역폭과 CYCLE_COUNT를 측정해야 합니다.

Vivado routed design을 연 뒤 실제 CNN 계층 경로를 설정하고 아래 보조 검사를 실행합니다.

```tcl
set CNN_CELL design_1_i/cnn_accelerator_top_0/inst
source implementation/check_resources.tcl
```

예시 경로는 실제 BD에 맞춰 바꿉니다. 스크립트 자체는 여기서 Vivado로 실행하지 못했습니다. 출력 보고서에서 전체 Slice LUT(분산 RAM 포함), routing 완료, unconstrained path, DRC 및 CNN 100MHz 제약까지 확인해야 최종 승인할 수 있습니다.

## 주요 변경점

- 전체 가중치 이중 저장소 제거, PW 가중치 1024bit bank 구성과 배포 binary 배치 고정.
- DDR FM_A/B로 읽기와 쓰기 분리. heatmap/offset은 같은 conv13 출력을 각각 재사용.
- conv0 바깥 padding은 doubled raw 255로 처리. letterbox 검정은 raw 0 유지.
- RNE ties-to-even, body/heatmap INT8, offset INT16로 고정.
- raw threshold는 확률 0.10 경계에 대한 ceil 기준 -46으로 명시.
- AXI-Lite AW/W 독립 수락, 응답 유지, WSTRB, 접근 오류, 원자적 결과 갱신, frame ID와 IRQ/복구 규칙 추가.
- stage별 cfg barrier, heatmap 단계 완료, weight reload 금지 구간과 tag 변환 명시.

[수정된 프로젝트 노션](https://app.notion.com/p/3d9e5946183b815692cbec0376e53196)
