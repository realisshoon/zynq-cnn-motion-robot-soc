# STEP 1 — CSV → canonical Robot pipeline → 동일 final output

STEP 1 RESULT = PASS

실제 C pipeline, Agent3/PWM host 경로, Single Arm Main의 실제 UDP 수신·관절 회전까지 실행했다. 실제 보드 PWM 핀과 서보 운동은 hardware unavailable로 미검증이다. 상세 결과는 `../../Validation/Step1FinalReport.md`에 있다.

## 데이터 경로

```text
example_pose2d_1280x720_20hz.csv (522 poses)
  → HumanPose2D (RIGHT ARM, 1280×720 pixels)
  → agent1_run → agent2_run (입력 frame마다)
  → agent2_tick (20 ms)
  → ctx.output ┬→ agent3_run → output_control_update → servo_control_convert
              │               → servo_hal_apply → servo_pwm_driver host mock
              └→ joint_command_trace.csv → send_joint_trace_udp.py
                                          → UdpJointCommandReceiver
                                          → RobotArmController.ApplyCommand
```

`ctx.command`는 accepted target이며 전송하지 않는다. `ctx.output`이 smoothing 후 양쪽에 공유되는 명령이다. C runner에서 Agent3가 output을 변경하지 않았는지 검사하고, 물리 trace와 Unity trace의 모든 공통 필드를 비교한다. Python sender는 각도를 재계산하지 않는다.

CSV 시간은 입력 replay 일정과 Agent1 입력 간격에 쓰이며, servo tick은 별도의 20 ms 시계다. `source_frame_id`는 CSV frame, `frame_id`는 증가하는 output sequence다. t=0 home 1개와 이후 제어 tick 1,353개를 합쳐 1,354개를 출력한다. 마지막 pose 후 1초 동안 smoothing을 계속한다.

## Canonical 원본

외부 저장소는 `D:\working\final_project\zynq-cnn-motion-robot-soc`다. 현재 checkout HEAD `905b943`에는 완성된 integration API가 없어서, 로컬에 이미 존재하는 `origin/dev/robot`의 commit `240007988aac24049e3775fb25d69999f6d30552`를 고정 사용한다. checkout/fetch/branch 변경 없이 `git show`로 원본 C/header blob을 `build/canonical/<commit>/`에 추출한다. 이는 빌드용 원본 snapshot이며 수정하거나 알고리즘을 분기하지 않는다. `canonical_manifest.json`에 commit과 각 원본의 SHA256을 기록한다. `ROBOT_TRACE`는 canonical 진단 통계를 활성화한다.

실제 기존 API는 `agent_pipeline_init`, `agent1_run`, `agent2_run`, `agent2_tick`, `agent3_run`이다. 입력 body dropout은 짧은 전체 target HOLD, finger dropout은 주요 관절 계산을 유지하면서 wrist/gripper HOLD, 반복 frame은 재계산하지 않는다. 장기 body dropout은 target invalid이며 마지막 승인 목표/output 유지 정책을 그대로 따른다.

## 실행

PowerShell에서 다음을 실행한다. Python 3와 GCC가 필요하며 현재 설치된 경로를 사용한다. 생성물은 Unity 프로젝트 내부 `Tools/AgentReplay/build`에만 저장한다.

```powershell
Set-Location 'D:\working\final_project\Unity\HumanMotionDigitalTwin'
$step1Python = 'C:\Users\kccistc\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
& $step1Python .\Tools\AgentReplay\build_canonical_replay.py
& $step1Python .\Tools\AgentReplay\verify_step1_host.py
```

Unity에서 현재 편집 내용을 저장한 다음 **Tools → Human Motion → STEP 1 → Run Main CSV Validation**을 누른다. 단축키는 **Ctrl+Shift+F8**이다. 이 메뉴는 Main을 열고 Play Mode에서만 UDP와 probe를 활성화한다. Console의 `STEP 1 Main 준비 완료`를 확인한 뒤:

```powershell
& $step1Python .\Tools\AgentReplay\verify_step1_unity.py
```

약 27초간 실제 UDP로 재생하며 모든 적용 명령과 5개 pivot·linkage gripper 회전을 검사한다. 각 검증은 새 메뉴 실행 후 한 번씩 한다. 종료 시 Unity Play를 끄면 원래 Manual 설정으로 돌아가고 `Step1MainCleanup.md`에 수신 thread Join과 UDP 포트 해제 결과를 기록한다.

```powershell
& $step1Python .\Tools\verify_dual_integrity.py
```

마지막 명령은 기존 보존 baseline을 재사용해 Main/핵심 파일 및 외부 작업 파일의 SHA256을 확인한다. 이전부터 존재한 이름이며 Dual 데모를 실행하지 않는다. 검증 없이 전송만 하려면 UDP mode의 기존 receiver가 실행 중일 때 `send_joint_trace_udp.py --csv .\Tools\AgentReplay\build\joint_command_trace.csv`를 사용한다.

## 결과물과 해석

| 파일 | 의미 |
|---|---|
| `build/joint_command_trace.csv` | Unity에 보낼 최종 output, transport/source frame 분리 |
| `build/physical_command_pwm_trace.csv` | 같은 output의 Agent3 입력 및 6채널 PWM 결과 |
| `build/pose_pipeline_trace.csv` | 각 source frame의 Agent1/Agent2 결과 |
| `build/replay_summary.json` | CSV/Agent/tick/servo 통계 |
| `build/canonical_manifest.json` | canonical commit 및 SHA256 |
| `build/step1_host_results.json` | 원본 테스트 출력과 host 검증 결과 |
| `build/step1_unity_results.json` | 실제 Unity replay 및 동일성 결과 |
| `../../Validation/step1_unity_applied.csv` | 실제 적용된 모든 명령과 transform |
| `../../Validation/Step1FinalReport.md` | 사용자 요구 17개 항목 최종 보고 |

이번 CSV는 모든 pose가 valid지만, Agent2 안전 검사가 473개 target을 거부했다. 정상 승인 목표 49개(새 목표 46개, 동일 목표 3개)를 따라 마지막 안전 목표를 유지한다. 모든 사람 pose를 승인했다는 뜻은 아니다. 안전 검사를 완화하지 않았다.

기존 receiver는 main Update에서 최신 mailbox를 적용한다. 이번에는 1,354개 전체를 수신하고 1,352개를 렌더에 적용했다. 중간 2개는 렌더 사이에서 최신값으로 교체됐고 패킷 stale/유실이 아니다. 실제 적용된 모든 tick은 물리 명령과 일치했다. 50 Hz 전 tick 렌더나 실제 서보의 기계적 각도/시간 동기화를 보장하는 검증은 아니다.

Agent3 검증은 canonical host register mock을 쓴다. 128-write 제한 때문에 tick마다 **mock HAL/register log만** 초기화하고 Agent1/Agent2/context는 유지한다. 6채널 값, 레지스터 offset, UPDATE를 검사한다. 이 host runner 자체를 보드 펌웨어로 배포하지 않는다.

Canonical 테스트 8개와 local CSV parser 테스트 1개가 PASS했다. 기존 `test_output_control.c`는 원본에서 삭제된 `motion_record.h/.c`를 참조하여 SKIP했다. 대신 현재 실제 Agent3 경로의 1,354개 전체 출력, servo control/HAL 단위 테스트, integration smoke를 실행했다. 실제 PWM 핀·서보 운동·기계 offset은 별도 보드 검증이 필요하다.
