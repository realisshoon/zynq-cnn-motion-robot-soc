#include "pipeline_internal.h"

#include <math.h>
#include <stddef.h>

#include "app/robot/control/servo_control.h"
#include "drivers/pwm/servo_hal.h"

static RobotOutputResult prepare_output_reference(
    const RobotPipeline *ctx, ForearmJointCommand *reference,
    ForearmMotionState *motion, ServoPwmCommand *pwm)
{
    ForearmJointCommand clamped;
    ForearmJointCommand output;
    unsigned axis;
    float positions[FOREARM_MOTION_JOINT_COUNT];

    *reference = ctx->applied_command_valid ? ctx->applied_command : robot_home_pose;
    if (!reference->valid || !isfinite(reference->elbow_roll_deg) ||
        !isfinite(reference->elbow_pitch_deg) || !isfinite(reference->wrist_pitch_deg) ||
        !isfinite(reference->wrist_roll_deg) || !isfinite(reference->gripper_norm)) {
        return ROBOT_OUTPUT_RESULT_REFERENCE_INVALID;
    }
    clamped = *reference;
    forearm_motion_control_apply_limits(&clamped);
    if (!robot_pipeline_same_command(&clamped, reference) || reference->gripper_norm < 0.0f ||
        reference->gripper_norm > 1.0f) return ROBOT_OUTPUT_RESULT_REFERENCE_LIMITS;
    if (!forearm_safety_check_apply(reference, NULL))
        return ROBOT_OUTPUT_RESULT_REFERENCE_UNSAFE;

    forearm_calibration_state_init(motion);
    forearm_calibration_set_target(motion, reference);
    forearm_calibration_step(motion, &output);
    if (!robot_pipeline_same_command(&output, reference) || !motion->has_target || motion->held ||
        motion->blocked_flags != FOREARM_SAFETY_CHECK_OK ||
        motion->gripper != reference->gripper_norm) return ROBOT_OUTPUT_RESULT_REFERENCE_STATE;
    positions[0] = reference->elbow_roll_deg;
    positions[1] = reference->elbow_pitch_deg;
    positions[2] = reference->wrist_pitch_deg;
    positions[3] = reference->wrist_roll_deg;
    for (axis = 0; axis < FOREARM_MOTION_JOINT_COUNT; ++axis) {
        if (motion->axes[axis].v != 0.0 || motion->axes[axis].q != positions[axis] ||
            motion->axes[axis].q != motion->axes[axis].target)
            return ROBOT_OUTPUT_RESULT_REFERENCE_STATE;
    }
    if (!servo_control_convert(reference, pwm)) return ROBOT_OUTPUT_RESULT_CONVERSION_FAILED;
    return ROBOT_OUTPUT_RESULT_NONE;
}

static void commit_output_reference(RobotPipeline *ctx,
                                    const ForearmJointCommand *reference,
                                    const ForearmMotionState *motion,
                                    const ServoPwmCommand *pwm)
{
    ctx->motion = *motion;
    ctx->output = *reference;
    ctx->command = *reference;
    ctx->command_valid = 1U;
    ctx->servo_command = *reference;
    ctx->servo_command_valid = 1U;
    ctx->servo_command_tick = ctx->ticks;
    ctx->pwm = *pwm;
    ctx->target_ready = 0U;
    forearm_motion_control_unwrap_state_init(&ctx->unwrap);
}

int servo_output_set_enabled(RobotPipeline *ctx, int enabled)
{
    ForearmJointCommand reference;
    ForearmMotionState motion;
    ServoPwmCommand pwm;
    RobotOutputResult result;

    if (ctx == NULL) return 0;
    if (enabled && ctx->virtual_compute) {
        ctx->output_result = ROBOT_OUTPUT_RESULT_VIRTUAL_ACTIVE;
        return 0;
    }
    if (enabled && ctx->output_faulted) {
        ctx->output_result = ROBOT_OUTPUT_RESULT_HAL_RECOVERY_DISABLE_FAILED;
        return 0;
    }
    if (enabled && ctx->output_enabled) {
        ctx->output_result = ROBOT_OUTPUT_RESULT_ENABLED;
        return 1;
    }
    result = prepare_output_reference(ctx, &reference, &motion, &pwm);
    if (!enabled) {
        if (!servo_hal_disable()) {
            ctx->servo_errors++;
            ctx->output_result = ROBOT_OUTPUT_RESULT_HAL_DISABLE_FAILED;
            return 0;
        }
        ctx->output_enabled = 0U;
        ctx->arm_stationary = ctx->arm_tracking_started = 0U;
        ctx->output_parked = 1U;
        ctx->output_faulted = 0U;
        gripper_control_reset(ctx);
        if (result != ROBOT_OUTPUT_RESULT_NONE) {
            ctx->output_result = result;
            return 0;
        }
        commit_output_reference(ctx, &reference, &motion, &pwm);
        ctx->output_result = ROBOT_OUTPUT_RESULT_DISABLED;
        return 1;
    }
    if (result != ROBOT_OUTPUT_RESULT_NONE) {
        ctx->output_result = result;
        return 0;
    }
    result = ROBOT_OUTPUT_RESULT_HAL_APPLY_FAILED;
    if (servo_hal_set_output_permitted(1) &&
        servo_hal_apply_joint_command(&reference, &pwm, 0)) {
        result = servo_hal_enable() ? ROBOT_OUTPUT_RESULT_NONE
                                   : ROBOT_OUTPUT_RESULT_HAL_ENABLE_FAILED;
    }
    if (result != ROBOT_OUTPUT_RESULT_NONE) {
        ctx->servo_errors++;
        if (!servo_hal_disable()) {
            ctx->servo_errors++;
            ctx->output_faulted = 1U;
            result = ROBOT_OUTPUT_RESULT_HAL_RECOVERY_DISABLE_FAILED;
        }
        ctx->output_result = result;
        return 0;
    }
    commit_output_reference(ctx, &reference, &motion, &pwm);
    ctx->applied_command = reference;
    ctx->applied_command_valid = 1U;
    ctx->output_enabled = 1U;
    ctx->output_parked = 0U;
    ctx->output_result = ROBOT_OUTPUT_RESULT_ENABLED;
    gripper_control_reset(ctx);
    ctx->servo_writes++;
    return 1;
}

const char *servo_output_result_name(const RobotPipeline *ctx)
{
    if (ctx == NULL) return "invalid_pipeline";
    switch (ctx->output_result) {
        case ROBOT_OUTPUT_RESULT_NONE: return "none";
        case ROBOT_OUTPUT_RESULT_VIRTUAL_ACTIVE: return "rejected_virtual_compute_active";
        case ROBOT_OUTPUT_RESULT_ENABLED: return "enabled_physical_pose_not_verified";
        case ROBOT_OUTPUT_RESULT_DISABLED: return "disabled_last_written_command_latched";
        case ROBOT_OUTPUT_RESULT_REFERENCE_INVALID: return "rejected_resume_reference_invalid";
        case ROBOT_OUTPUT_RESULT_REFERENCE_LIMITS: return "rejected_resume_reference_limits";
        case ROBOT_OUTPUT_RESULT_REFERENCE_UNSAFE: return "rejected_resume_reference_FK_safety";
        case ROBOT_OUTPUT_RESULT_REFERENCE_STATE: return "rejected_resume_reference_state";
        case ROBOT_OUTPUT_RESULT_CONVERSION_FAILED: return "rejected_resume_PWM_conversion";
        case ROBOT_OUTPUT_RESULT_HAL_APPLY_FAILED: return "HAL_resume_apply_failed";
        case ROBOT_OUTPUT_RESULT_HAL_ENABLE_FAILED: return "HAL_enable_failed_output_disabled";
        case ROBOT_OUTPUT_RESULT_HAL_DISABLE_FAILED: return "HAL_disable_failed_state_preserved";
        case ROBOT_OUTPUT_RESULT_HAL_RECOVERY_DISABLE_FAILED:
            return "HAL_recovery_disable_failed_hardware_state_unverified";
        default: return "unknown_output_result";
    }
}

int servo_output_update(RobotPipeline *ctx)
{
    if (ctx == NULL) return 0;
    return servo_output_apply(ctx, &ctx->output);
}

int servo_output_apply(RobotPipeline *ctx,
                         const ForearmJointCommand *command)
{
    ServoPwmCommand pwm;

    if (ctx == NULL || command == NULL || !command->valid) return 0;
    ctx->servo_command = *command;
    ctx->servo_command_valid = 1U;
    ctx->servo_command_tick = ctx->ticks;

    /* PWM-disabled에서는 변환값을 trace용으로 보존한다. PWM-enabled에서는
     * HAL 실패 시 마지막 실제 적용 PWM을 보존하고 성공 후에만 commit한다. */
    if (!servo_control_convert(command, &pwm)) {
        ctx->servo_errors++;
        return 0;
    }
    if (!ctx->output_enabled) {
        ctx->pwm = pwm;
        if (ctx->virtual_compute) {
            /* The same gripper timing policy observes virtual command history.
             * This is not a HAL success and does not update applied_command/w. */
            gripper_motion_sample(&ctx->gripper_motion_hold, command, robot_pipeline_time_us(ctx));
            gripper_latch_applied(&ctx->gripper_latch, command->gripper_norm, robot_pipeline_time_us(ctx));
        }
        return 1;
    }
    if (!servo_hal_apply_joint_command(command, &pwm, 1)) {
        ctx->servo_errors++;
        if (servo_hal_faulted() || !servo_hal_output_permitted()) {
            ctx->output_enabled = 0U;
            ctx->output_faulted = 1U;
        }
        return 0;
    }

    ctx->pwm = pwm;
    ctx->applied_command = *command;
    ctx->applied_command_valid = 1U;
    gripper_motion_sample(&ctx->gripper_motion_hold, command, robot_pipeline_time_us(ctx));
    gripper_latch_applied(&ctx->gripper_latch, command->gripper_norm, robot_pipeline_time_us(ctx));
    ctx->servo_writes++;
    return 1;
}

int servo_output_is_current_tick(const RobotPipeline *ctx)
{
    return ctx != NULL && ctx->servo_command_valid != 0U &&
           ctx->servo_command_tick == ctx->ticks;
}
