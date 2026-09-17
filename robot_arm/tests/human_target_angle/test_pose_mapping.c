#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "human_target_angle/pose_mapping.h"

/*
 * Agent1 기본 smoke / dropout test.
 * 실제 카메라 정확도를 검증하는 test가 아니라 다음 동작을 확인한다.
 *
 *  1) 10~20 Hz 범위의 가변 dt에서도 정상 계산
 *  2) 같은 frame_id 중복 호출은 재계산하지 않음
 *  3) Finger 1 frame 누락 시 Base/Shoulder/Elbow는 계속 출력하고
 *     Wrist/Gripper는 이전값 유지
 *  4) Major landmark 1 frame 누락 시 전체 target 잠깐 HOLD
 *  5) 너무 긴 Finger loss는 invalid
 */

static Point2D p2(float x, float y)
{
    Point2D p;
    p.x = x;
    p.y = y;
    p.valid = 1U;
    return p;
}

static int finite_target(const HumanJointTarget *t)
{
    return isfinite(t->base_deg) &&
           isfinite(t->shoulder_deg) &&
           isfinite(t->elbow_deg) &&
           isfinite(t->wrist_pitch_deg) &&
           isfinite(t->wrist_roll_deg);
}

static void make_pose(HumanPose2D *pose, uint32_t frame_id)
{
    memset(pose, 0, sizeof(*pose));

    pose->shoulder_l = p2(250.0f, 190.0f);
    pose->shoulder_r = p2(390.0f, 190.0f);

    pose->elbow   = p2(445.0f, 225.0f);
    pose->wrist   = p2(500.0f, 255.0f);
    pose->finger1 = p2(528.0f, 238.0f);
    pose->finger2 = p2(535.0f, 278.0f);

    pose->frame_id = frame_id;
    pose->valid = 1U;
}

int main(void)
{
    PoseMappingContext ctx;
    HumanPose2D pose;
    HumanJointTarget target;
    HumanJointTarget previous;
    int ret;
    int i;

    assert(pose_mapping_init(&ctx) == 0);

    /* ------------------------------------------------------------
     * 1) 첫 frame: 약 15 Hz
     * ------------------------------------------------------------ */
    make_pose(&pose, 1U);

    ret = pose_mapping_update(
        &ctx,
        &pose,
        POSE_ARM_RIGHT,
        1.0f / 15.0f,
        &target
    );

    assert(ret == 1);
    assert(target.valid == 1U);
    assert(finite_target(&target));
    assert(target.gripper_norm == 0.0f || target.gripper_norm == 1.0f);

    printf("frame1 base=%7.3f shoulder=%7.3f elbow=%7.3f pitch=%7.3f roll=%7.3f grip=%u\n",
           target.base_deg,
           target.shoulder_deg,
           target.elbow_deg,
           target.wrist_pitch_deg,
           target.wrist_roll_deg,
           (unsigned)target.gripper_norm);

    previous = target;

    /* ------------------------------------------------------------
     * 2) 같은 frame_id 반복 -> 이전 target 그대로
     * ------------------------------------------------------------ */
    ret = pose_mapping_update(
        &ctx,
        &pose,
        POSE_ARM_RIGHT,
        0.001f,
        &target
    );

    assert(ret == 0);
    assert(target.valid == 1U);
    assert(fabsf(target.elbow_deg - previous.elbow_deg) < 1.0e-5f);

    /* ------------------------------------------------------------
     * 3) 20 Hz frame에서 Finger2만 누락
     *    -> major joint는 새로 계산, hand command는 HOLD
     * ------------------------------------------------------------ */
    make_pose(&pose, 2U);
    pose.elbow.x += 3.0f;
    pose.wrist.x += 4.0f;
    pose.finger2.valid = 0U;

    ret = pose_mapping_update(
        &ctx,
        &pose,
        POSE_ARM_RIGHT,
        0.050f,
        &target
    );

    assert(ret == 1);
    assert(target.valid == 1U);
    assert(finite_target(&target));
    assert(fabsf(target.wrist_pitch_deg - previous.wrist_pitch_deg) < 1.0e-5f);
    assert(fabsf(target.wrist_roll_deg  - previous.wrist_roll_deg)  < 1.0e-5f);
    assert(fabsf(target.gripper_norm - previous.gripper_norm) < 1.0e-5f);

    /* ------------------------------------------------------------
     * 4) 10 Hz frame에서 Elbow 누락
     *    -> skeleton timestamp를 섞지 않고 이전 전체 target HOLD
     * ------------------------------------------------------------ */
    previous = target;
    make_pose(&pose, 3U);
    pose.elbow.valid = 0U;

    ret = pose_mapping_update(
        &ctx,
        &pose,
        POSE_ARM_RIGHT,
        0.100f,
        &target
    );

    assert(ret == 0);
    assert(target.valid == 1U);
    assert(fabsf(target.elbow_deg - previous.elbow_deg) < 1.0e-5f);

    /* ------------------------------------------------------------
     * 5) 다시 정상 frame
     * ------------------------------------------------------------ */
    make_pose(&pose, 4U);
    pose.elbow.x += 5.0f;

    ret = pose_mapping_update(
        &ctx,
        &pose,
        POSE_ARM_RIGHT,
        0.070f,
        &target
    );

    assert(ret == 1);
    assert(target.valid == 1U);
    assert(finite_target(&target));

    /* ------------------------------------------------------------
     * 6) Wrist Roll zero calibration
     *    10~20 Hz가 섞인 상태에서도 시간 기준으로 끝나야 한다.
     * ------------------------------------------------------------ */
    assert(pose_mapping_start_roll_zero_calibration(&ctx) == 0);

    for (i = 0; i < 20 && pose_mapping_is_roll_zero_calibrating(&ctx); ++i) {
        float dt = (i & 1) ? 0.050f : 0.100f;
        make_pose(&pose, (uint32_t)(5 + i));

        ret = pose_mapping_update(
            &ctx,
            &pose,
            POSE_ARM_RIGHT,
            dt,
            &target
        );

        assert(ret == 1);
        assert(target.valid == 1U);
    }

    assert(pose_mapping_is_roll_zero_calibrating(&ctx) == 0U);

    printf("after calibration roll=%7.3f deg\n", target.wrist_roll_deg);
/* ------------------------------------------------------------
 * 7) Finger 장기 누락
 *    -> Major joint는 계속 valid
 *    -> Wrist / Gripper는 마지막 정상값 HOLD
 * ------------------------------------------------------------ */
previous = target;

for (i = 0; i < 8; ++i) {
    make_pose(&pose, (uint32_t)(30 + i));

    pose.finger1.valid = 0U;
    pose.finger2.valid = 0U;

    ret = pose_mapping_update(
        &ctx,
        &pose,
        POSE_ARM_RIGHT,
        0.100f,
        &target
    );

    assert(ret == 1);
    assert(target.valid == 1U);
    assert(finite_target(&target));
}

/* Finger가 없으므로 Hand command는 마지막 정상값 유지 */
assert(fabsf(
    target.wrist_pitch_deg -
    previous.wrist_pitch_deg
) < 1.0e-5f);

assert(fabsf(
    target.wrist_roll_deg -
    previous.wrist_roll_deg
) < 1.0e-5f);

assert(fabsf(
    target.gripper_norm -
    previous.gripper_norm
) < 1.0e-5f);

    printf("test_pose_mapping: PASS\n");
    return 0;
}
