#include "stereo_vision/stereo_pose_filter.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifdef STEREO_POSE_FILTER_TEST_GRIPPER
#include "human_target_angle/forearm_mapping.h"
#endif

static HumanPose3D fixture(void)
{
    HumanPose3D pose;
    memset(&pose, 0, sizeof(pose));
    pose.elbow = (Point3D){0.0f, 0.0f, 1500.0f, 1U};
    pose.wrist = (Point3D){0.0f, 100.0f, 1250.0f, 1U};
    pose.finger1 = (Point3D){-25.0f, 130.0f, 1200.0f, 1U};
    pose.finger2 = (Point3D){25.0f, 130.0f, 1200.0f, 1U};
    pose.shoulder_l = (Point3D){-200.0f, 200.0f, 1500.0f, 1U};
    pose.shoulder_r = (Point3D){200.0f, 200.0f, 1500.0f, 1U};
    pose.frame_id = 42U;
    pose.valid = 1U;
    return pose;
}

static HumanPose3D flat_fixture(float depth, float forearm_length)
{
    HumanPose3D pose = fixture();
    pose.elbow = (Point3D){0.0f, 0.0f, depth, 1U};
    pose.wrist = (Point3D){0.0f, forearm_length, depth, 1U};
    pose.finger1 = (Point3D){-25.0f, forearm_length + 50.0f, depth, 1U};
    pose.finger2 = (Point3D){25.0f, forearm_length + 50.0f, depth, 1U};
    return pose;
}

static Point3D *point_at(HumanPose3D *pose, unsigned index)
{
    Point3D *points[] = {&pose->elbow, &pose->wrist, &pose->finger1, &pose->finger2};
    assert(index < STEREO_POSE_FILTER_POINT_COUNT);
    return points[index];
}

static void set_coordinate(Point3D *point, unsigned axis, float value)
{
    if (axis == 0U) point->x = value;
    else if (axis == 1U) point->y = value;
    else point->z = value;
}

static void assert_point_reset(const StereoOneEuro *state)
{
    unsigned axis;
    assert(!state->have_time && state->last_time_us == 0U);
    for (axis = 0U; axis < 3U; ++axis) {
        assert(state->previous_raw[axis] == 0.0f);
        assert(state->filtered[axis] == 0.0f);
        assert(state->derivative[axis] == 0.0f);
    }
}

static void assert_points_reset(const StereoPoseFilter *state)
{
    unsigned index;
    for (index = 0U; index < STEREO_POSE_FILTER_POINT_COUNT; ++index)
        assert_point_reset(&state->euro[index]);
}

static void assert_point_zero(Point3D point)
{
    assert(!point.valid);
    assert(point.x == 0.0f && point.y == 0.0f && point.z == 0.0f);
}

static void assert_pose_zero(const HumanPose3D *pose)
{
    HumanPose3D empty;
    memset(&empty, 0, sizeof(empty));
    assert(!memcmp(pose, &empty, sizeof(empty)));
}

static void test_initialization_and_arguments(void)
{
    StereoPoseFilter state;
    HumanPose3D pose = fixture();
    HumanPose3D unchanged = pose;
    unsigned index;

    stereo_pose_filter_init(NULL);
    assert(!stereo_pose_filter_apply(NULL, &pose, 0.02f));
    assert(!memcmp(&pose, &unchanged, sizeof(pose)));
    stereo_pose_filter_init(&state);
    assert(state.time_us == 0U);
    assert_points_reset(&state);
    for (index = 0U; index < STEREO_POSE_FILTER_POINT_COUNT; ++index) {
        assert(state.euro[index].config.minimum_cutoff_hz == 0.5f);
        assert(state.euro[index].config.beta_per_mm == 0.001f);
        assert(state.euro[index].config.derivative_cutoff_hz == 1.0f);
        state.euro[index].config = (StereoOneEuroConfig){2.0f, 0.0f, 3.0f};
    }
    assert(stereo_pose_filter_apply(&state, &pose, 0.02f));
    assert(!stereo_pose_filter_apply(&state, NULL, 0.02f));
    assert_points_reset(&state);
    assert(state.time_us == 0U);
    for (index = 0U; index < STEREO_POSE_FILTER_POINT_COUNT; ++index) {
        assert(state.euro[index].config.minimum_cutoff_hz == 2.0f);
        assert(state.euro[index].config.beta_per_mm == 0.0f);
        assert(state.euro[index].config.derivative_cutoff_hz == 3.0f);
    }
    pose = fixture();
    pose.valid = 0U;
    assert(!stereo_pose_filter_apply(&state, &pose, 0.02f));
    assert_pose_zero(&pose);
    assert_points_reset(&state);
    assert(state.time_us == 0U);
}

static void test_dt_and_clock_guards(void)
{
    StereoPoseFilter state;
    HumanPose3D pose;
    const float invalid_dt[] = {0.0f, -0.02f, NAN, INFINITY, -INFINITY, 0.4e-6f,
                                nextafterf(3600.0f, INFINITY)};
    unsigned index;

    for (index = 0U; index < sizeof(invalid_dt) / sizeof(invalid_dt[0]); ++index) {
        stereo_pose_filter_init(&state);
        pose = fixture();
        assert(stereo_pose_filter_apply(&state, &pose, 0.02f));
        pose = fixture();
        assert(!stereo_pose_filter_apply(&state, &pose, invalid_dt[index]));
        assert_pose_zero(&pose);
        assert_points_reset(&state);
        assert(state.time_us == 0U);
        pose = fixture();
        pose.wrist.x = 50.0f;
        assert(stereo_pose_filter_apply(&state, &pose, 0.02f));
        assert(pose.wrist.x == 50.0f);
    }
    stereo_pose_filter_init(&state);
    pose = fixture();
    assert(stereo_pose_filter_apply(&state, &pose, 0.6e-6f));
    assert(state.time_us == 1U);
    pose = fixture();
    assert(stereo_pose_filter_apply(&state, &pose, 3600.0f));
    assert(state.time_us == UINT64_C(3600000001));
    state.time_us = UINT64_MAX - 10000U;
    pose = fixture();
    assert(!stereo_pose_filter_apply(&state, &pose, 0.02f));
    assert_pose_zero(&pose);
    assert_points_reset(&state);
    assert(state.time_us == 0U);
    state.time_us = UINT64_MAX - 1U;
    pose = fixture();
    assert(stereo_pose_filter_apply(&state, &pose, 0.6e-6f));
    assert(state.time_us == UINT64_MAX);
    pose = fixture();
    assert(!stereo_pose_filter_apply(&state, &pose, 0.6e-6f));
    assert_pose_zero(&pose);
    assert_points_reset(&state);
    assert(state.time_us == 0U);
}

static void test_gap_boundary(void)
{
    const float elapsed[] = {0.499999f, 0.5f, 0.500001f, 0.7f};
    StereoPoseFilter state;
    HumanPose3D pose;
    unsigned index;

    for (index = 0U; index < sizeof(elapsed) / sizeof(elapsed[0]); ++index) {
        stereo_pose_filter_init(&state);
        pose = fixture();
        assert(stereo_pose_filter_apply(&state, &pose, 0.02f));
        pose = fixture();
        pose.wrist.x = 50.0f;
        assert(stereo_pose_filter_apply(&state, &pose, elapsed[index]));
        if (elapsed[index] <= 0.5f) {
            assert(pose.wrist.x > 0.0f && pose.wrist.x < 50.0f);
            assert(state.euro[1].derivative[0] > 0.0f);
        } else {
            assert(pose.wrist.x == 50.0f && state.euro[1].derivative[0] == 0.0f);
        }
    }
}

static void test_observe_invalid_before_admission(void)
{
    StereoPoseFilter state;
    StereoPoseFilter previous;
    HumanPose3D raw;
    HumanPose3D unchanged;
    unsigned index;
    unsigned failure;
    unsigned other;

    stereo_pose_filter_observe_invalid(NULL, NULL);
    for (index = 0U; index < STEREO_POSE_FILTER_POINT_COUNT; ++index) {
        for (failure = 0U; failure < 4U; ++failure) {
            HumanPose3D pose = fixture();
            float expected_x;
            stereo_pose_filter_init(&state);
            state.euro[index].config = (StereoOneEuroConfig){2.0f, 0.0f, 3.0f};
            assert(stereo_pose_filter_apply(&state, &pose, 0.02f));
            previous = state;
            raw = fixture();
            if (failure == 0U) point_at(&raw, index)->valid = 0U;
            else set_coordinate(point_at(&raw, index), failure - 1U,
                                failure == 2U ? INFINITY : NAN);
            unchanged = raw;
            stereo_pose_filter_observe_invalid(&state, &raw);
            assert(!memcmp(&raw, &unchanged, sizeof(raw)));
            assert(state.time_us == previous.time_us);
            assert_point_reset(&state.euro[index]);
            assert(state.euro[index].config.minimum_cutoff_hz == 2.0f);
            assert(state.euro[index].config.beta_per_mm == 0.0f);
            assert(state.euro[index].config.derivative_cutoff_hz == 3.0f);
            for (other = 0U; other < STEREO_POSE_FILTER_POINT_COUNT; ++other)
                if (other != index)
                    assert(!memcmp(&state.euro[other], &previous.euro[other],
                                   sizeof(state.euro[other])));
            pose = fixture();
            point_at(&pose, index)->x += 20.0f;
            expected_x = point_at(&pose, index)->x;
            assert(stereo_pose_filter_apply(&state, &pose, 0.02f));
            assert(point_at(&pose, index)->x == expected_x);
        }
    }
    stereo_pose_filter_init(&state);
    raw = fixture();
    assert(stereo_pose_filter_apply(&state, &raw, 0.02f));
    previous = state;
    stereo_pose_filter_observe_invalid(&state, NULL);
    assert(!memcmp(&state, &previous, sizeof(state)));
    raw = fixture();
    raw.shoulder_l.x = NAN;
    raw.shoulder_r.valid = 0U;
    stereo_pose_filter_observe_invalid(&state, &raw);
    assert(!memcmp(&state, &previous, sizeof(state)));
    raw.valid = 0U;
    stereo_pose_filter_observe_invalid(&state, &raw);
    assert_points_reset(&state);
    assert(state.time_us == previous.time_us);
}

static void test_invalid_point_independence(void)
{
    const float invalid[] = {NAN, INFINITY, -INFINITY};
    StereoPoseFilter state;
    unsigned index;
    unsigned axis;
    unsigned failure;

    for (index = 0U; index < STEREO_POSE_FILTER_POINT_COUNT; ++index) {
        for (axis = 0U; axis < 3U; ++axis) {
            for (failure = 0U; failure < 4U; ++failure) {
                HumanPose3D pose = fixture();
                stereo_pose_filter_init(&state);
                assert(stereo_pose_filter_apply(&state, &pose, 0.02f));
                pose = fixture();
                if (failure == 3U) point_at(&pose, index)->valid = 0U;
                else set_coordinate(point_at(&pose, index), axis, invalid[failure]);
                assert(stereo_pose_filter_apply(&state, &pose, 0.02f) == (index >= 2U));
                assert(pose.valid == (index >= 2U));
                assert(pose.frame_id == 42U);
                if (index < 2U) {
                    assert_points_reset(&state);
                } else {
                    assert(pose.elbow.valid && pose.wrist.valid);
                    assert(state.euro[0].have_time && state.euro[1].have_time);
                    assert_point_zero(pose.finger1);
                    assert_point_zero(pose.finger2);
                    assert_point_reset(&state.euro[2]);
                    assert_point_reset(&state.euro[3]);
                }
                pose = fixture();
                pose.finger1.x = -40.0f;
                pose.finger2.x = 40.0f;
                assert(stereo_pose_filter_apply(&state, &pose, 0.02f));
                assert(pose.finger1.x == -40.0f && pose.finger2.x == 40.0f);
            }
        }
    }
}

static void test_major_geometry_boundaries(void)
{
    const float depths[] = {nextafterf(300.0f, 0.0f), 300.0f, 3000.0f,
                            nextafterf(3000.0f, INFINITY)};
    const float lengths[] = {nextafterf(80.0f, 0.0f), 80.0f, 600.0f,
                             nextafterf(600.0f, INFINITY)};
    StereoPoseFilter state;
    unsigned index;
    unsigned major;

    for (major = 0U; major < 2U; ++major) {
        for (index = 0U; index < sizeof(depths) / sizeof(depths[0]); ++index) {
            HumanPose3D pose = flat_fixture(index < 2U ? 300.0f : 3000.0f, 200.0f);
            int expected = index == 1U || index == 2U;
            point_at(&pose, major)->z = depths[index];
            stereo_pose_filter_init(&state);
            assert(stereo_pose_filter_apply(&state, &pose, 0.02f) == expected);
            assert(pose.valid == expected);
            if (!expected) {
                assert_points_reset(&state);
                assert(state.time_us == 20000U);
                assert(pose.frame_id == 42U && pose.elbow.valid && pose.wrist.valid);
            }
        }
    }
    for (index = 0U; index < sizeof(lengths) / sizeof(lengths[0]); ++index) {
        HumanPose3D pose = flat_fixture(1500.0f, lengths[index]);
        int expected = index == 1U || index == 2U;
        stereo_pose_filter_init(&state);
        assert(stereo_pose_filter_apply(&state, &pose, 0.02f) == expected);
        if (!expected) assert_points_reset(&state);
    }
    {
        HumanPose3D pose = fixture();
        stereo_pose_filter_init(&state);
        pose.elbow.x = -FLT_MAX;
        pose.wrist.x = FLT_MAX;
        assert(!stereo_pose_filter_apply(&state, &pose, 0.02f));
        assert_points_reset(&state);
    }
}

static void test_postfilter_geometry(void)
{
    StereoPoseFilter state;
    HumanPose3D pose = flat_fixture(1500.0f, 200.0f);

    stereo_pose_filter_init(&state);
    assert(stereo_pose_filter_apply(&state, &pose, 0.02f));
    pose = flat_fixture(1500.0f, 0.0f);
    assert(stereo_pose_filter_apply(&state, &pose, 0.001f));
    assert(pose.wrist.y >= 80.0f && pose.wrist.y < 200.0f);
    stereo_pose_filter_init(&state);
    state.euro[1].config.minimum_cutoff_hz = 1.0f;
    state.euro[1].config.beta_per_mm = 0.0f;
    pose = flat_fixture(1500.0f, 200.0f);
    assert(stereo_pose_filter_apply(&state, &pose, 0.02f));
    pose = flat_fixture(1500.0f, -200.0f);
    assert(!stereo_pose_filter_apply(&state, &pose, 0.1f));
    assert(fabsf(pose.wrist.y) < 80.0f);
    assert_points_reset(&state);
    pose = flat_fixture(1500.0f, -200.0f);
    assert(stereo_pose_filter_apply(&state, &pose, 0.02f));
    assert(pose.wrist.y == -200.0f);
}

static void test_finger_geometry_and_recovery(void)
{
    const float spans[] = {nextafterf(10.0f, 0.0f), 10.0f, 250.0f,
                           nextafterf(250.0f, INFINITY)};
    StereoPoseFilter state;
    unsigned index;
    unsigned finger;

    for (index = 0U; index < sizeof(spans) / sizeof(spans[0]); ++index) {
        HumanPose3D pose = flat_fixture(1500.0f, 200.0f);
        int expected = index == 1U || index == 2U;
        pose.finger1 = pose.wrist;
        pose.finger2 = pose.wrist;
        pose.finger2.x = spans[index];
        stereo_pose_filter_init(&state);
        assert(stereo_pose_filter_apply(&state, &pose, 0.02f));
        assert(pose.valid && pose.finger1.valid == expected && pose.finger2.valid == expected);
        if (!expected) {
            assert_point_zero(pose.finger1);
            assert_point_zero(pose.finger2);
            assert_point_reset(&state.euro[2]);
            assert_point_reset(&state.euro[3]);
        }
    }
    for (finger = 2U; finger < 4U; ++finger) {
        for (index = 0U; index < 2U; ++index) {
            HumanPose3D pose = flat_fixture(1500.0f, 200.0f);
            unsigned other = finger == 2U ? 3U : 2U;
            *point_at(&pose, finger) = pose.wrist;
            *point_at(&pose, other) = pose.wrist;
            point_at(&pose, finger)->x = index ? nextafterf(250.0f, INFINITY) : 250.0f;
            point_at(&pose, other)->x = 230.0f;
            stereo_pose_filter_init(&state);
            assert(stereo_pose_filter_apply(&state, &pose, 0.02f));
            assert(pose.valid && pose.finger1.valid == !index && pose.finger2.valid == !index);
            if (index) {
                assert_point_zero(pose.finger1);
                assert_point_zero(pose.finger2);
                assert(state.euro[0].have_time && state.euro[1].have_time);
                pose = flat_fixture(1500.0f, 200.0f);
                assert(stereo_pose_filter_apply(&state, &pose, 0.02f));
                assert(pose.finger1.x == -25.0f && pose.finger2.x == 25.0f);
            }
        }
    }
    {
        HumanPose3D pose = flat_fixture(1500.0f, 200.0f);
        pose.finger1 = pose.wrist;
        pose.finger2 = pose.wrist;
        pose.finger1.x = -126.0f;
        pose.finger2.x = 126.0f;
        stereo_pose_filter_init(&state);
        assert(stereo_pose_filter_apply(&state, &pose, 0.02f));
        assert_point_zero(pose.finger1);
        assert_point_zero(pose.finger2);
    }
}

static void test_shoulders_and_frame_identity(void)
{
    StereoPoseFilter state;
    HumanPose3D pose = fixture();
    unsigned sample;

    stereo_pose_filter_init(&state);
    assert(stereo_pose_filter_apply(&state, &pose, 0.02f));
    for (sample = 0U; sample < 3U; ++sample) {
        HumanPose3D unchanged;
        pose = fixture();
        pose.frame_id = sample == 2U ? UINT32_MAX : sample;
        pose.wrist.x = 50.0f;
        pose.shoulder_l = (Point3D){NAN, INFINITY, -INFINITY, (uint8_t)(sample % 2U)};
        pose.shoulder_r = (Point3D){10000.0f, -5000.0f, -1.0f, 1U};
        unchanged = pose;
        assert(stereo_pose_filter_apply(&state, &pose, 0.02f));
        assert(!memcmp(&pose.shoulder_l, &unchanged.shoulder_l, sizeof(pose.shoulder_l)));
        assert(!memcmp(&pose.shoulder_r, &unchanged.shoulder_r, sizeof(pose.shoulder_r)));
        assert(pose.frame_id == unchanged.frame_id);
        assert(pose.wrist.x > 0.0f && pose.wrist.x < 50.0f);
    }
}

#ifdef STEREO_POSE_FILTER_TEST_GRIPPER
static void test_independent_2d_gripper(void)
{
    unsigned failure;
    for (failure = 0U; failure < 4U; ++failure) {
        StereoPoseFilter state;
        ForearmMappingContext mapping;
        HumanForearmTarget target;
        HumanPose3D pose = fixture();
        HumanPose2D image;
        HumanPose2D unchanged;
        memset(&image, 0, sizeof(image));
        image.valid = 1U;
        image.frame_id = pose.frame_id;
        image.elbow = (Point2D){300.0f, 200.0f, 1U};
        image.wrist = (Point2D){300.0f, 300.0f, 1U};
        image.finger1 = (Point2D){300.0f, 340.0f, 1U};
        image.finger2 = image.finger1;
        image.gripper_2d.wrist = image.wrist;
        image.gripper_2d.finger1 = image.finger1;
        image.gripper_2d.finger2 = image.finger2;
        image.gripper_2d.source = 2U;
        image.gripper_2d.reference_span_px = 100.0f;
        unchanged = image;
        if (failure == 0U) pose.finger1.valid = 0U;
        else if (failure == 1U) pose.finger2.z = NAN;
        else if (failure == 2U) pose.finger2 = pose.finger1;
        else pose.finger1.x = 1000.0f;
        stereo_pose_filter_init(&state);
        assert(stereo_pose_filter_apply(&state, &pose, 0.02f));
        assert_point_zero(pose.finger1);
        assert_point_zero(pose.finger2);
        assert(!memcmp(&image, &unchanged, sizeof(image)));
        assert(forearm_mapping_init(&mapping) == 0);
        assert(forearm_mapping_update_stereo(&mapping, &image, &pose,
                                            POSE_ARM_RIGHT, 0.02f, &target) == 1);
        assert(target.valid && target.gripper_valid);
        assert(!target.wrist_valid && !target.hand_fresh);
        assert(target.gripper_norm == 0.0f);
    }
    puts("test_stereo_pose_filter: independent 2D gripper PASS (4 partial hand failures)");
}
#endif

int main(void)
{
    test_initialization_and_arguments();
    test_dt_and_clock_guards();
    test_gap_boundary();
    test_observe_invalid_before_admission();
    test_invalid_point_independence();
    test_major_geometry_boundaries();
    test_postfilter_geometry();
    test_finger_geometry_and_recovery();
    test_shoulders_and_frame_identity();
#ifdef STEREO_POSE_FILTER_TEST_GRIPPER
    test_independent_2d_gripper();
#endif
    puts("test_stereo_pose_filter: PASS (9 test groups)");
    return 0;
}
