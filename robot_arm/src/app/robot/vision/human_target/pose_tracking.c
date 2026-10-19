#include "pose_mapping_internal.h"

#include <stddef.h>

/*
 * CNN Landmark Tracking
 *
 * 최종 baseline에서는 다음만 한다.
 * 1) frame/landmark valid 확인
 * 2) time-based EMA
 * 3) 이번 frame에 새 좌표가 들어왔는지 fresh 표시
 *
 * 별도의 속도 기반 outlier reject는 제거했다.
 * 실제 영상에서 한 번 reject된 좌표가 연속 reject되어 tracking이 멈추는
 * 문제가 있었고, 현재 단계에서는 EMA만으로 입력 jitter를 완화하는 편이
 * 더 단순하고 관찰하기 쉽다.
 */

static void update_landmark(
    PoseLandmarkState *state,
    Point2D raw,
    uint8_t raw_valid,
    float dt_filter_sec
)
{
    float alpha;

    if (state == NULL) return;

    state->fresh = 0U;

    if (!raw_valid) {
        return;
    }

    if (!state->initialized) {
        state->value = raw;
        state->value.valid = 1U;
        state->initialized = 1U;
    } else {
        alpha = pm_alpha_from_tau(dt_filter_sec, PM_INPUT_2D_TAU_SEC);
        state->value.x += alpha * (raw.x - state->value.x);
        state->value.y += alpha * (raw.y - state->value.y);
        state->value.valid = 1U;
    }

    state->fresh = 1U;
}

static void update_finger_relative_2d(
    PoseFingerRelative2DState *state,
    Point2D finger_raw,
    Point2D wrist_raw,
    Point2D wrist_filtered,
    float dt_filter_sec
)
{
    float dx = finger_raw.x - wrist_raw.x;
    float dy = finger_raw.y - wrist_raw.y;

    if (!state->initialized) {
        state->offset.x = dx;
        state->offset.y = dy;
        state->initialized = 1U;
    } else {
        float alpha = pm_alpha_from_tau(
            dt_filter_sec, PM_FINGER_RELATIVE_2D_TAU_SEC);
        state->offset.x += alpha * (dx - state->offset.x);
        state->offset.y += alpha * (dy - state->offset.y);
    }

    state->ray_input.x = wrist_filtered.x + state->offset.x;
    state->ray_input.y = wrist_filtered.y + state->offset.y;
    state->ray_input.valid = 1U;
}

void pm_update_all_landmarks(
    PoseMappingContext *ctx,
    const HumanPose2D *pose,
    float dt_filter_sec
)
{
    uint8_t frame_valid;

    if (ctx == NULL || pose == NULL) return;

    frame_valid = pose->valid ? 1U : 0U;
    ctx->gripper_input = pose->gripper_2d;
    if (!frame_valid) {
        ctx->gripper_input.wrist.valid = 0U;
        ctx->gripper_input.finger1.valid = 0U;
        ctx->gripper_input.finger2.valid = 0U;
    }

    update_landmark(&ctx->shoulder_l, pose->shoulder_l,
                    (uint8_t)(frame_valid && pose->shoulder_l.valid),
                    dt_filter_sec);
    update_landmark(&ctx->shoulder_r, pose->shoulder_r,
                    (uint8_t)(frame_valid && pose->shoulder_r.valid),
                    dt_filter_sec);
    update_landmark(&ctx->elbow, pose->elbow,
                    (uint8_t)(frame_valid && pose->elbow.valid),
                    dt_filter_sec);
    update_landmark(&ctx->wrist, pose->wrist,
                    (uint8_t)(frame_valid && pose->wrist.valid),
                    dt_filter_sec);
    update_landmark(&ctx->finger1, pose->finger1,
                    (uint8_t)(frame_valid && pose->finger1.valid),
                    dt_filter_sec);
    update_landmark(&ctx->finger2, pose->finger2,
                    (uint8_t)(frame_valid && pose->finger2.valid),
                    dt_filter_sec);

    /* Preserve the ordinary landmark EMA for the gripper and legacy path.
     * For tracked 3D fingers, smooth their displacement from the wrist so
     * whole-hand translation follows the wrist without the longer finger
     * offset time constant adding lag to that translation. */
    if (ctx->wrist.fresh && ctx->finger1.fresh)
        update_finger_relative_2d(&ctx->finger1_relative_2d,
                                  pose->finger1, pose->wrist,
                                  ctx->wrist.value, dt_filter_sec);
    if (ctx->wrist.fresh && ctx->finger2.fresh)
        update_finger_relative_2d(&ctx->finger2_relative_2d,
                                  pose->finger2, pose->wrist,
                                  ctx->wrist.value, dt_filter_sec);
}

uint8_t pm_major_all_fresh(const PoseMappingContext *ctx)
{
    if (ctx == NULL) return 0U;

    return (uint8_t)(
        ctx->shoulder_l.fresh &&
        ctx->shoulder_r.fresh &&
        ctx->elbow.fresh &&
        ctx->wrist.fresh
    );
}

uint8_t pm_fingers_both_fresh(const PoseMappingContext *ctx)
{
    if (ctx == NULL) return 0U;
    return (uint8_t)(ctx->finger1.fresh && ctx->finger2.fresh);
}

uint8_t pm_gripper_all_fresh(const PoseMappingContext *ctx)
{
    if (ctx == NULL) return 0U;
    if (ctx->gripper_input.source)
        return (uint8_t)(ctx->gripper_input.wrist.valid &&
                         ctx->gripper_input.finger1.valid &&
                         ctx->gripper_input.finger2.valid);
    return (uint8_t)(ctx->wrist.fresh && pm_fingers_both_fresh(ctx));
}
