# 발표 미디어 출처와 README 검토

원본은 팀 제공 `3팀 최종 프로젝트 발표용 (1).pptx`이며 59페이지입니다. 표지의 발표일은 2026-10-15입니다. 원본 PPT는 225,265,957 bytes로, 저장소에는 추가하지 않았습니다. 원본 SHA256은 `66c32a34130ad0d9d17797a35c49e40ce4439c5dee5a42c2aab2351dbd7d01c3`입니다.

## 사용한 자료

| 파일 | PPT 페이지 | 추출 방식 | bytes | README 용도 |
|---|---:|---|---:|---|
| [demo-final-slide10.gif](demo-final-slide10.gif) | 10 | 내장 `ppt/media/image42.gif` 그대로 | 14,400,982 | 실제 최종 발표 시연 |
| [system-architecture-slide12.png](system-architecture-slide12.png) | 12 | 내장 `ppt/media/image45.png` 그대로 | 115,508 | 전체 시스템 개념도 |
| [cnn-pipeline-slide26.png](cnn-pipeline-slide26.png) | 26 | PowerPoint 원본 슬라이드 PNG Export | 305,755 | Downsample·InputConv·DW·PW·Heads |
| [uvm-verification-slide27.png](uvm-verification-slide27.png) | 27 | 내장 `ppt/media/image128.png` 그대로 | 65,383 | 발표 당시 UVM 검증 영역·결과 |
| [unity-digital-twin-slide35.gif](unity-digital-twin-slide35.gif) | 35 | 내장 `ppt/media/image202.gif` 그대로 | 4,697,900 | 실제 Unity 표시·관절 움직임 |
| [feature-map-dma-slide43.png](feature-map-dma-slide43.png) | 43 | PowerPoint 원본 슬라이드 PNG Export | 236,834 | 외부 DDR·Feature DMA 구조 |
| [stereo-depth-issue-slide49.png](stereo-depth-issue-slide49.png) | 49 | 내장 `ppt/media/image276.png` 그대로 | 541,728 | 단안 깊이 추정 오류의 원인 분석 |

합계 **20,364,090 bytes (20.36 MB / 약 19.42 MiB)**입니다. [MEDIA_SOURCES.json](MEDIA_SOURCES.json)에 각 파일의 SHA256·해상도·Frame·Timing·Loop를 기록했습니다.

## 추출·선별 원칙

5개 내장 미디어는 ZIP 원본과 바이트가 동일합니다. 26·43페이지는 텍스트와 개별 이미지가 결합되어 있어 읽기 전용으로 연 PowerPoint에서 **1600×900 PNG**로 슬라이드 전체를 내보냈습니다. 새 도형이나 구조도를 작성하지 않았고, 자르기·수치 수정·주석 합성도 하지 않았습니다. 원본 PPT는 저장하지 않고 닫았으며 최종 hash를 다시 확인했습니다.

59페이지의 텍스트·미디어 관계를 읽었습니다. PowerPoint로 내보내 시각적으로 검토한 페이지는 1·2·10·12·26~29·32~35·39~51입니다. 시스템·CNN·검증 우선 범위 12~29페이지와 Troubleshooting 39~51페이지의 원본 자료를 대조하고, 중복 설명·작은 장식·가독성이 낮은 개별 조각은 추가하지 않았습니다.

시연 GIF는 각각 150 frames, Frame당 40 ms, 합계 6초, Loop 0(반복)입니다. 크기·Palette·Frame Timing·Loop·압축 상태까지 원본 그대로 유지합니다. 원본 움직임을 보존하고 중복 시연을 제외하는 것으로 범위를 줄였습니다. **최적화 재인코딩은 하지 않았으며 별도의 최적화본도 없습니다.** 가장 큰 선택 GIF는 14.40 MB여서 50 MiB 경고·100 MiB 차단 대상에 해당하지 않습니다. 전체 PPT와 21~22 MB의 중복 로봇 GIF들은 추가하지 않았습니다.

## 사실관계와 확인 한계

- 10·35페이지 GIF는 발표 당시 실제 화면입니다. 이번 STEP 3의 실제 COM·FPGA·로봇 재실행 PASS를 뜻하지 않습니다.
- 27페이지 이미지의 “23가지 시나리오 PASS”는 원본 발표 표기입니다. 이미지와 의미를 바꾸지 않고 README 바로 아래에서 현재 통합 후보의 결과와 구분했습니다.
- 우선 검토한 27~29페이지는 UVM 구성·결과 요약도이며, raw simulator transcript·파형·Coverage Database 화면을 제공하는 자료는 아닙니다. 가상의 검증 화면을 만들지 않았습니다. 현재 실행 증거는 [STEP3_RESULTS.json](../../validation/step3/STEP3_RESULTS.json)과 로그에 연결했습니다.
- 44페이지 처리 시간과 47페이지 정확도 개선 수치는 현재 통합 후보에서 재측정하지 않았으므로 최신 성능·정확도 지표로 인용하지 않았습니다.
- Troubleshooting은 39~44페이지 모델·입력 크기, 41~43페이지 DDR·DMA, 48~51페이지 Stereo 문제를 선별했습니다. 발표 당시 해결 방향과 현재 소스/검증 범위를 표에서 구분했습니다.
- Contributors는 2페이지 CNN/Robot Team 7명의 담당표를 기준으로 작성했습니다. 개인 중심 소개를 추가하지 않았고 실명↔GitHub 계정을 추정해서 링크하지 않았습니다.
- README 실행 방법은 최신 `dev/integration` / `dev/unity` 소스와 STEP 3 로그를 따릅니다. 하위 README의 과거 branch 안내·검증 수치를 현재 통합 결과로 확대하지 않습니다.

## 렌더링·링크 검사

[README_VALIDATION.json](README_VALIDATION.json)에 상대 링크·Anchor·이미지 Decode·원본 GIF Frame Timing·로컬 Browser 렌더링의 실제 검사 결과를 기록합니다. 로컬 검토 HTML·스크린샷·Contact Sheet는 `.validation/step3/`에만 보존하며 Git에는 추가하지 않습니다. GitHub 서버에 업로드해 렌더링한 결과는 아니며, push·PR은 실행하지 않았습니다.


검사 결과: README 상대 링크·Anchor **39개 PASS**, media Decode·SHA **7개 PASS**, 두 GIF의 Frame·Timing·Loop 원본 일치 **PASS**. 로컬 Browser에서 이미지 7/7 로딩, H2 11개, 폭 591px / 본문 scroll width 591px을 확인했습니다. 외부 Contributors URL은 web 도구 cache miss로 live 응답 미확인입니다. README·미디어 전용 임시 폴더만 제공한 loopback 서버를 검토 후 종료했습니다.

종료 로그 재검토로 Feature Map TB를 FAIL로 정정한 최종 README(12 PASS / 3 FAIL / 1 BLOCKED)도 Browser에서 다시 확인했습니다. README의 Pointwise Fault 예제는 원문 PowerShell 블록을 그대로 실행해 정상 PASS·$finish를 확인했으며, 관련 로그 hash는 README_VALIDATION.json에 기록했습니다.
