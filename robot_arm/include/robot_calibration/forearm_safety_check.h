#ifndef ROBOT_CALIBRATION_FOREARM_SAFETY_CHECK_H
#define ROBOT_CALIBRATION_FOREARM_SAFETY_CHECK_H

#include <stdint.h>
#include "robot_calibration/robot_geometry.h"
#include "robot_calibration/forearm_motion_control.h"

/*
 * 5축(팔꿈치부터 시작하는 수평 설치) 구조의 FK/안전검사.
 *
 * 원점(0,0,0) = elbow_roll/elbow_pitch 관절 중심("팔꿈치"). +Z=위(테이블에서
 * 멀어지는 방향, table normal과 동일). elbow_pitch=90도(중립)는 사용자
 * 확인(2026-09-22): 전완이 지면과 수직으로 서는(똑바로 위) 자세다. 링크
 * 길이는 사용자 확인(2026-10-04): 팔꿈치-손목(전완) 16cm,
 * 손목-그리퍼 끝(손) 20cm다. 손목은 wrist_pitch(굽힘)가 먼저, 그 결과를
 * wrist_roll(비틀림)이 전완 축 주위로 돌리는 순서로 가정한다(사용자
 * 확인, 2026-09-22 — 기구학적으로 더 안정적이라는 판단).
 *
 * RobotPoint3D는 robot_geometry.h의 범용 3D 점 타입을 그대로 재사용한다
 * (구 6축 전용 필드 이름이 없는 순수 {x_cm,y_cm,z_cm} 구조체이기 때문).
 * 중립 전완은 robot +Z, 기울임 기준 방위는 robot +Y, robot +X는 오른쪽이다.
 * A1 Table (+X,+Y,+Z)는
 * robot (+Y,-X,+Z)에 대응한다. +yaw는 +Z 오른손 회전(+Y에서 -X쪽),
 * elbow_pitch 서보값이 90도보다 커지면 그 방위 쪽으로 기울고,
 * +wrist_roll은 전완 방향 오른손 회전이다.
 */
typedef struct {
    RobotPoint3D elbow;  /* 원점, 항상 (0,0,0) */
    RobotPoint3D wrist;
    RobotPoint3D tip;
} ForearmJointPositions3D;

typedef struct {
    float elbow_wrist_cm;
    float wrist_roll_end_cm;
    float roll_end_tip_cm;
    float table_z_cm;
} ForearmRobotGeometry;

const ForearmRobotGeometry *forearm_robot_selected_geometry(void);

int forearm_robot_forward_kinematics_geometry(const ForearmJointCommand *command,
    const ForearmRobotGeometry *geometry, ForearmJointPositions3D *positions,
    RobotPoint3D *roll_motor_end);
int forearm_safety_check_geometry(const ForearmJointCommand *command,
    const ForearmRobotGeometry *geometry, uint32_t *issues);

/*
 * command==NULL이거나 finite가 아니면 0을 반환한다. command->valid는 보지
 * 않는다(안전검사와 무관하게 기하학은 항상 계산 가능해야 하므로).
 */
int forearm_robot_forward_kinematics_3d(const ForearmJointCommand *command, ForearmJointPositions3D *positions);

typedef uint32_t ForearmSafetyCheckFlags;
enum {
    FOREARM_SAFETY_CHECK_OK = 0u,
    FOREARM_SAFETY_CHECK_INVALID_COMMAND = 1u << 0,
    FOREARM_SAFETY_CHECK_SELF_COLLISION = 1u << 1,
    /* 사용자 확인(2026-10-04): 테이블은 팔꿈치 원점보다 10cm 아래(z=-10cm)다.
     * 2026-10-04 확인된 16cm/20cm 링크에서는 [20,160] 안에서도 도달 가능하다.
     * 예: (elbow_roll, elbow_pitch, wrist_pitch, wrist_roll)=(90,160,71,90)은
     * 손목 z=5.472cm, 손끝 z=-10.071cm로 테이블 충돌이다.
     * tests/robot_calibration/test_forearm_calibration.c의
     * test_clamped_envelope_contains_table_collisions 참고. */
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
