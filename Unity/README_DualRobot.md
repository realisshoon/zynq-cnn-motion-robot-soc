# G51 Dual Robot Digital Twin

기존 G51 `Main.unity`를 복제한 두 팔이 빨간 공을 전달하는 발표용 Scene이다. 기본 Build 결과는 **중앙 수직 기둥 + 상부 수평 빔 + 좌우 mounting bracket의 T-frame**이다. 로봇 root만 아래쪽·중앙을 향하도록 장착하며, 기존 관절 축·방향·rest rotation·G51 mesh/material·linkage 알고리즘을 유지한다. 실제 로봇의 충돌 회피나 물리 파지 검증을 의미하지 않는다.

## 1. 확인한 기존 구조

실제 pivot 경로는 `RobotArm/Base_Yaw/Shoulder_Pitch/Elbow_Pitch/Wrist_Pitch/Wrist_Roll/Gripper`다. `BaseLink`, `UpperArm`, `ForeArm`, `WristLink`, `Hand`는 각각 해당 pivot의 시각화 자식이다. 다섯 축은 Y/X/X/X/Y, direction=+1, neutral=90°, rest rotation=identity였다.

현재 Scene geometry는 baseHeight=0.08, shoulderOffset=0.04, upperArmLength=0.15, forearmLength=0.13, wristLength=0.06이다. 이 수치는 저장된 visual 설정이며 G51 실측값이라고 주장하지 않는다.

`RobotArmController`, `UdpJointCommandReceiver`, `JointCommandData`, `G51GripperVisual`은 `Assets/Scripts`에 있다. 명령 API는 `bool RobotArmController.ApplyCommand(JointCommandData)`이며 유효하지 않은 입력은 HOLD한다. Manual 모드는 `testCommand`를 적용한다.

Gripper는 Wrist_Roll 아래 local Y=0.05, scale=0.16이며 `Gear_L/R`, `Finger_L/R`, `TopLink_L/R`와 pad/anchor를 갖는다. `__G51V2_*` servo/bracket/link 및 `__G51V3_Bolt_*` 8개를 그대로 복제한다. Main Scene 바깥의 v3 조명 3개는 복제 대상 로봇에 포함되지 않는다.

작업 시작 때 미저장이던 G51 Scene은 기존 디스크본을 `Validation/Main.before-dual-disk.unity.txt`에 보존한 뒤 저장했다. 저장된 G51 원본은 `Validation/Main.G51-preserved.unity.txt`에도 보존했다. 그 이후 Main 및 핵심 8개 스크립트는 SHA256 비교로 보호한다.

## 2. 추가 파일과 역할

| 파일 | 역할 |
|---|---|
| `Assets/Scenes/DualRobotDemo.unity` | 발표용 별도 Scene |
| `Assets/Editor/DualRobotDemoBuilder.cs` | 저장된 Main을 preview Scene에서 읽어 복제, 내부 참조 검사, 배치·공·frame·camera 생성 |
| `Assets/Scripts/DualRobot/DualArmDemoController.cs` | scripted 상태 전이 / 오른팔 CSV 모드 / 명령 pose 보간 |
| `Assets/Scripts/DualRobot/BallTransferController.cs` | 단일 ownership과 world pose를 보존하는 parent 전환 |
| `Assets/Scripts/DualRobot/DualArmStatusUI.cs` | Mode, 양팔 상태, owner, UDP frame, CSV 수신 상태, Start/Reset |
| `Assets/Editor/DualRobotValidation.cs` | 실제 Play에서 명령·복제·파지·handoff·UDP 회귀 검사 |
| `Assets/Materials/DualRobot/` | 공과 중앙 frame 전용 material |
| `Tools/AgentReplay/build_canonical_replay.py` | 고정 Git blob을 읽고 Unity 내부에서 실제 C source 빌드 |
| `Tools/AgentReplay/verify_replay.py` | 실제 50 Hz 송신과 Unity telemetry를 canonical trace 행과 비교 |
| `Tools/verify_dual_integrity.py` | Main/core/external 파일 SHA256 및 외부 HEAD 비교 |

기존 `Tools/AgentReplay/csv_pose_to_joint_trace.c`, `send_joint_trace_udp.py`, 입력 CSV는 재사용했다. 기존 두 PowerShell build 진입점은 외부 저장소에서 생성물을 만들지 않도록 새 Python builder를 호출하게 변경했다.

## 3. Scene 생성과 실행

1. Unity에서 현재 편집 Scene을 저장한다.
2. `Tools → Human Motion → Dual Robot Demo → Build or Rebuild`를 누른다. 단축키는 Ctrl+Shift+F9다. 매번 저장된 Main을 기준으로 Dual Scene을 재생성하므로 Dual Scene에서 수동 편집한 내용은 재생성 때 대체된다.
3. `DualRobotDemo`에서 Play하고 Game 화면의 `START DEMO`를 누른다. Idle 상태에서 Space도 시작한다. 진행 중 또는 완료 후 Space는 Reset한다.
4. 왼팔 접근 → 잡기 → 중앙 이동 → 오른팔 접근 → 잡기 → ownership 전달 → 왼팔 열기 → 오른팔 이동 → Done 순서로 진행한다.
5. `RESET`은 양팔 초기 command와 공의 위치·scale·ownership을 복구한다.

`DualArmDemoController` Inspector에서 여섯 pose와 move/grip 시간을 변경할 수 있다. pose 변경 후 pick/handoff 위치가 맞지 않으면 순간 이동시키지 않고 Failed로 정지한다. `BallTransferController`의 ballRadius는 초기 0.02 m이고 Reset 시 반영된다. 기본 holdOpening≈0.622는 현재 visual의 pad 간격과 직경 0.04 m를 맞춘 값이다. 크기/geometry 변경 시 GrabPoint·holdOpening·pose를 함께 맞춰야 한다.

양팔은 positive scale과 root 위치/회전으로 배치된다. 기존 controller의 Manual 경로가 scripted 명령을 덮어쓰지 않도록 같은 `testCommand`도 갱신한다. 실제 pivot 회전은 controller만 수행한다. 오른팔 receiver만 존재하며 scripted 모드에서는 비활성화된다.

`CenterFrame/VerticalColumn`, `HorizontalBeam`은 silver aluminium extrusion visual이고, `LeftMount`/`RightMount`는 black bracket이다. mount 높이는 0.78 m이며 frame은 workspace 뒤쪽 Z=0.20 m에 둔다. 두 root는 mount 위치와 일치한다. root rotation은 좌우 각각 Euler(0,90,180)/(0,-90,180)이며 negative scale을 사용하지 않는다. `HandoffPosition`은 빔 아래 중앙, 약 Y=0.451 m이다. 공은 왼쪽 아래에서 잡아 중앙으로 운반한 뒤 오른쪽으로 이동한다. 정면에 가까운 카메라가 기둥·빔·양팔·공을 모두 담는다.

handoff command는 왼팔 B/S/E/WP/WR=90/140/70/150/90°, 오른팔=90/140/70/150/180°다. 양쪽 손목 중심은 각자의 작업 영역에 남고 손가락 평면을 90° 다르게 둔다. 중앙 기둥은 그리퍼 뒤에 떨어져 있다. 이는 시연 pose의 배치 기준이며 전체 mesh 충돌 및 실물 간섭 검증을 대신하지 않는다.

공은 물리 마찰에 의존하지 않는다. 오른팔이 닫힌 뒤 capture 거리를 검사하고 `SetParent(rightGrab, true)`로 world pose/scale을 보존한다. 이후 왼팔을 연다. Owner는 None/HeldByLeft/HeldByRight 중 하나만 갖는다. CenterFrame에는 제어 기능과 collider가 없다.

## 4. CSV → 실제 Agent1/Agent2 → UDP

사용 CSV는 1280×720, top-left 원점의 HumanPose2D다. C#에는 pose mapping을 추가하지 않았다. canonical 활성 팔이 `POSE_ARM_RIGHT`이므로 RobotArm_R만 CSV로 제어한다. RobotArm_L은 Start를 누르면 scripted pick/carry 후 전달점에서 HOLD한다. CSV 궤적은 handoff를 보장하지 않으므로 CSV 모드의 자동 ownership 전달은 수행하지 않는다. 완전한 handoff 시연은 Scripted 모드에서 실행한다.

현재 외부 checkout HEAD는 `905b943dcf323a96bb19a5515316fa23842264b7`이며 integration 파일이 없다. 로컬 Git에 이미 존재하는 `origin/dev/robot`의 commit `e9080f68f8ed37615f7aec0be5672a096bd24e5e`를 고정 사용했다. fetch/checkout/branch 변경 없이 `git show`로 C/header를 읽어 `Tools/AgentReplay/build/canonical/<SHA>/`에 저장한다. 원본 blob의 SHA256은 `build/canonical_manifest.json`에 기록한다. 외부 저장소에는 출력하지 않는다.

실행 PowerShell:

```powershell
Set-Location 'D:\working\final_project\Unity\HumanMotionDigitalTwin'
.\Tools\AgentReplay\build_and_convert_external_repo.ps1
```

Python이 PATH에 없으면 위 스크립트가 현재 설치된 Codex runtime을 찾는다. 다른 PC는 `-Python 'Python.exe 전체 경로'`를 지정한다. C compiler는 MinGW/MSYS2 GCC가 필요하다. 출력은 항상 Unity 내부 `Tools/AgentReplay/build/joint_command_trace.csv`다.

이후 Unity Play → `CSV Right Arm` → 다음 명령을 실행한다. 현재 PC의 명시적 Python 경로 예시:

```powershell
& 'C:\Users\kccistc\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' `
  .\Tools\AgentReplay\send_joint_trace_udp.py --csv .\Tools\AgentReplay\build\joint_command_trace.csv
```

다시 재생할 때 Unity `RESET` 또는 `CSV Right Arm`을 먼저 눌러 receiver sequence를 초기화한다. 연속 반복은 sender의 `--loop`를 사용한다. 수신 주소는 127.0.0.1:5005이며 기존 8필드 JSON schema를 그대로 사용한다.

변환은 실제 `agent1_run → agent2_run → 20 ms agent2_tick → pipeline.output`을 호출한다. accepted target인 `pipeline.command`를 전송하지 않는다. `source_frame_id`는 trace에 보존하고, UDP `frame_id`는 tick마다 증가시킨다. receiver는 기존 최신 명령 mailbox를 유지하며 CSV output에 Unity 보간을 추가하지 않는다. Unity 렌더링이 50 Hz보다 느리면 중간 tick은 생략되고 최신 command가 표시될 수 있다.

제공 CSV 522행에서 canonical 경로가 accepted/held로 반환한 것은 49행, target 부재/거절은 473행이었다. 결과는 0초 home을 포함해 1,354행, 마지막 시각 27.06초다. 잘못된 입력을 임의로 보정해 움직임을 만들어 내지 않는다. 실제 하드웨어와 같음을 확정하려면 해당 firmware commit/config 및 실측 축 검증이 추가로 필요하다.

## 5. 재현 가능한 검증

1. Dual Scene에서 `Tools → Human Motion → Dual Robot Demo → Run Validation (Play Mode)` 또는 Ctrl+Shift+F10을 실행한다.
2. 전체 scripted/UDP 검사가 끝나면 CSV 대기 상태로 전환된다. `Validation/DualRobotValidation.md`를 확인한다.
3. 그 Play 상태에서 `Tools/AgentReplay/verify_replay.py`를 Python으로 실행한다. 약 27초 동안 실제 sender를 구동하고 command 6개 필드·frame·오른팔 Base·왼팔 독립성을 검사한다. 결과는 `Validation/CsvReplayValidation.md`다.
4. Play를 종료하면 port 재bind 검사도 기록한다.
5. `Tools/verify_dual_integrity.py`를 실행하면 원본 보존 결과를 `Validation/DualIntegrityValidation.md`에 기록한다.

기존 `Validate Robot Arm` 메뉴는 초기 primitive 15-node 구조용 검사다. G51 단계의 회귀 결과는 새 Dual validation을 기준으로 본다. 기존 단일 팔 UI/UDP 경로는 Main에서 그대로 사용할 수 있다. Main의 Input Mode를 직접 UDP로 바꾸면 기존 receiver가 시작된다. geometry를 보존하려면 UDP 전환만을 위해 기존 Build/Apply Geometry 메뉴를 다시 실행할 필요는 없다.

## 6. 미확정 항목과 다음 단계

실측 링크 길이, 실제 서보 방향/offset, 두 팔 간 충돌·힘·마찰, 기구 연결 치수는 미확정이다. 현재 scope는 결정적으로 재현되는 visual handoff다. 왼팔 live mapping은 left elbow/wrist/finger 데이터가 추가된 뒤 canonical 입력 경로와 함께 확장한다. 제공 CSV의 valid/reject 사유 분석과 펌웨어 output 대조가 다음 검증 단계다.
