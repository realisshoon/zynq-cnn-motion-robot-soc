# STEP 1 Dual RobotArm_R 실제 UDP 검증

- 결과: PASS
- 전송 / 수신: 1354 / 1354
- 적용 / 캡처 / 같은 tick 명령·transform 대조: 1226 / 1226 / 1226
- stale / malformed / invalid / 캡처 누락: 0 / 0 / 0 / 0
- final output → Agent3 입력과 UDP 입력 동일성: 1354개 전체 PASS
- 명령 최대 오차: 7.99999998e-06; transform 최대 오차: 2.27e-05도
- 5개 joint 및 linkage gripper의 실제 변화: PASS
- 사전 검증 및 UDP에서 Manual overwrite 방지: PASS

기존 receiver는 렌더 Update에서 최신 mailbox 한 개를 적용한다. 따라서 수신 tick 중 128개는 렌더 사이에 최신값으로 교체됐으며, stale 패킷이나 UDP 손실이 아니다. 적용된 모든 tick을 같은 frame_id의 물리 경로 명령과 비교했다. 50Hz 모든 tick을 화면에 그린다는 의미는 아니다.

실제 보드 및 서보 운동은 미검증이며 physical 검증은 canonical Agent3/PWM host mock 경로다.

- PASS: 두 팔 및 T-frame 존재, receiver/listener 1개, 왼팔 전체 local transform 불변
- PASS: 매 LateUpdate 오른팔 overwrite 감시, START/Advance 자극, CSV START/RESET guard
