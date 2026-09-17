#include "pose_mapping_internal.h"

#include <math.h>
#include <stddef.h>

/* ================================================================
 * CNN Landmark Tracking / Dropout / Outlier 처리
 * ================================================================
 *
 * CNN 좌표가 10~20 Hz로 들어오고 일부 landmark가 순간적으로 누락되거나
 * 튈 수 있으므로 각 landmark마다 최근 정상값과 age_sec를 관리한다.
 */

static uint8_t landmark_usable(
    const PoseLandmarkState *s,
    float hold_sec
)
{
    if (s == NULL || !s->initialized) return 0U;
    return (s->age_sec <= hold_sec) ? 1U : 0U;
}

/* Outlier 속도 계산의 기준 길이로 양 어깨 pixel 폭을 사용한다. */
static float get_tracking_scale_px(
    const PoseMappingContext *ctx,
    const HumanPose2D *pose
)
{
    float scale = 0.0f;

    if (pose != NULL && pose->valid &&
        pose->shoulder_l.valid && pose->shoulder_r.valid) {
        scale = pm_distance_2d(pose->shoulder_l, pose->shoulder_r);
    }

    if (scale < PM_MIN_SHOULDER_WIDTH_PX &&
        ctx->shoulder_l.initialized && ctx->shoulder_r.initialized) {
        scale = pm_distance_2d(ctx->shoulder_l.value, ctx->shoulder_r.value);
    }

    if (scale < PM_MIN_SHOULDER_WIDTH_PX) {
        scale = PM_MIN_SHOULDER_WIDTH_PX;
    }

    return scale;
}

/* Landmark 한 개를 갱신한다. */
static void update_landmark(
    PoseLandmarkState *state,
    Point2D raw,
    uint8_t raw_valid,
    float dt_age_sec,
    float dt_filter_sec,
    float tracking_scale_px
)
{
    float alpha;

    if (state == NULL) return;

    state->fresh = 0U;

    if (!raw_valid) {
        if (state->initialized) state->age_sec += dt_age_sec;
        return;
    }

#if PM_ENABLE_LANDMARK_OUTLIER_REJECTION
    if (state->initialized && dt_age_sec > PM_EPS) {
        float jump_px = pm_distance_2d(state->value, raw);
        float speed_ratio = jump_px / (tracking_scale_px * dt_age_sec);

        if (speed_ratio > PM_MAX_LANDMARK_SPEED_SHOULDER_PER_SEC) {
            /* 순간적으로 비정상적으로 튄 좌표는 이번 frame만 버린다. */
            state->age_sec += dt_age_sec;
            return;
        }
    }
#endif

    if (!state->initialized) {
        state->value = raw;
        state->initialized = 1U;
    } else {
        alpha = pm_alpha_from_tau(dt_filter_sec, PM_INPUT_2D_TAU_SEC);

        state->value.x += alpha * (raw.x - state->value.x);
        state->value.y += alpha * (raw.y - state->value.y);
        state->value.valid = 1U;
    }

    state->age_sec = 0.0f;
    state->fresh = 1U;
}

void pm_update_all_landmarks(
    PoseMappingContext *ctx,
    const HumanPose2D *pose,
    float dt_age_sec,
    float dt_filter_sec
)
{
    float scale_px;
    uint8_t frame_valid;

    if (ctx == NULL || pose == NULL) return;

    scale_px = get_tracking_scale_px(ctx, pose);
    frame_valid = pose->valid ? 1U : 0U;

    update_landmark(
        &ctx->shoulder_l,
        pose->shoulder_l,
        (uint8_t)(frame_valid && pose->shoulder_l.valid),
        dt_age_sec,
        dt_filter_sec,
        scale_px
    );

    update_landmark(
        &ctx->shoulder_r,
        pose->shoulder_r,
        (uint8_t)(frame_valid && pose->shoulder_r.valid),
        dt_age_sec,
        dt_filter_sec,
        scale_px
    );

    update_landmark(
        &ctx->elbow,
        pose->elbow,
        (uint8_t)(frame_valid && pose->elbow.valid),
        dt_age_sec,
        dt_filter_sec,
        scale_px
    );

    update_landmark(
        &ctx->wrist,
        pose->wrist,
        (uint8_t)(frame_valid && pose->wrist.valid),
        dt_age_sec,
        dt_filter_sec,
        scale_px
    );

    update_landmark(
        &ctx->finger1,
        pose->finger1,
        (uint8_t)(frame_valid && pose->finger1.valid),
        dt_age_sec,
        dt_filter_sec,
        scale_px
    );

    update_landmark(
        &ctx->finger2,
        pose->finger2,
        (uint8_t)(frame_valid && pose->finger2.valid),
        dt_age_sec,
        dt_filter_sec,
        scale_px
    );
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

uint8_t pm_major_all_usable(const PoseMappingContext *ctx)
{
    if (ctx == NULL) return 0U;

    return (uint8_t)(
        landmark_usable(&ctx->shoulder_l, PM_MAJOR_LANDMARK_HOLD_SEC) &&
        landmark_usable(&ctx->shoulder_r, PM_MAJOR_LANDMARK_HOLD_SEC) &&
        landmark_usable(&ctx->elbow, PM_MAJOR_LANDMARK_HOLD_SEC) &&
        landmark_usable(&ctx->wrist, PM_MAJOR_LANDMARK_HOLD_SEC)
    );
}

uint8_t pm_fingers_both_fresh(const PoseMappingContext *ctx)
{
    if (ctx == NULL) return 0U;
    return (uint8_t)(ctx->finger1.fresh && ctx->finger2.fresh);
}

float pm_max_finger_age_sec(const PoseMappingContext *ctx)
{
    float a;
    float b;

    if (ctx == NULL) return 1.0e9f;

    a = ctx->finger1.initialized ? ctx->finger1.age_sec : 1.0e9f;
    b = ctx->finger2.initialized ? ctx->finger2.age_sec : 1.0e9f;

    return (a > b) ? a : b;
}
