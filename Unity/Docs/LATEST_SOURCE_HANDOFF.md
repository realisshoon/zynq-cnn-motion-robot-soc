# 최신 Unity 소스 공유

## 공유 기준

- 브랜치: `dev/unity`.
- 부모 커밋: `f6224e7d8cf36d5345f21ad58ca87df0247e31c5`.
- 기준: **20261014_CAMERA_OVERLAY_TOGGLE_R1**의 검증된 전체 프로젝트. CSV Unified UART R1 통합 소스에 관절점 표시 토글을 추가한 버전이다.
- Unity: **6000.3.24f1** / Windows x64 / Mono / Development Build OFF.
- Scene: `Demo_07_SingleArmControl` enabled, `Main` disabled.
- Assets / Packages / ProjectSettings의 기준 파일 712개 중 개인용 `_Recovery` Scene 및 meta 3개를 제외한 **709개**를 공유한다. 실행 Scene과 무관한 복구 사본은 Git에 넣지 않는다. 기존 원격 파일은 삭제하지 않았다.
- `LATEST_SOURCE_MANIFEST.json`은 기준 스냅샷의 원시 바이트 SHA256이다. Git의 CRLF/LF 정규화가 적용된 텍스트 파일은 체크아웃 환경에 따라 바이트 해시가 달라질 수 있다. DLL 등 바이너리는 바이트를 그대로 보존한다.

## 이번 공유 작업에서 변경하지 않은 것

기준 프로젝트의 런타임 동작을 새로 개발하거나 펌웨어를 수정하지 않았다. 검증된 소스를 Git 프로젝트 구조로 반영하고 실행/빌드 안내와 기존 오프라인 테스트의 실행 진입점을 추가했다. 원본 `feat/jee_unity`의 미커밋 작업, index, HEAD 및 기존 빌드는 보존한다. `robot_arm/`, FPGA/Vitis, Native DLL 내용과 Servo Mapping/Pivot은 이번 공유 작업에서 변경하지 않는다. 기존 원격 대비 필요한 검증 DLL 추가는 포함한다.

## 기존 검증 기록과 이번 재검증의 구분

2026-10-14 기준 패키지의 기존 오프라인 검증은 **2,942 PASS**였다.

| 기존 검증 | PASS |
|---|---:|
| Gateway / overlay 순수 C# | 43 |
| Camera overlay Player | 96 |
| CSV 입력 owner / 포트 연결 분리 | 88 |
| RIGHT/LEFT/BOTH G51 시각화 | 168 |
| Quick HUD | 416 |
| Records KO UX | 244 |
| CSV playback | 156 |
| 단일 역할 CSV / 실제 G51 Transform | 106 |
| Studio UI / Startup / Camera fixture | 1,625 |

이번 공유용 재검증은 2026-10-16에 별도 작업본에서 수행했다. 새 Library Import 및 Windows x64 빌드 성공(오류 0, 기존 경고 4), 위 9개 테스트 묶음 **2,942 PASS**를 동일 소스로 다시 확인했다. SingleRole은 제공된 1,356개 샘플 파일의 27.1초 실제 Player 재생과 G51 Transform 변화를 포함한다. 실제 COM 열기 0, 실제 UART TX 0이다. [기계 판독 결과](LATEST_VALIDATION.json)를 함께 제공한다.

빌드 경고 4개는 기존 RuntimePipelineConfig 미지정(Pipeline 패키지) 경고 1개와 미사용 필드 CS0414 3개다. 기존 URP 렌더링 설정을 변경하지 않았다. 실제 OBS 영상 내용 및 실제 FPGA/서보 검증은 이 수치에 포함하지 않는다.

## 재실행 방법

순수 C# 43개 테스트는 .NET 8 SDK로 `Tools/CameraOverlayValidation`에서 실행한다. SDK가 없는 이 PC에서는 Unity에 포함된 Roslyn과 설치된 .NET 8 runtime으로 동일 소스를 컴파일해 확인했다.

Windows Player 오프라인 회귀는 저장소 루트에서 다음과 같이 실행한다.

```powershell
& .\Unity\Tools\ShareValidation\Invoke-PlayerChecks.ps1 `
  -PlayerExe 'D:\UnityBuilds\NewBuild\HumanMotionDigitalTwin.exe' `
  -OutputDirectory 'D:\UnityValidation\NewRun'
```

1,356개 샘플의 `basic_1_robot1_G51_R1.csv`를 가진 경우에만 `-SingleRoleCsv '실제 CSV 절대경로'`를 추가한다. 사용자 녹화 CSV는 Git에 추가하지 않았다. 선택 옵션을 생략하면 해당 실데이터 테스트를 실행하지 않는다. 각 fixture는 실제 COM 대신 in-memory transport를 사용한다. 일반 실행에서는 검증용 인자를 주지 않는다.

## 남은 실기 확인

- 다른 팀원 PC의 최초 Import / 실행은 별도 현장 확인 대상이다.
- 실제 OBS Stereo 영상 내용, 실제 HDMI 관절점 변경, 설치 BOOT 버전과 그리퍼 보정, 실제 서보 동작 및 물리적 출력 차단은 이번 Git 공유 검증 대상이 아니다.
- 화면의 Virtual 및 UART 연결 성공을 실물 PWM 차단이나 안전 보증으로 해석하지 않는다.
- `b`의 보드 응답은 설정 상태다. 실제 HDMI 표시에는 다음 CNN 결과가 필요하다.
