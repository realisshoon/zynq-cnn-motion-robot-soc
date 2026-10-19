#include "app/robot/control/robot_pipeline.h"

#include <stddef.h>

#include "app/robot/vision/human_target/forearm_target.h"

#define HUMAN_TARGET_ARM POSE_ARM_RIGHT

int human_target_update(RobotPipeline *ctx, const HumanPose2D *pose, float dt_sec)
{
    int rc;

    if (ctx == NULL || pose == NULL) return 0;

    ctx->pose = *pose;
    ctx->dt_sec = dt_sec;
    ctx->frames_in++;
    ctx->target_ready = 0U;
    ctx->wrist_observation_fresh = 0U;

    /*
     * 반환값(1/0/-1)은 보지 않는다. 짧은 dropout 동안 HOLD(0)에서도 valid=1인
     * 마지막 정상 타겟이 나오므로, 다음 단계 진행 여부는 출력 valid로만 판단한다.
     */
    rc = forearm_target_run(&ctx->pose, HUMAN_TARGET_ARM, dt_sec);
    ctx->pose_result = (int8_t)rc; /* [TRACE] 로그용으로만 보관한다. 다음 단계 진행 여부의 판단에는 쓰지 않는다. */
    (void)rc;

    if (!forearm_target_output()->valid) return 0;

    ctx->target = *forearm_target_output();
    ctx->target_ready = 1U;
    ctx->targets_valid++;
    return 1;
}

int human_target_update_stereo(RobotPipeline *ctx, const HumanPose2D *image_pose,
                      const HumanPose3D *measured_pose, float dt_sec)
{
    int rc;
    if (ctx == NULL || image_pose == NULL || measured_pose == NULL) return 0;
    ctx->pose = *image_pose;
    ctx->dt_sec = dt_sec;
    ctx->frames_in++;
    ctx->target_ready = 0U;
    ctx->wrist_observation_fresh = measured_pose->valid &&
        measured_pose->wrist.valid && measured_pose->finger1.valid &&
        measured_pose->finger2.valid && measured_pose->frame_id == image_pose->frame_id;
    rc = forearm_target_run_stereo(image_pose, measured_pose,
                                        HUMAN_TARGET_ARM, dt_sec);
    ctx->pose_result = (int8_t)rc;
    (void)rc;
    if (!forearm_target_output()->valid) return 0;
    ctx->target = *forearm_target_output();
    ctx->target_ready = 1U;
    ctx->targets_valid++;
    return 1;
}
