#include "pose_mapping_internal.h"

#include <math.h>
#include <stddef.h>

/* ================================================================
 * Wrist Pitch / Wrist Roll / Gripper
 * ================================================================
 *
 * Roll은 Agent1에서 가장 민감한 값이므로 다음 보정을 함께 사용한다.
 *  - 3D Z 강한 EMA
 *  - Hand plane normal 생성
 *  - +N/-N sign flip 방지
 *  - Normal time-based EMA
 *  - Forearm 축 기준 signed atan2
 *  - ±180 unwrap
 *  - 순간 spike 억제
 *  - zero calibration
 */

static void update_roll_zero_calibration(
    PoseMappingContext *ctx,
    float raw_roll_deg,
    float dt_age_sec
)
{
    float rad;

    if (ctx == NULL || !ctx->roll_zero_calibrating) return;

    rad = raw_roll_deg * PM_DEG_TO_RAD;
    ctx->roll_zero_sin_sum += sinf(rad);
    ctx->roll_zero_cos_sum += cosf(rad);
    ctx->roll_zero_sample_count++;
    ctx->roll_zero_elapsed_sec += dt_age_sec;

    if (ctx->roll_zero_elapsed_sec >= PM_ROLL_ZERO_CALIB_SEC &&
        ctx->roll_zero_sample_count >= PM_ROLL_ZERO_MIN_SAMPLES) {

        ctx->roll_zero_offset_deg = atan2f(
            ctx->roll_zero_sin_sum,
            ctx->roll_zero_cos_sum
        ) * PM_RAD_TO_DEG;

        ctx->roll_zero_calibrated = 1U;
        ctx->roll_zero_calibrating = 0U;

        /* 새 0점 기준으로 출력 필터도 0도 근처에서 다시 시작한다. */
        ctx->prev_wrist_roll_deg = 0.0f;
    }
}

int pm_calculate_hand_angles_and_gripper(
    PoseMappingContext *ctx,
    float shoulder_span_px,
    float dt_age_sec,
    float dt_filter_sec,
    HumanJointTarget *out
)
{
    Vec3 body_x, body_y, body_z;
    Vec3 forearm, forearm_n;
    Vec3 finger_center;
    Vec3 hand_forward, hand_forward_n;
    Vec3 finger_span, finger_span_n;
    Vec3 pitch_axis;
    Vec3 hand_normal;
    Vec3 hand_normal_proj;
    Vec3 reference_normal;
    Vec3 temp;
    float sin_value, cos_value;
    float wrist_pitch_deg;
    float raw_roll_deg;
    float corrected_roll_deg;
    float plane_quality;
    float reference_quality;
    float normal_alpha;
    float finger_span_px;
    float gripper_ratio;

    if (ctx == NULL || out == NULL || shoulder_span_px <= PM_EPS) return -1;

    /* Major angle 단계에서 갱신한 동일한 안정화 Body frame을 공유한다. */
    if (pm_get_stable_body_frame(ctx, &body_x, &body_y, &body_z) != 0) {
        return -1;
    }

    forearm = pm_vsub(ctx->wrist_3d, ctx->elbow_3d);
    forearm_n = forearm;
    if (pm_vnormalize(&forearm_n) != 0) return -1;

    finger_center = pm_vscale(pm_vadd(ctx->finger1_3d, ctx->finger2_3d), 0.5f);
    hand_forward = pm_vsub(finger_center, ctx->wrist_3d);
    hand_forward_n = hand_forward;
    if (pm_vnormalize(&hand_forward_n) != 0) return -1;

    finger_span = pm_vsub(ctx->finger2_3d, ctx->finger1_3d);
    finger_span_n = finger_span;
    if (pm_vnormalize(&finger_span_n) != 0) return -1;

    /* ------------------------------------------------------------
     * Wrist Pitch
     * ------------------------------------------------------------ */
    pitch_axis = pm_project_perpendicular(finger_span_n, forearm_n);

    if (pm_vnormalize(&pitch_axis) != 0) {
        /* Finger span이 불안정한 경우 Body Z를 이용한 fallback */
        pitch_axis = pm_vcross(body_z, forearm_n);
        if (pm_vnormalize(&pitch_axis) != 0) return -1;
    }

    sin_value = pm_vdot(
        pitch_axis,
        pm_vcross(forearm_n, hand_forward_n)
    );

    cos_value = pm_clampf(
        pm_vdot(forearm_n, hand_forward_n),
        -1.0f,
        1.0f
    );

    wrist_pitch_deg = atan2f(sin_value, cos_value) * PM_RAD_TO_DEG;

    /* ------------------------------------------------------------
     * Wrist Roll
     * ------------------------------------------------------------
     * Hand Plane = (Wrist -> Finger Center) x (Finger1 -> Finger2)
     */
    hand_normal = pm_vcross(hand_forward_n, finger_span_n);
    plane_quality = pm_vlen(hand_normal);

    if (plane_quality < PM_MIN_HAND_PLANE_QUALITY) return -1;
    if (pm_vnormalize(&hand_normal) != 0) return -1;

    /* Roll은 Forearm 축 주위 회전이므로 Forearm 방향 성분 제거 */
    hand_normal_proj = pm_project_perpendicular(hand_normal, forearm_n);

    if (pm_vlen(hand_normal_proj) < PM_MIN_HAND_PLANE_QUALITY) return -1;
    if (pm_vnormalize(&hand_normal_proj) != 0) return -1;

    /* 동일 평면의 +N / -N 표현이 frame마다 뒤집히지 않도록 한다. */
    if (ctx->prev_hand_normal_valid) {
        if (pm_vdot(hand_normal_proj, ctx->prev_hand_normal) < 0.0f) {
            hand_normal_proj = pm_vscale(hand_normal_proj, -1.0f);
        }

        normal_alpha = pm_alpha_from_tau(dt_filter_sec, PM_HAND_NORMAL_TAU_SEC);

        hand_normal_proj = pm_vadd(
            pm_vscale(ctx->prev_hand_normal, 1.0f - normal_alpha),
            pm_vscale(hand_normal_proj, normal_alpha)
        );

        if (pm_vnormalize(&hand_normal_proj) != 0) return -1;
    }

    ctx->prev_hand_normal = hand_normal_proj;
    ctx->prev_hand_normal_valid = 1U;

    /* Camera-up-based Body Y supplies roll zero (X, then Z fallback). */
    reference_normal = pm_project_perpendicular(body_y, forearm_n);
    reference_quality = pm_vlen(reference_normal);

    if (reference_quality < PM_MIN_REFERENCE_QUALITY) {
        reference_normal = pm_project_perpendicular(body_x, forearm_n);
        reference_quality = pm_vlen(reference_normal);
    }

    if (reference_quality < PM_MIN_REFERENCE_QUALITY) {
        reference_normal = pm_project_perpendicular(body_z, forearm_n);
        reference_quality = pm_vlen(reference_normal);
    }

    if (reference_quality < PM_MIN_REFERENCE_QUALITY) return -1;
    if (pm_vnormalize(&reference_normal) != 0) return -1;

    temp = pm_vcross(reference_normal, hand_normal_proj);

    sin_value = pm_vdot(forearm_n, temp);
    cos_value = pm_clampf(
        pm_vdot(reference_normal, hand_normal_proj),
        -1.0f,
        1.0f
    );

    raw_roll_deg = atan2f(sin_value, cos_value) * PM_RAD_TO_DEG;

    /*
     * ±180도 경계 unwrap + 순간 spike 억제.
     * 여기의 spike 억제는 CNN/3D 추정 이상치 제거용이며
     * 로봇의 각속도 제한은 하지 않는다.
     */
    if (ctx->prev_roll_raw_valid) {
        float raw_delta;

        raw_roll_deg = pm_unwrap_near(
            raw_roll_deg,
            ctx->prev_roll_raw_unwrapped_deg
        );

        raw_delta = raw_roll_deg - ctx->prev_roll_raw_unwrapped_deg;

        if (fabsf(raw_delta) > PM_ROLL_SPIKE_MARGIN_DEG) {
            /*
             * 예전처럼 이전값을 그대로 HOLD하면 실제 손목 자세가 바뀐 뒤에도
             * 같은 큰 delta가 반복되어 Roll이 영구 고정될 수 있다.
             * 이번 frame에서는 허용된 범위까지만 따라가게 해서 spike는 줄이되
             * 다음 frame에서 새 자세로 계속 수렴할 수 있게 한다.
             * Robot angular-rate limit이 아니라 Human pose estimate의 이상치 완화다.
             */
            raw_roll_deg = ctx->prev_roll_raw_unwrapped_deg +
                copysignf(PM_ROLL_SPIKE_MARGIN_DEG, raw_delta);
        }
    } else {
        ctx->prev_roll_raw_valid = 1U;
    }

    ctx->prev_roll_raw_unwrapped_deg = raw_roll_deg;

    /* 정상적인 hand geometry가 나온 frame만 0점 평균에 사용한다. */
    update_roll_zero_calibration(ctx, raw_roll_deg, dt_age_sec);

    corrected_roll_deg = raw_roll_deg;
    if (ctx->roll_zero_calibrated) {
        corrected_roll_deg -= ctx->roll_zero_offset_deg;
    }

    if (!ctx->hand_angle_valid) {
        ctx->prev_wrist_pitch_deg = wrist_pitch_deg;
        ctx->prev_wrist_roll_deg = corrected_roll_deg;
        ctx->hand_angle_valid = 1U;
    } else {
        ctx->prev_wrist_pitch_deg = pm_filter_angle_continuous(
            ctx->prev_wrist_pitch_deg,
            wrist_pitch_deg,
            PM_JOINT_ANGLE_TAU_SEC,
            PM_JOINT_DEADBAND_DEG,
            dt_filter_sec,
            1U
        );

        ctx->prev_wrist_roll_deg = pm_filter_angle_continuous(
            ctx->prev_wrist_roll_deg,
            corrected_roll_deg,
            PM_ROLL_ANGLE_TAU_SEC,
            PM_ROLL_DEADBAND_DEG,
            dt_filter_sec,
            1U
        );
    }

    /* ------------------------------------------------------------
     * Gripper OPEN/CLOSE 의도
     * ------------------------------------------------------------
     * 0=CLOSE, 1=OPEN만 Agent2/3로 전달한다.
     * 실제 물체 접촉 압력은 Agent3의 압력센서 feedback이 담당한다.
     */
    finger_span_px = pm_distance_2d(ctx->finger1.value, ctx->finger2.value);
    gripper_ratio = finger_span_px / shoulder_span_px;

    if (!ctx->gripper_initialized) {
        ctx->gripper_state = (gripper_ratio >= PM_GRIPPER_OPEN_RATIO) ? 1U : 0U;
        ctx->gripper_initialized = 1U;
    } else if (ctx->gripper_state) {
        /* 현재 OPEN: 충분히 오므려졌을 때만 CLOSE */
        if (gripper_ratio <= PM_GRIPPER_CLOSE_RATIO) {
            ctx->gripper_state = 0U;
        }
    } else {
        /* 현재 CLOSE: 충분히 벌어졌을 때만 OPEN */
        if (gripper_ratio >= PM_GRIPPER_OPEN_RATIO) {
            ctx->gripper_state = 1U;
        }
    }

    out->wrist_pitch_deg = pm_wrap180(ctx->prev_wrist_pitch_deg);
    out->wrist_roll_deg = pm_wrap180(ctx->prev_wrist_roll_deg);
    out->gripper_norm = (float)ctx->gripper_state;

    return 0;
}
