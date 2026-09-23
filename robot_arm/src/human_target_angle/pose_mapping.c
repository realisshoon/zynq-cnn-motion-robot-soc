#include "human_target_angle/pose_mapping.h"

#include <stddef.h>
#include <string.h>

#include "pose_mapping_internal.h"

/* ================================================================
 * Agent1 상위 제어 / Public API
 * ================================================================
 *
 * 이 파일은 세부 수학을 직접 구현하지 않는다.
 * CNN frame 한 개가 들어왔을 때 아래 순서를 연결하는 역할만 담당한다.
 *
 *  1) frame_id 중복 검사
 *  2) landmark tracking / dropout 처리
 *  3) 상대 3D pose 복원
 *  4) major joint angle 계산
 *  5) wrist / gripper 계산
 *  6) 최종 HumanJointTarget 출력
 */

/* 마지막 정상 Target을 잠깐 유지하거나, 너무 오래되면 invalid 처리한다. */
static int output_hold_or_invalid(
    PoseMappingContext *ctx,
    HumanJointTarget *target
)
{
    if (ctx->last_target_valid && ctx->target_age_sec <= PM_TARGET_HOLD_SEC) {
        *target = ctx->last_target;
        target->valid = 1U;
        return 0;
    }

    memset(target, 0, sizeof(*target));
    target->valid = 0U;
    ctx->last_target_valid = 0U;

    /*
     * 장시간 Major dropout 동안 손목/손 geometry는 stale해질 수 있으므로
     * hand 쪽 3D/normal history만 다음 정상 frame에서 다시 잡는다.
     *
     * Body frame과 Major angle history는 유지한다. 측면에서 Shoulder가 다시
     * 잡히는 첫 frame은 오히려 불안정할 수 있으므로, 이전 안정 body frame에서
     * 천천히 따라가는 편이 갑작스러운 Base 축 점프를 줄인다.
     */
    ctx->hand_angle_valid = 0U;
    ctx->prev_hand_normal_valid = 0U;
    ctx->prev_roll_raw_valid = 0U;
    ctx->prev_pitch_raw_valid = 0U;
    ctx->finger_pose3d_valid = 0U;

    return -1;
}

int pose_mapping_init(PoseMappingContext *ctx)
{
    if (ctx == NULL) return -1;

    memset(ctx, 0, sizeof(*ctx));
    ctx->initialized = 1U;
    return 0;
}

void pose_mapping_reset(PoseMappingContext *ctx)
{
    if (ctx == NULL) return;

    memset(ctx, 0, sizeof(*ctx));
    ctx->initialized = 1U;
}

int pose_mapping_start_roll_zero_calibration(PoseMappingContext *ctx)
{
    if (ctx == NULL || !ctx->initialized) return -1;

    ctx->roll_zero_calibrating = 1U;
    ctx->roll_zero_elapsed_sec = 0.0f;
    ctx->roll_zero_sample_count = 0U;
    ctx->roll_zero_sin_sum = 0.0f;
    ctx->roll_zero_cos_sum = 0.0f;

    return 0;
}

uint8_t pose_mapping_is_roll_zero_calibrating(const PoseMappingContext *ctx)
{
    if (ctx == NULL) return 0U;
    return ctx->roll_zero_calibrating;
}

void pose_mapping_clear_roll_zero(PoseMappingContext *ctx)
{
    if (ctx == NULL) return;

    ctx->roll_zero_calibrating = 0U;
    ctx->roll_zero_elapsed_sec = 0.0f;
    ctx->roll_zero_sample_count = 0U;
    ctx->roll_zero_sin_sum = 0.0f;
    ctx->roll_zero_cos_sum = 0.0f;
    ctx->roll_zero_offset_deg = 0.0f;
    ctx->roll_zero_calibrated = 0U;
}

int pose_mapping_update(
    PoseMappingContext *ctx,
    const HumanPose2D *pose,
    PoseArmSide active_arm,
    float dt_sec,
    HumanJointTarget *target
)
{
    float dt_age_sec;
    float dt_filter_sec;
    float shoulder_span_px = 0.0f;
    HumanJointTarget fresh;
    uint8_t hand_updated = 0U;

    if (ctx == NULL || pose == NULL || target == NULL || !ctx->initialized) {
        return -1;
    }

    if (active_arm != POSE_ARM_LEFT && active_arm != POSE_ARM_RIGHT) {
        memset(target, 0, sizeof(*target));
        target->valid = 0U;
        return -1;
    }

    /*
     * PS main loop는 CNN보다 훨씬 빠를 수 있다.
     * 같은 frame_id를 여러 번 읽어도 filter를 다시 돌리지 않는다.
     */
    if (ctx->last_frame_id_valid && pose->frame_id == ctx->last_frame_id) {
        if (ctx->last_target_valid) {
            *target = ctx->last_target;
            return 0;
        }

        memset(target, 0, sizeof(*target));
        target->valid = 0U;
        return -1;
    }

    ctx->last_frame_id = pose->frame_id;
    ctx->last_frame_id_valid = 1U;

    /* dropout/timeout에는 실제 CNN frame 간 시간을 사용한다. */
    dt_age_sec = (dt_sec > 0.0f) ? dt_sec : (1.0f / PM_DEFAULT_FPS);

    /* EMA 계산에서 지나치게 큰/작은 dt의 영향을 막기 위해 제한한다. */
    dt_filter_sec = pm_sanitize_filter_dt(dt_age_sec);

    /*
     * 제어 팔이 바뀌면 이전 팔의 좌표/각도 history를 새 팔에 섞지 않는다.
     * Roll zero calibration도 팔마다 기준이 다를 수 있으므로 함께 초기화한다.
     */
    if (ctx->last_arm_side_valid && ctx->last_arm_side != active_arm) {
        pose_mapping_reset(ctx);
        ctx->last_frame_id = pose->frame_id;
        ctx->last_frame_id_valid = 1U;
    }

    ctx->last_arm_side = active_arm;
    ctx->last_arm_side_valid = 1U;

    if (ctx->last_target_valid) {
        ctx->target_age_sec += dt_age_sec;
    }

    /* ------------------------------------------------------------
     * Step 1. CNN Landmark Update
     * ------------------------------------------------------------ */
    pm_update_all_landmarks(ctx, pose, dt_filter_sec);

    /*
     * Shoulder / Elbow / Wrist 중 하나라도 현재 frame에서 빠졌다면
     * 서로 다른 timestamp의 Skeleton을 섞지 않는다.
     * 짧은 시간 동안 마지막 정상 Target만 유지한다.
     */
    if (!pm_major_all_fresh(ctx)) {
        return output_hold_or_invalid(ctx, target);
    }

    /* ------------------------------------------------------------
     * Step 2. Major Pose 상대 3D 복원
     * ------------------------------------------------------------ */
    if (pm_reconstruct_major_pose3d(
            ctx,
            active_arm,
            dt_filter_sec,
            &shoulder_span_px) != 0) {
        return output_hold_or_invalid(ctx, target);
    }

    /* ------------------------------------------------------------
     * Step 3. Base / Shoulder / Elbow
     * ------------------------------------------------------------ */
    memset(&fresh, 0, sizeof(fresh));

    if (pm_calculate_major_angles(
            ctx,
            active_arm,
            dt_filter_sec,
            &fresh) != 0) {
        return output_hold_or_invalid(ctx, target);
    }

    /* ------------------------------------------------------------
     * Step 4. Wrist Pitch / Roll / Gripper
     * ------------------------------------------------------------
     * Finger는 major landmark보다 더 자주 누락될 수 있으므로
     * Finger가 빠져도 Base/Shoulder/Elbow는 계속 갱신한다.
     */
    if (pm_fingers_both_fresh(ctx)) {
        if (pm_reconstruct_finger_pose3d(ctx, dt_filter_sec) == 0 &&
            pm_calculate_hand_angles_and_gripper(
                ctx,
                shoulder_span_px,
                dt_age_sec,
                dt_filter_sec,
                &fresh) == 0) {
            hand_updated = 1U;
        }
    }

    if (!hand_updated) {
        /*
         * Finger 순간 누락 시 Wrist/Gripper만 마지막 정상값을 유지한다.
         * Major joint는 이번 CNN frame의 새 값으로 계속 움직인다.
         */
        if (ctx->last_target_valid) {
            fresh.wrist_pitch_deg = ctx->last_target.wrist_pitch_deg;
            fresh.wrist_roll_deg  = ctx->last_target.wrist_roll_deg;
            fresh.gripper_norm         = ctx->last_target.gripper_norm;
        } else if (ctx->hand_angle_valid) {
            fresh.wrist_pitch_deg = pm_wrap180(ctx->prev_wrist_pitch_deg);
            fresh.wrist_roll_deg  = pm_wrap180(ctx->prev_wrist_roll_deg);
            fresh.gripper_norm         = (float)ctx->gripper_state;
        } else {
            /*
             * 시작 직후 Finger가 아직 한 번도 잡히지 않은 경우에도
             * Base/Shoulder/Elbow 계산 자체는 유효하다.
             * 공통 구조체에 hand_valid가 없으므로 Wrist/Gripper는
             * 안전한 초기값을 사용하고 Major joint target은 살린다.
             */
            fresh.wrist_pitch_deg = 0.0f;
            fresh.wrist_roll_deg  = 0.0f;
            fresh.gripper_norm    = 1.0f; /* OPEN */
        }

        /*
         * Finger가 오래 누락되어도 전체 HumanJointTarget을 invalid로 만들지 않는다.
         * Major joint는 계속 갱신하고, 손 관련 값만 마지막 정상값 또는
         * 위의 안전 초기값을 유지한다.
         * 실제 gripper 접촉/보호는 Agent3 압력센서 feedback이 담당한다.
         */
    }

    fresh.valid = 1U;

    ctx->target_age_sec = 0.0f;
    ctx->last_target = fresh;
    ctx->last_target_valid = 1U;

    *target = fresh;
    return 1;
}
