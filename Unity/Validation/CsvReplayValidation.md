# CSV 실제 UDP replay 검증

- PASS: canonical C trace 1354행 / 20 ms / 27.060000초
- PASS: source frame과 transport sequence 분리
- PASS: Unity 수신 1354, 적용 1183, 마지막 frame 1354, stale 0
- PASS: 253개 telemetry 표본의 6채널 값이 해당 canonical output 행과 일치
- PASS: 오른팔 Base pivot 일치 / 왼팔 GrabPoint 불변
- Windows/Python UDP 송신은 실시간 OS 보장이 아닌 20 ms deadline 기반 재생입니다.
