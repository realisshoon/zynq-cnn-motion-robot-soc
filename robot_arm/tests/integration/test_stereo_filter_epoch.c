#include "integration/input_pose.h"
#include "../../src/integration/input_pose_cnn.h"
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
    result.control_status = STEREO_POSE_UNVERIFIED;
    result.async_status = STEREO_POSE_OK;
    result.async_test = 1U;
    result.image_pose.valid = 1U;
    result.image_pose.frame_id = sequence;
    result.async_measured_pose.valid = 1U;
    result.async_measured_pose.frame_id = sequence;
    result.async_measured_pose.elbow = (Point3D){offset, 0.0f, 1000.0f, 1U};
    result.async_measured_pose.wrist = (Point3D){offset, 200.0f, 1000.0f, 1U};
    return result;
}

static float take_wrist(void)
{
    HumanPose2D image;
    HumanPose3D measured;
    float dt;
    assert(input_pose_cnn_take_stereo(&image, &measured, &dt));
    assert(measured.valid);
    return measured.wrist.x;
}

int main(void)
{
    StereoDepthResult result;
    uint32_t sequence;
    input_pose_cnn_set_stereo(1);
    input_pose_cnn_set_async_test(1);
    for (sequence = 1U; sequence <= 8U; ++sequence) {
        result = observation(sequence, 0.0f);
        assert(input_pose_cnn_publish_stereo(&result));
        assert(fabsf(take_wrist()) < 0.001f);
    }
    for (sequence = 9U; sequence <= 11U; ++sequence) {
        result = observation(sequence, 400.0f);
        if (sequence < 11U) {
            assert(!input_pose_cnn_publish_stereo(&result));
        } else {
            assert(input_pose_cnn_publish_stereo(&result));
            assert(strcmp(input_pose_cnn_admission_reason(), "REACQ_ACCEPT") == 0);
            assert(fabsf(take_wrist() - 400.0f) < 0.001f);
        }
    }
    puts("test_stereo_filter_epoch: PASS (reacquisition resets all filter variant histories)");
    return 0;
}
