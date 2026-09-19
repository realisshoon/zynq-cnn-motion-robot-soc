#include "pose_mapping_internal.h"

#include <math.h>
#include <stddef.h>

/* ================================================================
 * Base / Shoulder / Elbow Joint Angle 계산
 * ================================================================ */

int pm_calculate_major_angles(
    PoseMappingContext *ctx,
    PoseArmSide active_arm,
    float dt_filter_sec,
    HumanJointTarget *out
)
{
    Point3D shoulder;
    Vec3 body_x, body_y, body_z;
    Vec3 upper, upper_n;
    Vec3 elbow_to_shoulder, elbow_to_wrist;
    float ux, uy, uz;
    float base_deg;
    float shoulder_deg;
    float elbow_deg;

    if (ctx == NULL || out == NULL) return -1;

    shoulder = (active_arm == POSE_ARM_LEFT)
        ? ctx->shoulder_l_3d
        : ctx->shoulder_r_3d;

    /*
     * 측면 자세에서는 Shoulder landmark가 흔들리기 쉬우므로
     * raw body frame 대신 시간축으로 안정화된 body frame을 사용한다.
     */
    if (pm_update_stable_body_frame(ctx, dt_filter_sec) != 0 ||
        pm_get_stable_body_frame(ctx, &body_x, &body_y, &body_z) != 0) {
        return -1;
    }

    upper = pm_vsub(ctx->elbow_3d, shoulder);
    upper_n = upper;
    if (pm_vnormalize(&upper_n) != 0) return -1;

    /* Upper Arm을 사람 Body Coordinate 기준 성분으로 분해한다. */
    ux = pm_vdot(upper_n, body_x);
    uy = pm_vdot(upper_n, body_y);
    uz = pm_vdot(upper_n, body_z);

    /* 사람 몸 기준 Upper Arm의 좌우/앞뒤 방향 */
    base_deg = atan2f(ux, uz) * PM_RAD_TO_DEG;

    /* Upper Arm의 위/아래 elevation */
    shoulder_deg = atan2f(
        uy,
        sqrtf(ux * ux + uz * uz)
    ) * PM_RAD_TO_DEG;

    elbow_to_shoulder = pm_vsub(shoulder, ctx->elbow_3d);
    elbow_to_wrist = pm_vsub(ctx->wrist_3d, ctx->elbow_3d);

    if (pm_vnormalize(&elbow_to_shoulder) != 0 ||
        pm_vnormalize(&elbow_to_wrist) != 0) {
        return -1;
    }

    /* 사람 팔을 쭉 펴면 약 180도 */
    elbow_deg = acosf(pm_clampf(
        pm_vdot(elbow_to_shoulder, elbow_to_wrist),
        -1.0f,
        1.0f
    )) * PM_RAD_TO_DEG;

    if (!ctx->major_angle_valid) {
        ctx->prev_base_deg = base_deg;
        ctx->prev_shoulder_deg = shoulder_deg;
        ctx->prev_elbow_deg = elbow_deg;
        ctx->major_angle_valid = 1U;
    } else {
        ctx->prev_base_deg = pm_filter_angle_continuous(
            ctx->prev_base_deg,
            base_deg,
            PM_JOINT_ANGLE_TAU_SEC,
            PM_JOINT_DEADBAND_DEG,
            dt_filter_sec,
            1U
        );

        ctx->prev_shoulder_deg = pm_filter_angle_continuous(
            ctx->prev_shoulder_deg,
            shoulder_deg,
            PM_JOINT_ANGLE_TAU_SEC,
            PM_JOINT_DEADBAND_DEG,
            dt_filter_sec,
            0U
        );

        ctx->prev_elbow_deg = pm_filter_angle_continuous(
            ctx->prev_elbow_deg,
            elbow_deg,
            PM_JOINT_ANGLE_TAU_SEC,
            PM_JOINT_DEADBAND_DEG,
            dt_filter_sec,
            0U
        );
    }

    /*
     * 내부 필터는 unwrap 상태를 유지하지만 public Human angle은 [-180, 180]로
     * 정규화한다. 이후 Robot delta/rate 계산에서 shortest-angle 처리는 Agent2가 한다.
     */
    out->base_deg = pm_wrap180(ctx->prev_base_deg);
    out->shoulder_deg = ctx->prev_shoulder_deg;
    out->elbow_deg = pm_clampf(ctx->prev_elbow_deg, 0.0f, 180.0f);

    return 0;
}
