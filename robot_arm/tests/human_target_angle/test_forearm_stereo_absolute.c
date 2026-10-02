#include "human_target_angle/agent1_forearm_stage.h"
#include "../../src/human_target_angle/forearm_mapping_internal.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    HumanPose2D image;
    HumanPose3D measured;
} StereoFixture;

static void near(float actual, float expected, float tolerance)
{
    if (!isfinite(actual) || fabsf(actual - expected) > tolerance)
        fprintf(stderr, "actual=%f expected=%f tolerance=%f\n", actual, expected, tolerance);
    assert(isfinite(actual) && fabsf(actual - expected) <= tolerance);
}

static void point_near(Point3D actual, Point3D expected)
{
    near(actual.x, expected.x, 0.001f);
    near(actual.y, expected.y, 0.001f);
    near(actual.z, expected.z, 0.001f);
    assert(actual.valid == expected.valid);
}

static Point2D project(Point3D point)
{
    Point2D image = {640.0f + 1000.0f * point.x / point.z,
                     360.0f - 1000.0f * point.y / point.z, point.valid};
    return image;
}

static StereoFixture fixture(float yaw_deg, float elevation_deg,
                              float wrist_pitch_deg, float wrist_roll_deg)
{
    StereoFixture result;
    float yaw = yaw_deg * PM_DEG_TO_RAD;
    float elevation = elevation_deg * PM_DEG_TO_RAD;
    float pitch = wrist_pitch_deg * PM_DEG_TO_RAD;
    float roll = wrist_roll_deg * PM_DEG_TO_RAD;
    Vec3 forward = pm_vec3(-sinf(yaw) * cosf(elevation), sinf(elevation),
                           -cosf(yaw) * cosf(elevation));
    Vec3 up = pm_vec3(sinf(yaw) * sinf(elevation), cosf(elevation),
                      cosf(yaw) * sinf(elevation));
    Vec3 normal = pm_vadd(pm_vscale(up, cosf(roll)),
                          pm_vscale(pm_vcross(forward, up), sinf(roll)));
    Vec3 lateral = pm_vcross(normal, forward);
    Vec3 hand_forward = pm_vsub(pm_vscale(forward, cosf(pitch)),
                                pm_vscale(normal, sinf(pitch)));
    Vec3 center;
    memset(&result, 0, sizeof(result));
    result.measured.elbow = pm_vec3(80.0f, -40.0f, 1000.0f);
    result.measured.wrist = pm_vadd(result.measured.elbow, pm_vscale(forward, 250.0f));
    result.measured.shoulder_l = pm_vec3(200.0f, 200.0f, 1000.0f);
    result.measured.shoulder_r = pm_vec3(-200.0f, 200.0f, 1000.0f);
    center = pm_vadd(result.measured.wrist, pm_vscale(hand_forward, 80.0f));
    result.measured.finger1 = pm_vsub(center, pm_vscale(lateral, 25.0f));
    result.measured.finger2 = pm_vadd(center, pm_vscale(lateral, 25.0f));
    result.image.elbow = project(result.measured.elbow);
    result.image.wrist = project(result.measured.wrist);
    result.image.shoulder_l = project(result.measured.shoulder_l);
    result.image.shoulder_r = project(result.measured.shoulder_r);
    result.image.finger1 = project(result.measured.finger1);
    result.image.finger2 = project(result.measured.finger2);
    result.image.valid = result.measured.valid = 1U;
    result.image.frame_id = result.measured.frame_id = 1U;
    return result;
}

static int update(ForearmMappingContext *context, StereoFixture *input,
                   float dt, HumanForearmTarget *target)
{
    input->measured.frame_id = input->image.frame_id;
    return forearm_mapping_update_stereo(context, &input->image, &input->measured,
                                          POSE_ARM_RIGHT, dt, target);
}

static void targets_near(HumanForearmTarget actual, HumanForearmTarget expected)
{
    near(actual.elbow_roll_deg, expected.elbow_roll_deg, 0.001f);
    near(actual.elbow_pitch_deg, expected.elbow_pitch_deg, 0.001f);
    near(actual.wrist_pitch_deg, expected.wrist_pitch_deg, 0.001f);
    near(actual.wrist_roll_deg, expected.wrist_roll_deg, 0.001f);
    near(actual.gripper_norm, expected.gripper_norm, 0.001f);
    assert(actual.valid == expected.valid);
    assert(actual.hand_fresh == expected.hand_fresh);
    assert(actual.wrist_valid == expected.wrist_valid);
    assert(actual.elbow_roll_observable == expected.elbow_roll_observable);
}

static void test_absolute_angles_and_storage(void)
{
    const float angles[][2] = {{0, 0}, {90, 0}, {-90, 0}, {-180, 0},
                               {30, 45}, {-60, -45}, {0, 90}, {0, -90}};
    unsigned index;
    for (index = 0; index < sizeof(angles) / sizeof(angles[0]); ++index) {
        ForearmMappingContext context;
        HumanForearmTarget target;
        StereoFixture input = fixture(angles[index][0], angles[index][1], 25.0f, 35.0f);
        assert(forearm_mapping_init(&context) == 0);
        assert(update(&context, &input, 0.05f, &target) == 1);
        assert(target.valid && context.stereo_input_active);
        near(target.elbow_roll_deg, angles[index][0], 0.001f);
        near(target.elbow_pitch_deg, angles[index][1], 0.001f);
        assert(target.elbow_roll_observable == (index < 6));
        if (index < 6) {
            assert(target.wrist_valid && target.hand_fresh);
            near(target.wrist_pitch_deg, 25.0f, 0.001f);
            near(target.wrist_roll_deg, 35.0f, 0.001f);
            point_near(context.pose.finger1_3d, input.measured.finger1);
            point_near(context.pose.finger2_3d, input.measured.finger2);
        }
        point_near(context.pose.elbow_3d, input.measured.elbow);
        point_near(context.pose.wrist_3d, input.measured.wrist);
        assert(!context.pose.body_frame_valid && !context.pose.camera_roll_estimate_valid);
        assert(!context.pose.finger_branch_selected_valid);
        assert(context.pose.finger_branch_elapsed_frames == 0);
    }
}

static void test_shoulders_are_optional_and_independent(void)
{
    ForearmMappingContext context;
    HumanForearmTarget baseline, target;
    StereoFixture input = fixture(32.0f, 18.0f, -20.0f, 40.0f);
    unsigned index;
    assert(forearm_mapping_init(&context) == 0);
    assert(update(&context, &input, 0.05f, &baseline) == 1);
    for (index = 0; index < 5; ++index) {
        input.image.frame_id++;
        if (index == 0) {
            input.measured.shoulder_l = pm_vec3(-800.0f, 350.0f, 600.0f);
            input.measured.shoulder_r = pm_vec3(100.0f, -80.0f, 1600.0f);
        } else if (index == 1) {
            input.measured.shoulder_r = input.measured.shoulder_l;
        } else if (index == 2) {
            input.measured.shoulder_l.valid = input.measured.shoulder_r.valid = 0U;
        } else {
            input.measured.shoulder_l = pm_vec3(NAN, INFINITY, -1.0f);
            input.measured.shoulder_r = pm_vec3(INFINITY, NAN, 0.0f);
        }
        input.image.shoulder_l = project(input.measured.shoulder_l);
        input.image.shoulder_r = project(input.measured.shoulder_r);
        assert(update(&context, &input, 0.05f, &target) == 1);
        targets_near(target, baseline);
        assert(!context.pose.body_frame_valid && !context.pose.camera_roll_estimate_valid);
    }
    assert(forearm_mapping_init(&context) == 0);
    assert(update(&context, &input, 0.05f, &target) == 1);
    targets_near(target, baseline);
}

static void test_depth_and_translation(void)
{
    ForearmMappingContext context;
    HumanForearmTarget target;
    StereoFixture input = fixture(90.0f, 0.0f, 0.0f, 0.0f);
    Point3D *points[] = {&input.measured.elbow, &input.measured.wrist,
                         &input.measured.finger1, &input.measured.finger2};
    unsigned index;
    assert(forearm_mapping_init(&context) == 0);
    assert(update(&context, &input, 0.05f, &target) == 1);
    near(target.elbow_roll_deg, 90.0f, 0.001f);
    input.measured.wrist.z -= 250.0f;
    input.measured.finger1.valid = input.measured.finger2.valid = 0U;
    assert(forearm_mapping_init(&context) == 0);
    assert(update(&context, &input, 0.05f, &target) == 1);
    near(target.elbow_roll_deg, 45.0f, 0.001f);
    near(context.pose.wrist_3d.z, 750.0f, 0.001f);
    for (index = 0; index < sizeof(points) / sizeof(points[0]); ++index) {
        points[index]->x += 500.0f;
        points[index]->y -= 100.0f;
        points[index]->z += 400.0f;
    }
    assert(forearm_mapping_init(&context) == 0);
    assert(update(&context, &input, 0.05f, &target) == 1);
    near(target.elbow_roll_deg, 45.0f, 0.001f);
    near(target.elbow_pitch_deg, 0.0f, 0.001f);
    point_near(context.pose.wrist_3d, input.measured.wrist);
}

static void test_continuity_and_poles(void)
{
    ForearmMappingContext context;
    HumanForearmTarget target;
    StereoFixture input = fixture(179.0f, 0.0f, 0.0f, 0.0f);
    float previous;
    unsigned index;
    assert(forearm_mapping_init(&context) == 0);
    assert(update(&context, &input, 0.05f, &target) == 1);
    previous = target.elbow_roll_deg;
    input = fixture(-179.0f, 0.0f, 0.0f, 0.0f);
    for (index = 0; index < 40; ++index) {
        input.image.frame_id = index + 2;
        assert(update(&context, &input, 0.05f, &target) == 1);
        assert(fabsf(fm_wrap180(target.elbow_roll_deg - previous)) < 2.1f);
        previous = target.elbow_roll_deg;
    }
    near(fm_wrap180(target.elbow_roll_deg + 179.0f), 0.0f,
          PM_JOINT_DEADBAND_DEG + 0.01f);
    input = fixture(45.0f, 0.0f, 0.0f, 0.0f);
    assert(forearm_mapping_init(&context) == 0);
    assert(update(&context, &input, 0.05f, &target) == 1);
    input = fixture(0.0f, 90.0f, 0.0f, 0.0f);
    input.image.frame_id = 2;
    assert(update(&context, &input, 0.05f, &target) == 1);
    near(target.elbow_roll_deg, 45.0f, 0.001f);
    assert(!target.elbow_roll_observable && isfinite(target.wrist_roll_deg));
}

static void test_hand_missing_and_degenerate(void)
{
    ForearmMappingContext context;
    HumanForearmTarget previous, target;
    StereoFixture input = fixture(45.0f, 20.0f, 25.0f, 35.0f);
    assert(forearm_mapping_init(&context) == 0);
    assert(update(&context, &input, 0.05f, &previous) == 1);
    assert(previous.hand_fresh && previous.wrist_valid);
    input.measured.finger1.valid = 0U;
    input.image.frame_id++;
    input.measured.wrist.z += 100.0f;
    assert(update(&context, &input, 0.05f, &target) == 1);
    assert(target.valid && target.wrist_valid && !target.hand_fresh);
    assert(!context.pose.finger_pose3d_valid);
    near(target.wrist_pitch_deg, previous.wrist_pitch_deg, 0.0f);
    near(target.wrist_roll_deg, previous.wrist_roll_deg, 0.0f);
    near(target.gripper_norm, previous.gripper_norm, 0.0f);
    input.measured.finger1 = input.measured.finger2;
    input.image.frame_id++;
    assert(update(&context, &input, 0.05f, &target) == 1);
    assert(target.valid && !target.hand_fresh);
    near(target.wrist_pitch_deg, previous.wrist_pitch_deg, 0.0f);
    near(target.wrist_roll_deg, previous.wrist_roll_deg, 0.0f);
    assert(forearm_mapping_init(&context) == 0);
    assert(update(&context, &input, 0.05f, &target) == 1);
    assert(target.valid && !target.wrist_valid && !target.hand_fresh);
    input = fixture(45.0f, 20.0f, 25.0f, 35.0f);
    input.measured.finger2.x = NAN;
    assert(forearm_mapping_init(&context) == 0);
    assert(update(&context, &input, NAN, &target) == 1);
    assert(target.valid && !target.hand_fresh);
    assert(isfinite(target.wrist_pitch_deg) && isfinite(target.wrist_roll_deg));
}

static void test_invalid_and_duplicate(void)
{
    ForearmMappingContext context;
    HumanForearmTarget target;
    StereoFixture input = fixture(30.0f, 15.0f, 10.0f, 20.0f);
    unsigned index;
    assert(forearm_mapping_init(&context) == 0);
    assert(update(&context, &input, 0.05f, &target) == 1);
    input.measured.wrist.x = NAN;
    assert(update(&context, &input, 100.0f, &target) == 0);
    near(context.pose.target_age_sec, 0.0f, 0.0f);
    assert(target.valid && !target.hand_fresh);
    input.image.frame_id++;
    assert(update(&context, &input, 0.05f, &target) == 0);
    assert(target.valid && target.frame_id == 1U);
    input.image.frame_id++;
    assert(update(&context, &input, PM_TARGET_HOLD_SEC + 0.1f, &target) == -1);
    assert(!target.valid && isfinite(target.elbow_roll_deg));
    for (index = 0; index < 6; ++index) {
        input = fixture(30.0f, 15.0f, 10.0f, 20.0f);
        if (index == 0) input.measured.wrist = input.measured.elbow;
        else if (index == 1) input.measured.wrist.z = 0.0f;
        else if (index == 2) input.measured.wrist.y = INFINITY;
        else if (index == 3) {
            input.measured.elbow.x = -FLT_MAX;
            input.measured.wrist.x = FLT_MAX;
        } else if (index == 4) input.image.elbow.x = NAN;
        else input.measured.wrist.valid = 0U;
        assert(forearm_mapping_init(&context) == 0);
        assert(update(&context, &input, 0.05f, &target) == -1);
        assert(!target.valid && isfinite(target.elbow_roll_deg) &&
               isfinite(target.elbow_pitch_deg));
    }
    input = fixture(30.0f, 15.0f, 10.0f, 20.0f);
    assert(forearm_mapping_init(&context) == 0);
    input.measured.frame_id++;
    assert(forearm_mapping_update_stereo(&context, &input.image, &input.measured,
                                         POSE_ARM_RIGHT, 0.05f, &target) == -1);
}

static void test_person_axes_and_recorded_pose(void)
{
    ForearmMappingContext context;
    HumanForearmTarget target;
    StereoFixture input = fixture(30.0f, 0.0f, 0.0f, 0.0f);
    assert(input.measured.wrist.x < input.measured.elbow.x);
    assert(input.measured.wrist.z < input.measured.elbow.z);
    assert(forearm_mapping_init(&context) == 0);
    assert(update(&context, &input, 0.05f, &target) == 1);
    near(target.elbow_roll_deg, 30.0f, 0.001f);
    input = fixture(-30.0f, 0.0f, 0.0f, 0.0f);
    assert(input.measured.wrist.x > input.measured.elbow.x);
    assert(forearm_mapping_init(&context) == 0);
    assert(update(&context, &input, 0.05f, &target) == 1);
    near(target.elbow_roll_deg, -30.0f, 0.001f);
    input.measured.elbow = pm_vec3(-236.007f, -282.937f, 1553.784f);
    input.measured.wrist = pm_vec3(-315.109f, -23.009f, 1217.803f);
    input.measured.finger1.valid = input.measured.finger2.valid = 0U;
    input.image.elbow = project(input.measured.elbow);
    input.image.wrist = project(input.measured.wrist);
    assert(forearm_mapping_init(&context) == 0);
    assert(update(&context, &input, 0.05f, &target) == 1);
    near(target.elbow_roll_deg, 13.2482f, 0.001f);
    near(target.elbow_pitch_deg, 36.9816f, 0.001f);
    point_near(context.pose.elbow_3d, input.measured.elbow);
    point_near(context.pose.wrist_3d, input.measured.wrist);
    assert(!target.wrist_valid && !context.pose.body_frame_valid);
}

static void test_stage_and_mono_mode_isolation(void)
{
    ForearmMappingContext stereo_context, mono_context;
    HumanForearmTarget stereo_target, mono_target;
    StereoFixture input = fixture(25.0f, 20.0f, 10.0f, 20.0f);
    HumanPose2D mono = {{528, 238, 1}, {535, 278, 1}, {445, 225, 1},
                        {500, 255, 1}, {250, 190, 1}, {390, 190, 1}, 123, 1};
    unsigned index;
    assert(agent1_forearm_stage_init() == 0);
    assert(agent1_forearm_stage_run_stereo(&input.image, &input.measured,
                                          POSE_ARM_RIGHT, 0.05f) == 1);
    near(agent1_forearm_stage_output()->elbow_roll_deg, 25.0f, 0.001f);
    assert(forearm_mapping_init(&stereo_context) == 0);
    assert(forearm_mapping_init(&mono_context) == 0);
    assert(update(&stereo_context, &input, 0.05f, &stereo_target) == 1);
    for (index = 0; index < 65; ++index) {
        int stereo_status, mono_status;
        mono.frame_id++;
        stereo_status = forearm_mapping_update(&stereo_context, &mono, POSE_ARM_RIGHT,
                                                0.05f, &stereo_target);
        mono_status = forearm_mapping_update(&mono_context, &mono, POSE_ARM_RIGHT,
                                              0.05f, &mono_target);
        assert(stereo_status == mono_status);
        targets_near(stereo_target, mono_target);
    }
    assert(!stereo_context.stereo_input_active && stereo_context.pose.body_frame_valid);
    assert(stereo_target.valid);
    assert(update(&stereo_context, &input, 0.05f, &stereo_target) == 1);
    assert(stereo_context.stereo_input_active && !stereo_context.pose.body_frame_valid);
    point_near(stereo_context.pose.wrist_3d, input.measured.wrist);
}

int main(void)
{
    test_absolute_angles_and_storage();
    test_shoulders_are_optional_and_independent();
    test_depth_and_translation();
    test_continuity_and_poles();
    test_hand_missing_and_degenerate();
    test_invalid_and_duplicate();
    test_person_axes_and_recorded_pose();
    test_stage_and_mono_mode_isolation();
    puts("test_forearm_stereo_absolute: PASS (8 groups, person right/up/forward axes)");
    return 0;
}
