#ifndef ROBOT_CALIBRATION_FOREARM_SAFETY_CHECK_H
#define ROBOT_CALIBRATION_FOREARM_SAFETY_CHECK_H

#include <stdint.h>
#include "robot_calibration/safety_check.h"
#include "robot_calibration/forearm_motion_control.h"

/*
 * 새 5축(팔꿈치부터 시작하는 수평 설치) 구조의 FK/안전검사.
 *
 * 원점(0,0,0) = elbow_roll/elbow_pitch 관절 중심("팔꿈치"). +Z=위(테이블에서
 * 멀어지는 방향, table normal과 동일). elbow_pitch=90도(중립)는 사용자
 * 확인(2026-09-22): 전완이 지면과 수직으로 서는(똑바로 위) 자세다. 링크
 * 길이(24cm/10cm)는 사용자가 사진으로 실측해 준 값이다(2026-09-22); 어느
 * 구간이 팔꿈치-손목(전완)이고 어느 구간이 손목-그리퍼(손)인지는 확인
 * 필요 — 아래 .c 파일 주석 참고. 손목은 wrist_pitch(굽힘)가 먼저, 그 결과를
 * wrist_roll(비틀림)이 전완 축 주위로 돌리는 순서로 가정한다(사용자
 * 확인, 2026-09-22 — 기구학적으로 더 안정적이라는 판단).
 *
 * RobotPoint3D는 safety_check.h의 범용 3D 점 타입을 그대로 재사용한다
 * (구 6축 전용 필드 이름이 없는 순수 {x_cm,y_cm,z_cm} 구조체이기 때문).
 * 중립 전완은 robot +Y, robot +X는 오른쪽이다. A1 Table (+X,+Y,+Z)는
 * robot (+Y,-X,+Z)에 대응한다. +yaw는 +Z 오른손 회전(+Y에서 -X쪽),
 * +pitch는 위, +wrist_roll은 전완 방향 오른손 회전이다.
 */
typedef struct {
    RobotPoint3D elbow;  /* 원점, 항상 (0,0,0) */
    RobotPoint3D wrist;
    RobotPoint3D tip;
} ForearmJointPositions3D;

/*
 * command==NULL이거나 finite가 아니면 0을 반환한다. command->valid는 보지
 * 않는다(안전검사와 무관하게 기하학은 항상 계산 가능해야 하므로 — 구
 * safety_check.c와 동일한 설계).
 */
int forearm_robot_forward_kinematics_3d(const ForearmJointCommand *command, ForearmJointPositions3D *positions);

typedef uint32_t ForearmSafetyCheckFlags;
enum {
    FOREARM_SAFETY_CHECK_OK = 0u,
    FOREARM_SAFETY_CHECK_INVALID_COMMAND = 1u << 0,
    FOREARM_SAFETY_CHECK_SELF_COLLISION = 1u << 1,
    /* 수평 설치라 테이블(=이전의 "바닥")이 실제로 작업영역 아래에 있다.
     * 구 수직 설치와 달리 이번엔 비활성화하지 않았다 — 아래 .c 파일 주석과
     * 최종 보고의 "재실측 필요" 목록 참고.
     * *** 2026-09-22 확인: elbow_pitch=90=수직 규약 확정 이후, 현재
     * [20,160] 클램프 범위 안에서는 이 플래그가 도달 불가능하다(전역 최소
     * 높이 +0.548cm > 테이블 -5cm, tests/robot_calibration/
     * test_forearm_calibration.c의 test_clamped_envelope_never_reaches_table
     * 참고). 관절 실측 가동범위가 [20,160]보다 넓어지거나 테이블 높이가
     * 낮아지면 다시 발동할 수 있다. *** */
    FOREARM_SAFETY_CHECK_TABLE_COLLISION = 1u << 2
};

/*
 * finite/valid 명령 + 3D 기하 자기충돌 + 테이블 충돌 검사. 관절 각도 한계는
 * forearm_motion_control_apply_limits()가 별도로 적용한다. 반환값 1은
 * issues==OK와 동일하다.
 */
int forearm_safety_check_apply(const ForearmJointCommand *command,
                               ForearmSafetyCheckFlags *issues_out);

#endif /* ROBOT_CALIBRATION_FOREARM_SAFETY_CHECK_H */
