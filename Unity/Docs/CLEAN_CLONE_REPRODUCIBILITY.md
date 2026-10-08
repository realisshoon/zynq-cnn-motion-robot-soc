# Clean Clone 재현성 및 Humanoid 추가 인계 검증

작성: 2026-10-08. Clean Clone 검증 후 사용자가 dev/unity Commit/Push를 승인했다. 이 문서는 해당 인계 커밋에 포함된다.

CLEAN CLONE VALIDATION = PASS

COMMIT / PUSH AUTHORIZATION = APPROVED

Clean Clone의 기능·빌드·실행 시험은 통과했다. **최신 원본 Editor에서 새 Humanoid Shoulder / BOTH UI를 직접 확인하는 항목은 아직 미검증**으로 기록한다. 원본 UI 자동화 helper가 초기화 중 종료되었고, 사용자는 “아직 확인하지 못함”으로 답했다. 이 항목을 PASS로 대체하지 않는다. 이전 UART 명칭 표시·START는 사용자가 확인했다.

## 작업공간·버전·선별 반영

- 전달 프로젝트 위치: 저장소의 `Unity/`. 원격 `dev/unity` SHA `30b060feb8ec3ef33e0d3d2432b465270a2f120f` 기준 별도 clone.
- 최신 원본은 별도 HumanMotionDigitalTwin 프로젝트로 유지했다. 승인된 UART 표시 문자열과 Shoulder/BOTH 관련 UI·visual 코드만 수정했으며 이동·삭제하지 않았다.
- 기존 Git 작업공간의 branch/HEAD/index/status는 보존했다. 로봇팀/CNN 파일 변경 0, 원격 전용 11개 보존, 원격 파일 삭제 0.
- 기존 424개 후보를 실제 파일 및 SHA256으로 재검토했다. 이후 사용자 승인 UI 변경, 재현성 수정, 문서/검증 코드가 추가되어 최종 Git diff 후보 수와 424는 다르다. 줄바꿈만 다른 파일은 Git diff에 나타나지 않는다.
- 전체 파일별 반영/제외 목록과 SHA256은 제출 밖 `files_to_commit.csv`, `424_candidate_review.csv`, `final_audit.json`에 보관한다. 빌드·Library·로그·캡처를 Git에 넣지 않는다.

## Import / Build Scene / URP

Unity 6000.3.24f1에서 새 Library로 Import했다. URP 17.3.0 유지, Demo07이 첫 enabled Build Scene이다. Main은 삭제하지 않고 Build Settings에서 disabled로 보존한다.

URP의 미해결 GUID 11개를 분석했다. DefaultVolumeProfile에 있던 9개 누락/Editor 시험 subasset(누락 script GUID 4개 및 fileID 0 시험 component)과 PC_Renderer의 obsolete probe resource GUID 7개를 정리했다. 정상 Volume component 19개와 SSAO는 보존했다. 패키지의 현재 probe resource 이동 계약을 확인했다. 전체 GUID 미해결 참조, missing script, 필수 meta 누락, 중복 GUID는 최종 검사에서 0이었다.

플랫폼/패키지 전체 변경, HDRP 전환, Robot material/mesh/pivot 변경은 하지 않았다. Unity Build가 갱신한 shader prefilter 직렬화 항목은 별도 diff에 기록했다.

## Native DLL 및 portability

| DLL | ABI | SHA256 |
|---|---:|---|
| control_studio_v2.dll | 2 | `236e05db7e386afdb79279df0d73c8abc41b1ece17873edddb3ec794d9ca9783` |
| control_studio_xyz.dll | 1 | `8d6af5f835d3be4513ad0e3f5a22a073b855901770594dbcd2fba85b578a789a` |

실제 경로는 `Assets/Plugins/x86_64/control_studio_v2.dll`, `Assets/Plugins/x86_64/control_studio_xyz.dll`이다. 기존 legacy `control_studio.dll`도 삭제하지 않았으며 Demo07의 ABI v2/XYZ 호출 대상과 구분한다. PE x86-64 / KERNEL32 / Windows UCRT 의존성을 검사했고 Editor와 실제 Windows Player에서 두 native context를 실행했다. 원본 DLL 바이트는 변경하지 않았다.

Native PowerShell 재빌드 스크립트는 `-RobotRoot` / `-Gcc` 입력으로 개발자 절대경로를 제거했다. 고정 C source revision 검사를 유지했으며 DLL 재빌드는 하지 않았다. 기존 Editor batch의 고정 절대경로/자동 실행 조건은 `ControlStudioBatchPaths`의 프로젝트 상대경로와 명시 `--isolated-validation`으로 바꿨다.

`System.IO.Ports.dll`은 Microsoft NuGet 6.0.0의 netstandard2.0 바이너리와 SHA256이 일치하며 MIT/third-party notices를 포함했다. 실제 COM 포트 실행 가능성은 시험하지 않았다.

## 실제 Humanoid 구조와 구현 경계

RightShoulderRoot local position `(0.43, 1.44, -0.20)`, LeftShoulderRoot `(-0.43, 1.44, -0.20)`이다. 각 root 아래 ArmSourceFrame_Demo01 → ShoulderYaw(Y) → ShoulderPitch(X) → ShoulderRoll(Z)가 있고 기존 rest quaternion에 delta를 곱한다. parent/localPosition은 변경하지 않았다.

- `HumanoidPreviewControl.cs`: shoulder Pitch 0~10°, Roll -8~8°와 LEFT 관절의 작은 시연 범위. 물리적 제한값이 아니라 **VISUAL ONLY** 범위다. 자세 보간은 Pause를 존중한다.
- `HumanoidServoPreviewPanel.cs`: SHOULDER/ARM JOINTS 독립 접기, R/L 값 동시 표시, 기존 RIGHT preset 범위를 명시한다.
- `RobotVisualProfiles.cs`: 기존 단일 visual 적용 위치에서 right approved Applied와 preview delta를 합친 target 및 left preview target을 전달한다.
- `RobotControlMockPanel.cs`: RIGHT/LEFT/BOTH 대상을 동일 크기로 표시하고 기존 독립 Mock 인스턴스를 유지한다. 선택만으로 명령이나 각도값을 변경하지 않는다.
- `ControlStudioRuntimeUI.cs`: 새 Humanoid 전용 패널을 부착한다. G51의 기존 5축 UI/listener는 그대로다.

RIGHT ARM 입력은 ManualServoSource → Agent2 승인/출력 → SingleArmCommandRouter → ApplyCommand → visual adapter다. LEFT/Shoulder는 이 native ABI에 채널을 추가하지 않는다. LEFT는 센서/UART 제어가 아니다. 실제 firmware ServoChannel은 여전히 5개다.

BOTH A 시험에서 RIGHT PWM ON / LEFT PWM OFF일 때 Accepted / Denied 및 PARTIAL을 확인했다. 결과 모델에는 Timeout 구분이 있으나 실제 timeout 응답/재시도는 구현하지 않았다. E/A/S/X/RGB 송신도 연결하지 않았다.

## 시험 결과

- 추가 Editor Play: SHOULDER/BOTH 294 PASS / 0 FAIL.
- 기존 Editor Mock 회귀: 242 PASS / 0 FAIL. 기존 150개 20ms Agent2 trace와 정확히 일치.
- 추가 Windows Player: SHOULDER/BOTH 294 PASS / 0 FAIL. 실제 EventSystem raycast/click/drag 및 1920×1080·1280×720 캡처.
- G51 독립 native Agent2 160개 틱 Applied trace 일치, Humanoid RIGHT/LEFT shoulder 축, BOTH의 명시 편집, LEFT 편집의 RIGHT 무영향, parent/localPosition 유지, Pause, Controls Hide, Presentation camera 유지 확인.
- 이전 Clean Clone Windows Player layout 279 PASS / 0 FAIL 및 양 CSV XYZ 상태 검증은 보존했다. 최종 빌드의 기존 Player 회귀 252 PASS / 0 FAIL 및 두 CSV XYZ 시험 308 PASS / 0 FAIL도 확인했다.
- Windows x64 Build: errors=0, warnings=4. URP shadow atlas 자동 축소와 기존 미사용 필드 경고는 오류와 구분한다. batch 종료 시 Unity 임시 allocator 진단 로그가 있으며 Player 앱 예외와 혼동하지 않는다.

첫 Player 어깨 시험은 보간 완료를 고정 wall-time으로 가정하고 엄격한 float equality를 사용해 실패했다. 실제 pose 도달을 기다리고 float 회전 비교 허용오차를 둔 fixture로 수정한 뒤 통과했다. 기능 코드/물리 보정값을 수치 맞추기 위해 변경하지 않았다. 실패 로그도 삭제하지 않았다.

OS 마우스 자동화가 아니라 실행 중 Unity Player의 EventSystem 입력이다. 실제 보드/HDMI 연결 시험은 아니다. 원본 사용자 Editor 세션과 별도 clone Editor/Player를 구분한다.

## UI·시각 검수

실제 Player의 compact/shoulder/right/left/both/Camera/Controls Hide/Presentation 캡처를 확인했다. 두 해상도에서 패널 겹침·화면 밖 잘림이 없고, 기존 고품질 Humanoid와 Simulation Background를 유지했다. 시험 자세에서 mesh 분리 없이 팔이 따라갔다.

시험한 working pose의 양측 ToolMount 간격은 0.15 scene unit보다 컸다. 전 관절 조합의 자기충돌 방지, 물리적 cable/grasp, 하드웨어 shoulder limit 검증은 하지 않았다. 이 특정 자세 검사를 전 범위 안전성 PASS로 확대하지 않는다.

## 빌드·증거 보관 및 팀 재검증

검증 결과물은 Git 밖 별도 evidence의 `ShoulderFinal/Windows/SingleArmControl.exe` 전체 폴더에 있다. 같은 방식의 팀 재현 기본 출력은 `Unity/Validation/HandoffRepro/Windows/SingleArmControl.exe`다. exe만 배포하지 말고 `_Data`, UnityPlayer.dll 등 전체 폴더를 함께 전달한다.

실행 검증은 `--shoulder-verify <output>` 또는 `--handoff-verify <output>`을 명시한 경우에만 동작한다. 평상시 실행은 자동 관절시험/Mock 조작을 시작하지 않는다. Editor용 `HumanoidShoulderBatch.Run`은 분리된 clone과 `--isolated-validation`에서만 실행한다.

인수인계서 3개와 본 문서의 상대 링크를 검사했다. 위 원격 기준 SHA는 통합 이전 부모 커밋이다. 실제 전달 SHA는 이 문서를 포함한 커밋의 Git 이력 또는 `git rev-parse HEAD`로 확인한다.

## 남은 항목

1. 최신 원본 Editor에서 Humanoid 어깨 / RIGHT·LEFT·BOTH 표시와 조작 직접 확인. 사용자는 아직 확인하지 못함. UART 표시/START 확인은 이미 완료.
2. 다른 팀원 Windows PC의 Import/실행.
3. 실제 UART 보드 상태/TK/V/T/E/A/S/X/RGBY, COM별 timeout, HDMI Preview, 실물 양팔/서보 안전성.
4. Source revision/filter/zero 차이에서 유래한 기존 16.41° wrist 차이. 이번 UI 작업으로 해결했다고 보고하지 않는다.

이후 사용자가 업로드를 승인했다. 최신 원본과 실행 코드의 일치, 원격 dev/unity 변경 없음, 검증된 Player 소스의 일치를 마지막 확인한 뒤 인계한다. 원본 직접 조작 미검증을 PASS로 바꾸지는 않는다.
