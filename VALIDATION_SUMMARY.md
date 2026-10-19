# STEP 3 검증 요약

기준 source commit: `b7eb63cb6b79a5b22a6e6b6e9fceb70eec67aa46`.
실행일: 2026-10-19 (Asia/Seoul). 기존 7개 source tree를 다시 통합하지 않았다.

**소스 통합 Draft PR 준비: PASS. 실제 시스템 호환성·최종 merge/릴리스 준비: BLOCKED.**

기능·ID·register·Golden 기준·assertion·timeout을 수정하지 않았다. 미해결 문제를 공개하는 Draft 준비와 실행 호환성 승인을 구분한다. push/PR/main merge는 수행하지 않았다.

## 실제 결과

| 영역 | 결과 | 근거와 범위 |
|---|---|---|
| Git 업로드 크기 | PASS | 현재 이력, 4개 source 이력, 현재 원격 36개 branch 이력 모두 50 MiB/100 MiB 초과 blob 0개. 가장 큰 파일 30,664,242 bytes. LFS 포인터 없음 |
| RTL 전체 Verilog compile | PASS | XSim 2020.2 xvlog로 원본 RTL·TB 파싱 |
| Directed TB 16개 | 12 PASS / 3 FAIL / 1 BLOCKED | 아래 개별 결과. FAIL은 argmax 모듈명 차이, color marker 검사 실패·wall timeout, Feature Map TB 원본 2ms timeout |
| downsample 추가 Golden 모드 | BLOCKED | 기본 directed 모드는 PASS. 추가 +GOLDEN 입력 hex 2개 누락 |
| Packed Python Golden | PASS | Python 3.10/NumPy 1.24.4, frame 121~125 관절 동일·heatmap/offset 원소 차이 0 |
| ZIP Golden | FAIL | 같은 고정 환경에서 기존 `version/hash mismatch` 재현 |
| 제공된 weight/image converter | PASS | 원본 변환기를 scratch 출력으로 실행. 누락 expected를 작성한 것이 아님 |
| UVM Common | PASS (제한된 smoke) | compile/elaboration 및 `cnn_base_test` 실행, UVM_ERROR=0/UVM_FATAL=0. time 0 초기화 smoke이며 CNN 수치 검증은 아님 |
| UVM Control | FAIL / 회귀 BLOCKED | XSim이 원본 `checker` 식별자에서 compile 오류 |
| UVM DMA-Stream | compile PASS / elaboration FAIL / 회귀 BLOCKED | XSim 내부 EXCEPTION_ACCESS_VIOLATION. 일부 cover 미지원 |
| UVM Golden | compile/elaboration PASS / 회귀 BLOCKED | final_expected.hex 등 완전한 입력 집합 누락 |
| VCS·Verdi 원본 회귀·coverage | NOT RUN | 현재 Windows PATH에 도구 없음. XSim 대체 결과로 VCS coverage 승인을 선언하지 않음 |
| 표준 Vitis RIGHT/LEFT 스크립트 | BLOCKED | IDE 연결 timeout 뒤 앱 .cproject 미생성. `[OK]` 표시를 성공 근거로 사용하지 않음 |
| Native HSI BSP 생성·컴파일 | PASS | 원본 final_uart0.xsa, custom driver, xilffs 4.4, libxil.a 생성 확인 |
| Vitis ARM GCC 직접 RIGHT/LEFT 빌드 | PASS | 역할별 원본 C 72개, 원본과 같은 역할 심볼·include 및 HSI BSP로 compile/link. 소스 불변·ELF 존재·ARM 형식·DDR 상한 확인 |
| RTL/Firmware ID·RGBY map | BLOCKED | 정적 불일치 유지. ELF 빌드 PASS와 runtime 호환성은 별개 |
| Unity Import/Windows x64 Build | PASS | 6000.3.24f1, Demo_07, Development OFF, 오류 0·경고 4 |
| Unity Player 오프라인 fixture | PASS | 7개 묶음 2793 검사 |
| 순수 C# gateway/overlay fixture | PASS | 43 검사. net8.0 대상 빌드 후 설치된 .NET 10.0.12로 명시적 roll-forward 실행 |
| Native DLL 로딩·ABI/role | PASS | RIGHT ABI3/role2, LEFT ABI3/role1, XYZ ABI1. 재빌드 재현성은 BLOCKED |
| 외부 SingleRole CSV fixture | NOT RUN | 선택 옵션의 외부 1356-sample CSV 미제공. 이전 검증 수치를 재사용하지 않음 |
| 실제 COM/FPGA/로봇·설치 BOOT | NOT RUN | 물리 UART 송신·프로그램·구동을 수행하지 않음 |

## Directed TB 개별 결과

| TB | compile | elaboration | simulation | 종합 |
|---|---|---|---|---|
| `tb_cnn_accelerator_top_reset_idle` | PASS | PASS | PASS | PASS |
| `tb_argmax_threshold` | PASS | FAIL | BLOCKED | FAIL |
| `tb_cnn_accelerator_top_partial` | PASS | PASS | PASS | PASS |
| `tb_cnn_accelerator_top_reset4` | PASS | PASS | PASS | PASS |
| `tb_color_marker_detect` | PASS | PASS | FAIL | FAIL |
| `tb_coord_restore` | PASS | PASS | PASS | PASS |
| `tb_depthwise_conv_pe` | PASS | PASS | BLOCKED | BLOCKED |
| `tb_depthwise_conv_pe_full_layer` | PASS | PASS | PASS | PASS |
| `tb_downsample_module` | PASS | PASS | PASS | PASS |
| `tb_feature_map_io` | PASS | PASS | FAIL | FAIL |
| `tb_input_conv_pe` | PASS | PASS | PASS | PASS |
| `tb_layer_param_rom` | PASS | PASS | PASS | PASS |
| `tb_pointwise_conv_pe` | PASS | PASS | PASS | PASS |
| `tb_pointwise_conv_pe_fault` | PASS | PASS | PASS | PASS |
| `tb_top_level_fsm` | PASS | PASS | PASS | PASS |
| `tb_weight_bram_swap_fsm` | PASS | PASS | PASS | PASS |

Feature Map은 `TEST O`에서 진행 후 원본 2ms timeout의 `TIMEOUT PASS=200110 FAIL=0`과 `$stop`으로 종료됐습니다. 정상 완료 `REGRESSION ... DONE_PULSES=12` / `$finish`가 없으므로 FAIL입니다. 초기 보고의 중간 PASS 문자열·exit 0 기반 오분류를 정정했고, 문서 예제 재실행에서도 같은 결과를 재현했습니다. 다른 Directed TB 12개의 정상 성공 marker·종료도 다시 확인했습니다. 원본 소스와 timeout은 변경하지 않았습니다.

Color marker는 실제 검사 실패 후 600초 wall guard에서 종료됐으며 정리까지 616.8초가 걸렸다. 원본 SV timeout을 바꾸지 않았다. 다른 FAIL을 숨기기 위해 assertion·입력·expected를 바꾸지 않았다.

XSim의 초기 timescale 누락은 원본 VCS 실행기와 같은 기본 `1ns/1ps`를 CLI에 제공해 재시도했다. Windows wrapper의 `=` plusarg 파싱 오류는 동일 UVM_TESTNAME을 옵션 파일에 넣어 해결했다. RANDC→RAND, cover 미지원 경고가 있으므로 random 분포·coverage의 동등성은 승인하지 않는다.

## Firmware 빌드 근거

표준 IDE 경로의 실패는 그대로 보존했다. 대체 경로는 Vitis 2020.2의 native HSI `add_library/generate_bsp`와 동일 배포판 ARM GCC를 사용한다. HSI wrapper의 exit 0만으로 성공 판정하지 않고 완료 marker와 libxil.a를 확인했다. 생성된 linker는 원본 setup 스크립트와 같은 DDR 상한 `0x0A000000`을 사용한다. CPU 진입점은 `0x100000`이다. BOOT 생성·SD 설치는 하지 않았다.

| 역할 | C 소스 | ELF bytes | ELF SHA256 | load 끝 주소 |
|---|---:|---:|---|---|
| RIGHT | 72 | 2073900 | `6db67624d4f4cc39e358f1dbe6b9d65a96c07927b909f3a49c61817bac23e65e` | `0x458510` |
| LEFT | 72 | 2065968 | `b7f6bd8af370e4b5f42203862505d96316b497b303cfd0990e82ef5e2f2aa07b` | `0x458b70` |

둘 다 compiler warning 0개이고 load segment가 CNN 예약 DDR 아래에 있다. 생성 ELF는 로컬 검증 산출물로 보존하며 Git에 추가하지 않는다. 표준 `setup_vitis.ps1` 자동화 성공이나 물리 실행 PASS로 확대하지 않는다.

## Unity 범위

Player fixture 7개는 CameraOverlay 96, PortOwner 88, G51 168, QuickHUD 416, Records 244, CSV 156, Studio 1625개 검사다. 순수 C# 43개와 합계 **2836개**다. 선택 옵션의 SingleRole 106개 검사는 NOT RUN이므로 과거 문서의 2942개를 이번 PASS 수로 기록하지 않는다. 실제 COM과 UART TX는 요청하지 않았다.

## Git 이력 보존 분석과 권고

현재 snapshot commit은 원본 source commit 이력을 main의 조상으로 연결하지 않았다. 승인된 4개 source 이력에서 **168개**, 그 밖에 squash로 분리된 원격 feature 원본 **102개**, 합계 **270개**가 현재 통합 후보의 조상이 아니다.

- 4개 source 이력만 연결할 때의 비중복 tip은 UVM `eb621949...`, Firmware `43895a84...`, Unity `86e08ecc...` 3개다. CNN tip은 UVM tip의 조상이다.
- 현재 원격 36개 branch의 원본 SHA까지 모두 main 조상으로 보존하려면 `git merge-base --independent`로 확인한 **18개 tip**을 고려해야 한다. 전체 SHA와 작성자 통계는 [이력 JSON](validation/step3/STEP3_HISTORY.json)에 있다.
- 권고는 기존 commit을 재작성하지 않는 후속 `git merge -s ours --no-ff` provenance merge다. 이 전략은 현재 tree를 유지하며 parent 이력만 연결한다. 실행 전후 tree와 270개 commit의 조상 관계를 확인해야 한다. `-X ours`와 구분한다.
- 전체 브랜치를 병합한 것으로 기록하므로 이후 merge-base에 영향을 준다. 최종 tree에서 제외한 과거 파일도 이력에는 남는다. 이 의미를 승인한 뒤 적용해야 한다. 이번에는 merge를 실행하지 않았다.
- 최종 GitHub PR은 저장소 정책이 허용하는 **Create a merge commit** 방식이 필요하다. Squash/Rebase 방식은 원하는 원본 조상 관계를 보존하지 않는다. 정책을 우회하거나 설정을 변경하지 않는다. 모든 원본 feature까지 main ancestry에 연결할 필요가 없다면 source tip 연결과 별도 보존 ref/archive를 선택할 수 있으나, 후자는 main 조상 보존과 다르다.

근거: [Git ours 전략](https://git-scm.com/docs/merge-strategies), [GitHub merge 방식](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/configuring-pull-request-merges/about-merge-methods-on-github).

## 업로드와 관리 변경

[업로드 audit](validation/step3/STEP3_UPLOAD_AUDIT.json)은 snapshot과 원본 이력을 모두 검사했다. [GitHub 공식 기준](https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-large-files-on-github)의 50 MiB 경고·100 MiB 차단 대상이 없다. 약 1.10 GB의 선택 데이터는 그대로 유지했고 LFS 변환이나 이력 재작성은 하지 않았다.

추적 cache `.pyc` 2개와 `gcc_err.txt` 1개만 `git rm --cached`로 추적 해제하고 디스크에는 보존했다. source .gitignore는 건드리지 않고 루트 규칙으로 검증 산출물을 제외한다. HSI가 XSA 옆에 생성한 untracked 추출물은 추적 여부·경로를 확인하고 `.validation/step3/xsa_extracted/`로 이동해 보존했다. DLL/XSA/BOOT/가중치/입력 데이터는 제외하지 않았다. 기존 Unity AgentReplay/DualReplay 보조 스크립트 3개에는 작업 PC RepoRoot 기본 경로가 남아 있으므로 실행 시 명시적으로 재지정해야 한다. 해당 원본 도구는 이번에 수정하지 않았다.

## 재현 명령과 증거

[STEP3_RESULTS.json](validation/step3/STEP3_RESULTS.json)에 실행 argv, 상태, 종료 코드, 경과 시간, 로그 SHA256과 증거 경로를 기록했다. 공유 파일에서는 사용자 경로를 `$REPO`, `$USERPROFILE`, `$BUNDLED_PYTHON`으로 치환했다. 원본 전체 로그·fixture 결과·생성 입력·ELF·Player는 로컬 `.validation/step3/`와 별도 Vitis workspace에 보존했다. 공유용 발췌는 생략 여부를 표시하며 원본 로그 hash와 구분한다.

대표 증거:

- [uvm_common_compile](validation/step3/evidence/uvm_common_compile.txt)
- [uvm_common_elab_default_timescale](validation/step3/evidence/uvm_common_elab_default_timescale.txt)
- [uvm_common_optionfile_cnn_base_test](validation/step3/evidence/uvm_common_optionfile_cnn_base_test.txt)
- [uvm_control_compile](validation/step3/evidence/uvm_control_compile.txt)
- [uvm_dma_stream_compile](validation/step3/evidence/uvm_dma_stream_compile.txt)
- [uvm_dma_stream_elab_default_timescale](validation/step3/evidence/uvm_dma_stream_elab_default_timescale.txt)
- [uvm_golden_compile](validation/step3/evidence/uvm_golden_compile.txt)
- [uvm_golden_elab_default_timescale](validation/step3/evidence/uvm_golden_elab_default_timescale.txt)
- [golden_pinned_archive](validation/step3/evidence/golden_pinned_archive.txt)
- [vitis_right_pwsh](validation/step3/evidence/vitis_right_pwsh.txt)
- [vitis_left_pwsh](validation/step3/evidence/vitis_left_pwsh.txt)
- [vitis_hsi_native_bsp](validation/step3/evidence/vitis_hsi_native_bsp.txt)
- [firmware_hsi_direct_right](validation/step3/evidence/firmware_hsi_direct_right.txt)
- [firmware_hsi_direct_left](validation/step3/evidence/firmware_hsi_direct_left.txt)
- [unity_import_build](validation/step3/evidence/unity_import_build.txt)
- [unity_offline_player_checks](validation/step3/evidence/unity_offline_player_checks.txt)
- [dotnet_gateway_43](validation/step3/evidence/dotnet_gateway_43.txt)

## 판정과 후속 단계

소스 통합과 문서·증거를 검토하는 Draft PR은 준비 가능하다. Git 이력 연결 범위를 결정하고 미해결 호환성 및 검증 환경 문제를 해결하기 전에는 최종 merge·실제 시스템 릴리스 승인을 권고하지 않는다. [미해결 문제](INTEGRATION_BLOCKERS.md)를 유지한다.

이번 단계는 별도 로컬 문서·Git 관리 commit까지만 수행한다. source 구현과 기존 commit은 재작성하지 않는다.

공식 팀 프로젝트 README·발표 미디어는 검증·Git 관리 변경과 분리한 로컬 commit으로 기록한다. 사용자 추가 요구에 따라 개인 포트폴리오 지시를 대체했다. PPT 10페이지 시연 GIF 등 7개 미디어(20,364,090 bytes)를 사용하며, Contributors는 발표자료 2페이지의 7명 담당표에 근거한다. 슬라이드·hash·원본 움직임 보존 여부는 docs/media/에 기록했다. 발표 당시 결과를 현재 통합 검증 PASS로 확대하지 않았다.
