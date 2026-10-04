#include "integration/input_pose.h"
#include "../../src/integration/input_pose_cnn.h"
#include "stereo_vision/stereo_pose_filter.h"
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
    result.left_session_id = 1U;
    result.right_session_id = 2U;
    result.left_sequence = result.right_sequence = sequence;
    result.left_received_us = now - 1000U;
    result.right_received_us = now;
    result.control_status = STEREO_POSE_UNVERIFIED;
    result.async_status = STEREO_POSE_OK;
    result.async_test = 1U;
    result.image_pose.valid = 1U;
    result.image_pose.frame_id = sequence;
    result.image_pose.elbow = (Point2D){500.0f, 300.0f, 1U};
    result.image_pose.wrist = (Point2D){500.0f, 500.0f, 1U};
    result.image_pose.finger1 = (Point2D){490.0f, 550.0f, 1U};
    result.image_pose.finger2 = (Point2D){510.0f, 550.0f, 1U};
    result.async_measured_pose.valid = 1U;
    result.async_measured_pose.frame_id = sequence;
    result.async_measured_pose.elbow = (Point3D){offset, 0.0f, 1000.0f, 1U};
    result.async_measured_pose.wrist = (Point3D){offset, 200.0f, 1000.0f, 1U};
    result.async_measured_pose.finger1 = (Point3D){offset - 20.0f, 250.0f, 1000.0f, 1U};
    result.async_measured_pose.finger2 = (Point3D){offset + 20.0f, 250.0f, 1000.0f, 1U};
    return result;
}

static HumanPose3D take(HumanPose2D *image)
{
    HumanPose3D measured;
    float dt;
    assert(input_pose_cnn_take_stereo(image, &measured, &dt));
    assert(measured.valid);
    assert(!input_pose_ready());
    assert(fabsf(dt - 0.1f) < 0.00001f);
    return measured;
}

static void test_default_filter_and_raw_admission(void)
{
    StereoPoseFilter expected_filter;
    HumanPose2D image;
    HumanPose3D expected, measured;
    StereoDepthResult result;
    unsigned sequence;
    reset_input();
    stereo_pose_filter_init(&expected_filter);
    for (sequence = 1U; sequence <= 3U; ++sequence) {
        float offset = (float)(sequence - 1U) * 140.0f;
        result = observation(sequence, offset);
        expected = result.async_measured_pose;
        assert(stereo_pose_filter_apply(&expected_filter, &expected, 0.1f));
        assert(input_pose_cnn_publish_stereo(&result));
        assert(strcmp(input_pose_cnn_admission_reason(), "OK") == 0);
        measured = take(&image);
        assert(fabsf(measured.wrist.x - expected.wrist.x) < 0.001f);
        assert(fabsf(result.async_measured_pose.wrist.x - offset) < 0.001f);
        assert(image.wrist.x == result.image_pose.wrist.x);
        if (sequence > 1U) {
            assert(measured.wrist.x > 0.0f && measured.wrist.x < offset);
            assert(offset - measured.wrist.x > 10.0f);
        }
    }
}

static void test_finger_loss_and_recovery(void)
{
    StereoDepthResult result;
    HumanPose2D image;
    HumanPose3D measured;
    reset_input();
    result = observation(1U, 0.0f);
    assert(input_pose_cnn_publish_stereo(&result));
    take(&image);
    result = observation(2U, 20.0f);
    result.async_measured_pose.finger1.valid = 0U;
    assert(input_pose_cnn_publish_stereo(&result));
    measured = take(&image);
    assert(!measured.finger1.valid && !measured.finger2.valid);
    assert(image.finger1.valid && image.finger2.valid && image.wrist.valid);
    result = observation(3U, 40.0f);
    assert(input_pose_cnn_publish_stereo(&result));
    measured = take(&image);
    assert(measured.finger1.valid && measured.finger2.valid);
    assert(fabsf(measured.finger1.x - result.async_measured_pose.finger1.x) < 0.001f);
}

static void test_reacquisition_and_mode_reset(void)
{
    StereoDepthResult result;
    HumanPose2D image;
    HumanPose3D measured;
    unsigned sequence;
    reset_input();
    result = observation(1U, 0.0f);
    assert(input_pose_cnn_publish_stereo(&result));
    take(&image);
    result = observation(2U, 100.0f);
    assert(input_pose_cnn_publish_stereo(&result));
    measured = take(&image);
    assert(measured.wrist.x < 100.0f);
    for (sequence = 3U; sequence <= 5U; ++sequence) {
        result = observation(sequence, 400.0f);
        if (sequence < 5U) {
            assert(!input_pose_cnn_publish_stereo(&result));
        } else {
            assert(input_pose_cnn_publish_stereo(&result));
            assert(strcmp(input_pose_cnn_admission_reason(), "REACQ_ACCEPT") == 0);
            measured = take(&image);
            assert(fabsf(measured.wrist.x - 400.0f) < 0.001f);
        }
    }
    input_pose_cnn_set_async_test(0);
    input_pose_cnn_set_async_test(1);
    result = observation(6U, -200.0f);
    assert(input_pose_cnn_publish_stereo(&result));
    measured = take(&image);
    assert(fabsf(measured.wrist.x + 200.0f) < 0.001f);
}

static void test_strict_path_filtered(void)
{
    StereoDepthResult result;
    HumanPose2D image;
    HumanPose3D measured;
    unsigned sequence;
    reset_input();
    input_pose_cnn_set_async_test(0);
    for (sequence = 1U; sequence <= 2U; ++sequence) {
        result = observation(sequence, (float)(sequence - 1U) * 20.0f);
        result.time_verified = 1U;
        result.control_status = STEREO_POSE_OK;
        result.measured_pose = result.async_measured_pose;
        result.left_metadata = (StereoFrameMetadata){0};
        result.left_metadata.exposure_time_verified = 1U;
        result.left_metadata.fixed_geometry_verified = 1U;
        result.left_metadata.localization_quality_verified = 1U;
        result.left_metadata.shared_clock_epoch = 7U;
        result.left_metadata.exposure_time_us = now;
        result.right_metadata = result.left_metadata;
        assert(input_pose_cnn_publish_stereo(&result));
        measured = take(&image);
        if (sequence == 2U) assert(measured.wrist.x > 0.0f && measured.wrist.x < 20.0f);
    }
}

int main(void)
{
    test_default_filter_and_raw_admission();
    test_finger_loss_and_recovery();
    test_reacquisition_and_mode_reset();
    test_strict_path_filtered();
    puts("test_stereo_filter_integration: PASS (production One Euro, raw admission, independent gripper, resets)");
    return 0;
}
