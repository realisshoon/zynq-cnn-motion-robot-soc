# Robot Arm 검증 결과

실행 UTC: 2026-09-21T05:52:40.0412125Z
Unity: 6000.3.24f1
기준 소스: dev/robot @ f7d8c0495383642ef0401571fe26e289d9765cf6

독립 preview Scene에서 Builder 및 실제 Transform을 실행한 결과입니다. 하드웨어 검증은 아닙니다.

- Hierarchy / 자동 reference: PASS
- Neutral: PASS
- Base (Test A): PASS
- Manual / UDP 입력 분리: PASS
- Shoulder/Elbow (Test B): PASS
- Wrist (Test C): PASS
- Gripper (Test D): PASS
- valid=false HOLD / 비유한 수 거부: PASS
- Inspector 설정 / 재실행 / 비활성 중복 방지: PASS
- 부분 Hierarchy 확장 / Undo 복원: PASS
- Compile (Unity Editor에서 컴파일된 코드 실행): PASS

전체 결과: PASS

## 실행한 소스 SHA256

- `Scripts/JointCommandData.cs`: `258E75E482CAD302A8E8291F3539779D32DC718F0A085FB8FD52EEF0BCB8D56C`
- `Scripts/RobotArmController.cs`: `0DEFD275BD811D07BD242F5CE14C0FE5A7AABFBF422093CF336BD529BC7DAA36`
- `Editor/RobotArmBuilder.cs`: `455C496721C96FC6E3A7DE5521EB768B989186337885A22741466363B9259714`
- `Editor/RobotArmValidation.cs`: `077493CB2660B9A8F652A90DBD1CCCAEC25528CD18CCC810077A11D76B4E4429`
