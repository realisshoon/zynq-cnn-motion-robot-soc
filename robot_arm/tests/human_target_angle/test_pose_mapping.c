#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "human_target_angle/pose_mapping.h"
#include "robot_config.h"

/*
 * Agent1 기본 smoke / dropout test.
 * 실제 카메라 정확도를 검증하는 test가 아니라 다음 동작을 확인한다.
 *
 *  1) 10~20 Hz 범위의 가변 dt에서도 정상 계산
 *  2) 같은 frame_id 중복 호출은 재계산하지 않음
 *  3) Finger 1 frame 누락 시 Base/Shoulder/Elbow는 계속 출력하고
 *     Wrist/Gripper는 이전값 유지
 *  4) Major landmark 1 frame 누락 시 전체 target 잠깐 HOLD
 *  5) Finger가 오래 누락되어도 major target은 유지
 */

/*
 * 기존 regression fixture는 640x480 시절 실제 영상 좌표다.
 * 최종 1280x720 카메라 모델(FX/FY 2배, 중심 639.5/359.5)에서도
 * 같은 camera ray를 보도록 좌표를 변환한다.
 *
 * x720 = 639.5 + 2 * (x640 - 319.5)
 * y720 = 359.5 + 2 * (y480 - 239.5)
 *
 * 단순 x*2, y*1.5가 아니다.
 * 16:9 720p에서 같은 angular ray를 유지하기 위한 test fixture 변환이다.
 */
static Point2D p2_from_legacy_640(float x, float y)
{
    Point2D p;
    p.x = PM_CAMERA_CX + (PM_CAMERA_FX / 554.0f) * (x - 319.5f);
    p.y = PM_CAMERA_CY + (PM_CAMERA_FY / 554.0f) * (y - 239.5f);
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

    pose->shoulder_l = p2_from_legacy_640(250.0f, 190.0f);
    pose->shoulder_r = p2_from_legacy_640(390.0f, 190.0f);

    pose->elbow   = p2_from_legacy_640(445.0f, 225.0f);
    pose->wrist   = p2_from_legacy_640(500.0f, 255.0f);
    pose->finger1 = p2_from_legacy_640(528.0f, 238.0f);
    pose->finger2 = p2_from_legacy_640(535.0f, 278.0f);

    pose->frame_id = frame_id;
    pose->valid = 1U;
}


static void test_side_view_reconstruction(void)
{
    /*
     * 실제 사람 영상에서 기존 equal-shoulder-Z 방식이 끊겼던 구간의
     * 2D 좌표(frame 34~39)를 1280x720 camera ray에 맞게 변환해 사용한다.
     *
     * 측면 자세에서는 2D shoulder 폭만으로 depth를 고정하면 실패하지만,
     * 새 depth-search reconstruction은 Shoulder L/R의 서로 다른 Z를 허용하므로
     * Major target이 연속적으로 생성되어야 한다.
     */
    static const float sample[6][12] = {
        {533.978f,299.440f, 516.981f,231.975f, 488.153f,271.493f,
         417.723f,284.086f, 395.496f,287.197f, 391.758f,282.566f},
        {534.942f,295.828f, 522.914f,232.741f, 503.240f,275.551f,
         423.033f,286.616f, 395.643f,288.870f, 391.946f,285.292f},
        {535.207f,300.815f, 530.472f,234.638f, 515.028f,278.878f,
         440.319f,303.765f, 413.518f,303.157f, 413.916f,297.446f},
        {535.553f,299.481f, 525.837f,238.611f, 499.612f,278.934f,
         437.990f,295.194f, 398.702f,304.526f, 400.541f,298.495f},
        {532.234f,296.834f, 525.777f,244.764f, 508.443f,310.186f,
         441.649f,357.462f, 417.290f,391.762f, 419.277f,390.926f},
        {532.181f,298.331f, 529.262f,241.015f, 517.266f,311.772f,
         470.162f,360.625f, 449.499f,365.877f, 452.006f,361.752f}
    };

    PoseMappingContext ctx;
    HumanPose2D pose;
    HumanJointTarget target;
    int i;

    assert(pose_mapping_init(&ctx) == 0);

    for (i = 0; i < 6; ++i) {
        int ret;

        memset(&pose, 0, sizeof(pose));
        pose.shoulder_l = p2_from_legacy_640(sample[i][0],  sample[i][1]);
        pose.shoulder_r = p2_from_legacy_640(sample[i][2],  sample[i][3]);
        pose.elbow      = p2_from_legacy_640(sample[i][4],  sample[i][5]);
        pose.wrist      = p2_from_legacy_640(sample[i][6],  sample[i][7]);
        pose.finger1    = p2_from_legacy_640(sample[i][8],  sample[i][9]);
        pose.finger2    = p2_from_legacy_640(sample[i][10], sample[i][11]);
        pose.frame_id = (uint32_t)(100U + (uint32_t)i);
        pose.valid = 1U;

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
        assert(ctx.major_pose3d_valid == 1U);

        printf("side frame=%d sl_z=%7.3f sr_z=%7.3f elbow_z=%7.3f wrist_z=%7.3f\n",
               34 + i,
               ctx.shoulder_l_3d.z,
               ctx.shoulder_r_3d.z,
               ctx.elbow_3d.z,
               ctx.wrist_3d.z);
    }
}


static void test_long_dropout_recovery(void)
{
    /*
     * 실제 영상에서 확인된 패턴을 단순화한 regression test.
     * - 정상 측면 pose
     * - Elbow 장기 dropout -> target invalid
     * - Elbow 재검출 -> 즉시 target 복구
     */
    static const float before_dropout[12] = {
        532.181f, 298.331f, 529.262f, 241.015f,
        517.266f, 311.772f, 470.162f, 360.625f,
        449.499f, 365.877f, 452.006f, 361.752f
    };
    static const float recovered[12] = {
        535.185f, 180.482f, 546.011f, 177.038f,
        572.068f, 260.682f, 488.040f, 285.655f,
        457.787f, 289.930f, 465.988f, 295.040f
    };

    PoseMappingContext ctx;
    HumanPose2D pose;
    HumanJointTarget target;
    int ret;
    int i;

    assert(pose_mapping_init(&ctx) == 0);

    memset(&pose, 0, sizeof(pose));
    pose.shoulder_l = p2_from_legacy_640(before_dropout[0], before_dropout[1]);
    pose.shoulder_r = p2_from_legacy_640(before_dropout[2], before_dropout[3]);
    pose.elbow      = p2_from_legacy_640(before_dropout[4], before_dropout[5]);
    pose.wrist      = p2_from_legacy_640(before_dropout[6], before_dropout[7]);
    pose.finger1    = p2_from_legacy_640(before_dropout[8], before_dropout[9]);
    pose.finger2    = p2_from_legacy_640(before_dropout[10], before_dropout[11]);
    pose.frame_id = 39U;
    pose.valid = 1U;

    ret = pose_mapping_update(&ctx, &pose, POSE_ARM_RIGHT, 1.0f / 15.0f, &target);
    assert(ret == 1);
    assert(target.valid == 1U);

    /* 0.6초 이상 Elbow dropout -> PM_TARGET_HOLD_SEC 초과 */
    for (i = 0; i < 6; ++i) {
        pose.frame_id = (uint32_t)(40 + i);
        pose.elbow.valid = 0U;
        ret = pose_mapping_update(&ctx, &pose, POSE_ARM_RIGHT, 0.100f, &target);
    }

    assert(ret == -1);
    assert(target.valid == 0U);

    /* 실제 영상 frame 73과 유사한 재검출 pose */
    memset(&pose, 0, sizeof(pose));
    pose.shoulder_l = p2_from_legacy_640(recovered[0], recovered[1]);
    pose.shoulder_r = p2_from_legacy_640(recovered[2], recovered[3]);
    pose.elbow      = p2_from_legacy_640(recovered[4], recovered[5]);
    pose.wrist      = p2_from_legacy_640(recovered[6], recovered[7]);
    pose.finger1    = p2_from_legacy_640(recovered[8], recovered[9]);
    pose.finger2    = p2_from_legacy_640(recovered[10], recovered[11]);
    pose.frame_id = 73U;
    pose.valid = 1U;

    ret = pose_mapping_update(&ctx, &pose, POSE_ARM_RIGHT, 1.0f / 15.0f, &target);

    assert(ret == 1);
    assert(target.valid == 1U);
    assert(finite_target(&target));
    assert(ctx.body_frame_valid == 1U);
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
    pose.elbow.x += 6.0f;
    pose.wrist.x += 8.0f;
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
    pose.elbow.x += 10.0f;

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
     *    -> 현재 정책은 전체 target을 invalid로 만들지 않는다.
     *       Major joint는 계속 갱신하고 Hand 출력만 HOLD한다.
     * ------------------------------------------------------------ */
    previous = target;

    for (i = 0; i < 8; ++i) {
        make_pose(&pose, (uint32_t)(30 + i));
        pose.elbow.x += (float)i * 1.5f;
        pose.wrist.x += (float)i * 2.0f;
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
        assert(fabsf(target.wrist_pitch_deg - previous.wrist_pitch_deg) < 1.0e-5f);
        assert(fabsf(target.wrist_roll_deg  - previous.wrist_roll_deg)  < 1.0e-5f);
        assert(fabsf(target.gripper_norm   - previous.gripper_norm)    < 1.0e-5f);
    }

    test_side_view_reconstruction();
    test_long_dropout_recovery();

    printf("test_pose_mapping: PASS\n");
    return 0;
}
