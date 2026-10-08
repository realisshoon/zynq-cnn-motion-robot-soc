#include "integration/input_pose.h"
#include "../../src/integration/input_pose_cnn.h"
#include "stereo_vision/stereo_pose_filter.h"
#include "xtime_l.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static XTime now = 1000000U;
void XTime_GetTime(XTime *time) { *time = now; }

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
    result.async_status = STEREO_POSE_OK;
    result.control_status = STEREO_POSE_UNVERIFIED;
    result.async_test = 1U;
    result.image_pose.valid = 1U;
    result.image_pose.frame_id = sequence;
    result.async_measured_pose.valid = 1U;
    result.async_measured_pose.frame_id = sequence;
    result.async_measured_pose.elbow = (Point3D){offset, 0.0f, 1000.0f, 1U};
    result.async_measured_pose.wrist = (Point3D){offset, 200.0f, 1000.0f, 1U};
    return result;
}

static float take(uint32_t sequence, float offset)
{
    StereoDepthResult result = observation(sequence, offset);
    HumanPose2D image;
    HumanPose3D measured;
    float elapsed;
    assert(input_pose_cnn_publish_stereo(&result));
    assert(input_pose_cnn_take_stereo(&image, &measured, &elapsed));
    return measured.wrist.x;
}

int main(void)
{
    StereoPoseFilter state, before;
    StereoOneEuroConfig config = stereo_one_euro_defaults();
    StereoLink link;
    HumanPose3D measured = observation(1U, 0.0f).async_measured_pose;
    float output;
    stereo_pose_filter_init(&state);
    assert(stereo_pose_filter_apply(&state, &measured, 0.1f));
    before = state;
    config.minimum_cutoff_hz = 0.3f;
    config.beta_per_mm = 0.0f;
    assert(stereo_pose_filter_configure(&state, config));
    assert(state.time_us == before.time_us);
    assert(memcmp(state.euro[1].filtered, before.euro[1].filtered,
        sizeof(state.euro[1].filtered)) == 0);
    config.beta_per_mm = NAN;
    before = state;
    assert(!stereo_pose_filter_configure(&state, config));
    assert(memcmp(&before, &state, sizeof(state)) == 0);
    memset(&link, 0, sizeof(link));
    link.filtered[0][10] = (Point2D){100.0f, 200.0f, 1U};
    assert(stereo_link_set_pixel_tau_us(&link, 150000U));
    assert(link.pixel_tau_us == 150000U && link.filtered[0][10].x == 100.0f);
    assert(!stereo_link_set_pixel_tau_us(&link, 0U));
    assert(link.pixel_tau_us == 150000U);
    input_pose_cnn_set_stereo(1);
    input_pose_cnn_set_async_test(1);
    assert(input_pose_cnn_filter_tunable());
    config = input_pose_cnn_filter_config();
    assert(config.minimum_cutoff_hz == 0.5f && config.beta_per_mm == 0.001f);
    assert(take(1U, 0.0f) == 0.0f);
    config.minimum_cutoff_hz = 0.3f;
    config.beta_per_mm = 0.0f;
    assert(input_pose_cnn_filter_configure(config));
    output = take(2U, 50.0f);
    assert(output > 0.0f && output < 50.0f);
    input_pose_cnn_set_async_test(0);
    input_pose_cnn_set_async_test(1);
    assert(take(3U, 50.0f) == 50.0f);
    assert(input_pose_cnn_filter_config().minimum_cutoff_hz == 0.3f);
    output = take(4U, 100.0f);
    assert(output > 50.0f && output < 65.0f);
    now += 600000U;
    {
        uint32_t epoch = input_pose_cnn_filter_epoch();
        StereoDepthResult result = observation(5U, 100.0f);
        HumanPose2D image;
        HumanPose3D measured;
        float elapsed;
        assert(input_pose_cnn_publish_stereo(&result));
        assert(input_pose_cnn_filter_epoch() == epoch + 1U);
        assert(input_pose_cnn_take_stereo(&image, &measured, &elapsed));
        assert(measured.wrist.x == 100.0f && elapsed == 0.1f);
    }
    assert(input_pose_cnn_filter_config().beta_per_mm == 0.0f);
    config.derivative_cutoff_hz = INFINITY;
    assert(!input_pose_cnn_filter_configure(config));
    assert(input_pose_cnn_filter_config().derivative_cutoff_hz == 1.0f);
    puts("test_stereo_filter_runtime: PASS (state-preserving update, validation and epoch persistence)");
    return 0;
}
