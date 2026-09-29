# STEP 1 Host 검증

canonical commit: `240007988aac24049e3775fb25d69999f6d30552`

- PASS: human_target_angle/test_pose_mapping.c
- PASS: robot_calibration/test_robot_calibration.c
- PASS: robot_calibration/test_motion_limits.c
- PASS: robot_calibration/test_motion_smoothing.c
- PASS: robot_calibration/test_safety_check.c
- PASS: output_controller/test_servo_control.c
- PASS: output_controller/test_servo_hal.c
- SKIP: output_controller/test_output_control.c — canonical 테스트가 삭제된 motion_record.h/.c를 참조함. 현재 Agent3 경로는 실제 trace 전체 및 servo_control/servo_hal 테스트로 검증.
- PASS: integration/test_integration_smoke.c
- PASS: LOCAL_PARSER
- PASS: physical/Unity final command 1354행 전 필드 일치
- PASS: 동일 source frame 사이에서 변화한 smoothing output 56개
- PASS: 20 ms sequence / 유한값 / joint limits / tick별 속도 제한 / PWM 500~2500 µs
- PASS: canonical blob SHA256 불변
- 하드웨어: HOST_MOCK_ONLY. 실제 PWM 핀/서보 움직임은 미검증.
