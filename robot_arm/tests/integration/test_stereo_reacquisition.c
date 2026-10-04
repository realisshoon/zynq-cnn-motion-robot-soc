#include "integration/input_pose.h"
#include "../../src/integration/input_pose_cnn.h"
#include "xtime_l.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static XTime now;
void XTime_GetTime(XTime *time) { *time = now; }

static void reset_input(void)
{
    now = 1000000U;
    input_pose_cnn_set_stereo(1);
    input_pose_cnn_set_async_test(1);
}

static StereoDepthResult observation(uint32_t sequence, float offset)
{
    StereoDepthResult result;
    memset(&result, 0, sizeof(result));
    now += 100000U;
    result.left_session_id = 11U;
    result.right_session_id = 22U;
    result.left_sequence = sequence;
    result.right_sequence = sequence;
    result.left_frame_id = 700U + sequence;
    result.right_frame_id = 1500U + sequence;
    result.left_received_us = now - 1000U;
    result.right_received_us = now;
    result.control_status = STEREO_POSE_UNVERIFIED;
    result.async_test = 1U;
    result.async_status = STEREO_POSE_OK;
    result.image_pose.valid = 1U;
    result.image_pose.frame_id = result.left_frame_id;
    result.image_pose.elbow = (Point2D){500.0f, 300.0f, 1U};
    result.image_pose.wrist = (Point2D){500.0f, 500.0f, 1U};
    result.async_measured_pose.valid = 1U;
    result.async_measured_pose.frame_id = result.left_frame_id;
    result.async_measured_pose.elbow = (Point3D){offset, 0.0f, 1000.0f, 1U};
    result.async_measured_pose.wrist = (Point3D){offset, 200.0f, 1000.0f, 1U};
    return result;
}

static void take_at(float offset, float expected_dt)
{
    HumanPose2D image;
    HumanPose3D measured;
    float dt;
    assert(input_pose_cnn_take_stereo(&image, &measured, &dt));
    assert(image.valid && measured.valid);
    assert(fabsf(measured.wrist.x - offset) < 0.001f);
    assert(fabsf(dt - expected_dt) < 0.00001f);
    assert(!input_pose_ready());
}

static void start_tracking(void)
{
    StereoDepthResult result = observation(1U, 0.0f);
    assert(input_pose_cnn_publish_stereo(&result));
    take_at(0.0f, 0.1f);
}

static void test_reacquire_new_location(void)
{
    StereoDepthResult result;
    uint32_t epoch;
    reset_input();
    start_tracking();
    epoch = input_pose_cnn_filter_epoch();
    result = observation(2U, 300.0f);
    assert(!input_pose_cnn_publish_stereo(&result));
    assert(strcmp(input_pose_cnn_admission_reason(), "JUMP_HOLD") == 0);
    assert(strcmp(input_pose_cnn_tracking_state(), "HOLD") == 0);
    assert(input_pose_cnn_reacquire_count() == 1U);
    assert(!input_pose_ready() && input_pose_cnn_filter_epoch() == epoch);
    assert(!input_pose_cnn_publish_stereo(&result));
    assert(strcmp(input_pose_cnn_admission_reason(), "SEQUENCE") == 0);
    assert(input_pose_cnn_reacquire_count() == 1U);
    result = observation(3U, 320.0f);
    assert(!input_pose_cnn_publish_stereo(&result));
    assert(strcmp(input_pose_cnn_admission_reason(), "REACQ_WAIT") == 0);
    assert(input_pose_cnn_reacquire_count() == 2U);
    result = observation(4U, 310.0f);
    assert(input_pose_cnn_publish_stereo(&result));
    assert(strcmp(input_pose_cnn_admission_reason(), "REACQ_ACCEPT") == 0);
    assert(strcmp(input_pose_cnn_tracking_state(), "TRACK") == 0);
    assert(input_pose_cnn_reacquire_count() == 0U);
    assert(input_pose_cnn_filter_epoch() == epoch + 1U);
    take_at(310.0f, 0.1f);
    result = observation(5U, 320.0f);
    assert(input_pose_cnn_publish_stereo(&result));
    assert(strcmp(input_pose_cnn_admission_reason(), "OK") == 0);
    assert(input_pose_cnn_filter_epoch() == epoch + 1U);
    take_at(320.0f, 0.1f);
}

static void test_spike_and_fixed_anchor(void)
{
    StereoDepthResult result;
    reset_input();
    start_tracking();
    result = observation(2U, 300.0f);
    assert(!input_pose_cnn_publish_stereo(&result));
    result = observation(3U, 10.0f);
    assert(input_pose_cnn_publish_stereo(&result));
    assert(input_pose_cnn_reacquire_count() == 0U);
    take_at(10.0f, 0.2f);
    result = observation(4U, 300.0f);
    assert(!input_pose_cnn_publish_stereo(&result));
    result = observation(5U, 340.0f);
    assert(!input_pose_cnn_publish_stereo(&result));
    assert(input_pose_cnn_reacquire_count() == 2U);
    result = observation(6U, 380.0f);
    assert(!input_pose_cnn_publish_stereo(&result));
    assert(input_pose_cnn_reacquire_count() == 1U);
    assert(!input_pose_ready());
}

static void test_bad_observations_clear_candidates(void)
{
    StereoDepthResult result;
    reset_input();
    start_tracking();
    result = observation(2U, 300.0f);
    assert(!input_pose_cnn_publish_stereo(&result));
    result = observation(3U, 300.0f);
    result.async_status = STEREO_POSE_GEOMETRY;
    assert(!input_pose_cnn_publish_stereo(&result));
    assert(strcmp(input_pose_cnn_admission_reason(), "GEOMETRY") == 0);
    assert(input_pose_cnn_reacquire_count() == 0U);
    result = observation(4U, 300.0f);
    assert(!input_pose_cnn_publish_stereo(&result));
    result = observation(5U, 300.0f);
    result.async_measured_pose.elbow.x = NAN;
    assert(!input_pose_cnn_publish_stereo(&result));
    assert(strcmp(input_pose_cnn_admission_reason(), "INVALID") == 0);
    assert(input_pose_cnn_reacquire_count() == 0U);
    result = observation(6U, 300.0f);
    assert(!input_pose_cnn_publish_stereo(&result));
    result = observation(7U, 300.0f);
    result.left_received_us = now - 100001U;
    assert(!input_pose_cnn_publish_stereo(&result));
    assert(strcmp(input_pose_cnn_admission_reason(), "MATCH_GAP") == 0);
    assert(input_pose_cnn_reacquire_count() == 0U);
    result = observation(8U, 300.0f);
    result.left_received_us = now - 250001U;
    assert(!input_pose_cnn_publish_stereo(&result));
    assert(strcmp(input_pose_cnn_admission_reason(), "STALE") == 0);
    result = observation(9U, 300.0f);
    result.right_received_us = now + 1U;
    assert(!input_pose_cnn_publish_stereo(&result));
    assert(strcmp(input_pose_cnn_admission_reason(), "CLOCK") == 0);
    assert(input_pose_cnn_reacquire_count() == 0U);
}

static void test_candidate_timeout_and_session(void)
{
    StereoDepthResult result;
    reset_input();
    start_tracking();
    result = observation(2U, 300.0f);
    assert(!input_pose_cnn_publish_stereo(&result));
    now += 250001U;
    result = observation(3U, 300.0f);
    assert(!input_pose_cnn_publish_stereo(&result));
    assert(input_pose_cnn_reacquire_count() == 1U);
    result = observation(1U, 300.0f);
    result.left_session_id++;
    result.right_sequence = 4U;
    assert(!input_pose_cnn_publish_stereo(&result));
    assert(input_pose_cnn_reacquire_count() == 1U);
    result = observation(2U, 300.0f);
    result.left_session_id++;
    result.right_sequence = 5U;
    assert(!input_pose_cnn_publish_stereo(&result));
    assert(input_pose_cnn_reacquire_count() == 2U);
    result = observation(3U, 300.0f);
    result.left_session_id++;
    result.right_sequence = 6U;
    assert(input_pose_cnn_publish_stereo(&result));
    assert(strcmp(input_pose_cnn_admission_reason(), "REACQ_ACCEPT") == 0);
    take_at(300.0f, 0.1f);
}

static void test_pending_age_and_filter_epoch(void)
{
    StereoDepthResult result;
    HumanPose2D image;
    HumanPose3D measured;
    float dt;
    uint32_t epoch;
    reset_input();
    result = observation(UINT32_MAX, 0.0f);
    assert(input_pose_cnn_publish_stereo(&result));
    take_at(0.0f, 0.1f);
    epoch = input_pose_cnn_filter_epoch();
    now += 1000000U;
    result = observation(1U, 10.0f);
    assert(input_pose_cnn_publish_stereo(&result));
    assert(input_pose_cnn_filter_epoch() == epoch + 1U);
    take_at(10.0f, 0.1f);
    result = observation(2U, 20.0f);
    assert(input_pose_cnn_publish_stereo(&result));
    now += 250001U;
    assert(!input_pose_cnn_take_stereo(&image, &measured, &dt));
    assert(!input_pose_ready());
    assert(strcmp(input_pose_cnn_admission_reason(), "STALE") == 0);
    input_pose_cnn_set_async_test(0);
    assert(strcmp(input_pose_cnn_tracking_state(), "OFF") == 0);
}

static void test_invalid_duplicate_and_independent_sessions(void)
{
    StereoDepthResult result;
    reset_input();
    start_tracking();
    result = observation(2U, 300.0f);
    assert(!input_pose_cnn_publish_stereo(&result));
    result.async_measured_pose.wrist.x = NAN;
    assert(!input_pose_cnn_publish_stereo(&result));
    assert(strcmp(input_pose_cnn_admission_reason(), "INVALID") == 0);
    assert(input_pose_cnn_reacquire_count() == 0U);
    result = observation(3U, 300.0f);
    assert(!input_pose_cnn_publish_stereo(&result));
    assert(input_pose_cnn_reacquire_count() == 1U);
    result = observation(1U, 300.0f);
    result.left_session_id++;
    result.right_sequence = 3U;
    assert(!input_pose_cnn_publish_stereo(&result));
    assert(strcmp(input_pose_cnn_admission_reason(), "SEQUENCE") == 0);
    assert(input_pose_cnn_reacquire_count() == 1U);
    result = observation(4U, 300.0f);
    result.left_session_id++;
    result.left_sequence = 1U;
    assert(!input_pose_cnn_publish_stereo(&result));
    assert(input_pose_cnn_reacquire_count() == 1U);
    result = observation(5U, 300.0f);
    result.right_session_id++;
    result.left_session_id++;
    result.left_sequence = 1U;
    result.right_sequence = 1U;
    assert(!input_pose_cnn_publish_stereo(&result));
    assert(strcmp(input_pose_cnn_admission_reason(), "SEQUENCE") == 0);
    assert(input_pose_cnn_reacquire_count() == 1U);
}

static void test_inclusive_boundaries_and_pending_left_age(void)
{
    StereoDepthResult result;
    HumanPose2D image;
    HumanPose3D measured;
    float dt;
    reset_input();
    result = observation(1U, 0.0f);
    result.left_received_us = now - 250000U;
    result.right_received_us = now - 150000U;
    assert(input_pose_cnn_publish_stereo(&result));
    assert(input_pose_cnn_take_stereo(&image, &measured, &dt));
    result = observation(2U, 300.0f);
    assert(!input_pose_cnn_publish_stereo(&result));
    result = observation(3U, 350.0f);
    assert(!input_pose_cnn_publish_stereo(&result));
    assert(input_pose_cnn_reacquire_count() == 2U);
    result = observation(4U, 350.001f);
    assert(!input_pose_cnn_publish_stereo(&result));
    assert(input_pose_cnn_reacquire_count() == 1U);
    result = observation(5U, 350.0f);
    result.left_received_us = now - 250001U;
    assert(!input_pose_cnn_publish_stereo(&result));
    assert(input_pose_cnn_reacquire_count() == 0U);
    result = observation(6U, 350.0f);
    assert(!input_pose_cnn_publish_stereo(&result));
    result = observation(7U, 350.0f);
    result.right_received_us = now + 1U;
    assert(!input_pose_cnn_publish_stereo(&result));
    assert(input_pose_cnn_reacquire_count() == 0U);
    result = observation(8U, 350.0f);
    assert(!input_pose_cnn_publish_stereo(&result));
    assert(!input_pose_cnn_publish_stereo(NULL));
    assert(input_pose_cnn_reacquire_count() == 0U);
    reset_input();
    result = observation(1U, 0.0f);
    result.left_received_us = now - 100000U;
    assert(input_pose_cnn_publish_stereo(&result));
    now += 150001U;
    assert(!input_pose_cnn_take_stereo(&image, &measured, &dt));
    assert(strcmp(input_pose_cnn_admission_reason(), "STALE") == 0);
}

int main(void)
{
    test_reacquire_new_location();
    test_spike_and_fixed_anchor();
    test_bad_observations_clear_candidates();
    test_candidate_timeout_and_session();
    test_pending_age_and_filter_epoch();
    test_invalid_duplicate_and_independent_sessions();
    test_inclusive_boundaries_and_pending_left_age();
    puts("test_stereo_reacquisition: PASS (freshness, unique pairs, anchored reacquisition, epochs)");
    return 0;
}
