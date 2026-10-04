#include "stereo_vision/stereo_link.h"
#include "integration/input_pose.h"
#include "../../src/integration/input_pose_cnn.h"
#include "human_target_angle/forearm_mapping.h"
#include "xtime_l.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static XTime now = 1000000;
void XTime_GetTime(XTime *time) { *time = now; }

static StereoCalibration calibration(void)
{
    StereoCalibration result;
    memset(&result, 0, sizeof(result));
    result.left.fx = result.left.fy = 1000;
    result.left.cx = 640; result.left.cy = 360;
    result.right = result.left;
    result.rotation[0] = result.rotation[4] = result.rotation[8] = 1;
    result.translation_mm[0] = -200;
    result.image_width = 1280; result.image_height = 720;
    return result;
}

static StereoCoordinateFrame frame(unsigned side, unsigned sequence, unsigned offset)
{
    StereoCoordinateFrame result;
    unsigned index;
    memset(&result, 0, sizeof(result));
    result.session_id = side + 1;
    result.sequence = sequence;
    result.frame_id = side * 100 + sequence;
    for (index = 0; index < STEREO_UART_JOINTS; ++index)
        result.joint[index] = (StereoCoordinateJoint){(uint16_t)(640 - side * 200 + offset), 360, 50, 1};
    result.joint[5].x = (uint16_t)(440 - side * 200 + offset);
    result.joint[6].x = (uint16_t)(840 - side * 200 + offset);
    result.joint[8].x = (uint16_t)(840 - side * 200 + offset);
    result.joint[8].y = 560;
    result.joint[10].x = (uint16_t)(1040 - side * 200 + offset);
    result.joint[10].y = 560;
    result.markers[0] = 0x80000000U | (560U << 11) | (1060U - side * 200 + offset);
    result.markers[2] = 0x80000000U | (560U << 11) | (1080U - side * 200 + offset);
    return result;
}

static StereoDepthResult pair(StereoLink *link, unsigned sequence, unsigned offset,
                              uint64_t left_time, uint64_t right_time)
{
    StereoCoordinateFrame left = frame(0, sequence, offset), right = frame(1, sequence, offset);
    StereoDepthResult result;
    assert(stereo_link_push(link, 0, &left, left_time));
    assert(stereo_link_push(link, 1, &right, right_time));
    assert(stereo_link_take(link, &result));
    return result;
}

static void test_async_latest(const StereoCalibration *parameters)
{
    StereoLink link;
    StereoDepthResult result;
    unsigned side, sequence;
    assert(stereo_link_init(&link, parameters));
    stereo_link_set_async_test(&link, 1);
    for (side = 0; side < 2; ++side)
        for (sequence = 1; sequence <= 3; ++sequence) {
            StereoCoordinateFrame queued = frame(side, sequence, 0);
            uint64_t received = 1000000U + sequence * 20000U + side * 1000U;
            assert(stereo_link_push(&link, side, &queued, received));
        }
    assert(link.count[0] == 3 && link.count[1] == 3 && !link.ready);
    assert(!link.expired_frames && !link.queue_overflows);
    sequence = 3;
    {
        assert(stereo_link_take(&link, &result));
        assert(result.left_sequence == sequence && result.right_sequence == sequence);
        assert(result.left_frame_id == sequence && result.right_frame_id == 100 + sequence);
        assert(result.left_received_us == 1000000U + sequence * 20000U);
        assert(result.right_received_us == 1001000U + sequence * 20000U);
        assert(result.async_test && result.async_status == STEREO_POSE_OK);
        assert(!result.time_verified && result.control_status == STEREO_POSE_UNVERIFIED);
        assert(!result.left_metadata.exposure_time_verified && !result.right_metadata.exposure_time_verified);
        assert(link.count[0] == 3 - sequence && link.count[1] == 3 - sequence);
    }
    assert(!stereo_link_take(&link, &result));
    assert(link.pairs == 1 && !link.overwritten_results && !link.expired_frames);

    assert(STEREO_LINK_QUEUE == 8);
    assert(stereo_link_init(&link, parameters));
    stereo_link_set_async_test(&link, 1);
    for (side = 0; side < 2; ++side)
        for (sequence = 1; sequence <= STEREO_LINK_QUEUE + 2; ++sequence) {
            StereoCoordinateFrame queued = frame(side, sequence, 0);
            assert(stereo_link_push(&link, side, &queued, 6000000U + sequence * 20000U));
            assert(link.count[side] <= STEREO_LINK_QUEUE);
        }
    assert(link.count[0] == 8 && link.count[1] == 8);
    assert(link.queue_overflows == 4 && !link.expired_frames && !link.pairs);
    sequence = STEREO_LINK_QUEUE + 2;
    {
        assert(stereo_link_take(&link, &result));
        assert(result.left_sequence == sequence && result.right_sequence == sequence);
        assert(result.async_test && result.async_status == STEREO_POSE_OK);
    }
    assert(!stereo_link_take(&link, &result));
    assert(!link.count[0] && !link.count[1] && link.pairs == 1 && !link.overwritten_results);
}

static void test_strict_timing(const StereoCalibration *parameters)
{
    StereoLink link;
    StereoDepthResult result;
    StereoCoordinateFrame left = frame(0, 1, 0), right = frame(1, 1, 0);
    assert(stereo_link_init(&link, parameters));
    result = pair(&link, 1, 0, 1000000U, 1000000U + STEREO_LINK_MAX_AGE_US);
    assert(!result.async_test && !link.expired_frames);
    assert(stereo_link_init(&link, parameters));
    assert(stereo_link_push(&link, 0, &left, 1000000U));
    assert(stereo_link_push(&link, 1, &right, 1000000U + STEREO_LINK_MAX_AGE_US + 1));
    assert(link.expired_frames == 1 && !link.count[0] && link.count[1] == 1);
    assert(!stereo_link_take(&link, &result));

    assert(stereo_link_init(&link, parameters));
    stereo_link_set_async_test(&link, 1);
    left.metadata = (StereoFrameMetadata){1000000U, 7, 1, 1, 1};
    right.metadata = (StereoFrameMetadata){1000000U, 8, 1, 1, 1};
    assert(stereo_link_push(&link, 0, &left, 1000000U));
    assert(stereo_link_push(&link, 1, &right, 1000000U));
    assert(!stereo_link_take(&link, &result));
    assert(link.count[0] == 1 && link.count[1] == 1 && !link.unsynchronized_pairs);
    left.sequence++;
    left.metadata.shared_clock_epoch = 8;
    left.metadata.exposure_time_us++;
    assert(!stereo_link_push(&link, 0, &left, 1000001U));
    assert(link.rejected_frames == 1);

    assert(stereo_link_init(&link, parameters));
    stereo_link_set_async_test(&link, 1);
    left = frame(0, 1, 0);
    left.metadata = (StereoFrameMetadata){1000000U, 7, 1, 1, 1};
    right.metadata = (StereoFrameMetadata){1000000U + STEREO_LINK_MAX_SKEW_US + 1, 7, 1, 1, 1};
    assert(stereo_link_push(&link, 0, &left, 1000000U));
    assert(stereo_link_push(&link, 1, &right, 1000000U));
    assert(!stereo_link_take(&link, &result));
    assert(!link.unsynchronized_pairs);
    left.sequence++;
    left.metadata.exposure_time_us++;
    assert(stereo_link_push(&link, 0, &left, 1000001U));
    assert(stereo_link_take(&link, &result));
    assert(result.time_verified && !result.async_test && result.control_status == STEREO_POSE_OK);
    assert(result.diagnostics.exposure_skew_us == STEREO_LINK_MAX_SKEW_US);
    assert(result.left_metadata.exposure_time_us == left.metadata.exposure_time_us);
    assert(result.right_metadata.exposure_time_us == right.metadata.exposure_time_us);

    assert(stereo_link_init(&link, parameters));
    stereo_link_set_async_test(&link, 1);
    assert(stereo_link_push(&link, 0, &left, 1000000U));
    assert(stereo_link_push(&link, 1, &right, 1000000U + STEREO_LINK_MAX_AGE_US + 1));
    assert(link.expired_frames == 1 && !link.count[0]);
    assert(!stereo_link_take(&link, &result));
}

static void test_gripper_view_fallback(const StereoCalibration *parameters)
{
    StereoLink link;
    StereoDepthResult result;
    HumanPose2D image;
    HumanPose3D measured;
    ForearmMappingContext mapping;
    HumanForearmTarget target;
    float dt;
    unsigned sequence;
    assert(stereo_link_init(&link, parameters));
    stereo_link_set_async_test(&link, 1);
    input_pose_cnn_set_stereo(1);
    input_pose_cnn_set_async_test(1);
    assert(forearm_mapping_init(&mapping) == 0);
    for (sequence = 1; sequence <= 8; ++sequence) {
        StereoCoordinateFrame left = frame(0, sequence, 0);
        StereoCoordinateFrame right = frame(1, sequence, 0);
        /* Coincident markers close the grip even though the 10 mm 3D hand
         * quality check necessarily invalidates their wrist-angle geometry. */
        left.markers[0] = left.markers[2] = 0x80000000U | (560U << 11) | 1080U;
        right.markers[0] = right.markers[2] = 0x80000000U | (560U << 11) | 880U;
        if (sequence == 1) left.markers[2] = 0U; /* right camera fallback */
        if (sequence == 3) right.markers[2] = 0U; /* return to complete left */
        if (sequence == 4) {
            left.markers[2] = 0U;
            right.markers[0] = 0U; /* cannot combine red/green across views */
        }
        if (sequence == 6) left.markers[2] = 0x80000000U | (560U << 11) | 1180U;
        if (sequence == 8) left.markers[2] = right.markers[2] = 0U;
        now += 100000U;
        assert(stereo_link_push(&link, 0, &left, now - 1000U));
        assert(stereo_link_push(&link, 1, &right, now));
        assert(stereo_link_take(&link, &result));
        assert(result.async_status == STEREO_POSE_OK);
        assert(result.image_pose.gripper_2d.source == (sequence <= 2 ? 2U : 1U));
        assert(result.image_pose.wrist.x == 1040.0f); /* major input remains left */
        if (sequence <= 2) assert(result.image_pose.gripper_2d.wrist.x == 840.0f);
        assert(input_pose_cnn_publish_stereo(&result));
        assert(input_pose_cnn_take_stereo(&image, &measured, &dt));
        assert(forearm_mapping_update_stereo(&mapping, &image, &measured,
                                             POSE_ARM_RIGHT, dt, &target) == 1);
        assert(target.gripper_valid && !target.wrist_valid);
        assert(target.gripper_norm == 0.0f); /* includes one-frame opening spike */
        assert(mapping.pose.gripper_last_hold == (sequence == 4 || sequence == 8));
    }
}

static void test_end_to_end_reacquisition(const StereoCalibration *parameters)
{
    StereoLink link;
    StereoDepthResult result;
    HumanPose2D image;
    HumanPose3D measured;
    float dt;
    unsigned sequence;
    assert(stereo_link_init(&link, parameters));
    stereo_link_set_async_test(&link, 1);
    input_pose_cnn_set_stereo(1);
    input_pose_cnn_set_async_test(1);
    now += 100000U;
    result = pair(&link, 1U, 0U, now - 1000U, now);
    assert(input_pose_cnn_publish_stereo(&result));
    assert(input_pose_cnn_take_stereo(&image, &measured, &dt));
    for (sequence = 2U; sequence <= 6U; ++sequence) {
        int accepted;
        now += 100000U;
        result = pair(&link, sequence, 170U, now - 1000U, now);
        accepted = input_pose_cnn_publish_stereo(&result);
        if (sequence < 6U) {
            assert(!accepted && !input_pose_ready());
        } else {
            assert(accepted && strcmp(input_pose_cnn_admission_reason(), "REACQ_ACCEPT") == 0);
            assert(input_pose_cnn_take_stereo(&image, &measured, &dt));
            assert(fabsf(measured.wrist.x - 570.0f) < 0.1f);
        }
    }
}

int main(void)
{
    StereoCalibration parameters = calibration();
    StereoLink link;
    StereoDepthResult result, altered;
    HumanPose2D image;
    HumanPose3D measured;
    ForearmMappingContext mapping;
    HumanForearmTarget target;
    float dt;
    test_async_latest(&parameters);
    test_strict_timing(&parameters);
    test_gripper_view_fallback(&parameters);
    test_end_to_end_reacquisition(&parameters);
    assert(stereo_link_init(&link, &parameters));
    input_pose_cnn_set_stereo(1);
    result = pair(&link, 1, 0, now - 1000, now);
    assert(!result.async_test && result.control_status == STEREO_POSE_UNVERIFIED);
    assert(!input_pose_cnn_publish_stereo(&result));
    input_pose_cnn_set_async_test(1);
    assert(!input_pose_cnn_publish_stereo(&result));
    stereo_link_set_async_test(&link, 1);
    for (unsigned sequence = 2U; sequence <= 4U; ++sequence) {
        now += 20000U;
        result = pair(&link, sequence, 0, now - 1000U, now);
        if (sequence < 4U) assert(result.async_status == STEREO_POSE_MAJOR_INVALID);
    }
    assert(result.async_test && result.async_status == STEREO_POSE_OK);
    assert(!result.time_verified && !result.measured_pose.valid);
    assert(result.control_status == STEREO_POSE_UNVERIFIED);
    assert(!result.left_metadata.exposure_time_verified && !result.left_metadata.shared_clock_epoch);
    assert(input_pose_cnn_publish_stereo(&result));
    assert(!input_pose_cnn_publish_stereo(&result));
    assert(input_pose_cnn_take_stereo(&image, &measured, &dt));
    assert(fabsf(measured.wrist.z - 1000.0f) < 0.001f && fabsf(dt - 0.1f) < 1e-6f);
    forearm_mapping_init(&mapping);
    assert(forearm_mapping_update_stereo(&mapping, &image, &measured, POSE_ARM_RIGHT, dt, &target) == 1);
    assert(target.valid && mapping.stereo_input_active);
    now += 20000;
    result = pair(&link, 5, 10, now - 1000, now);
    assert(input_pose_cnn_publish_stereo(&result));
    assert(input_pose_cnn_take_stereo(&image, &measured, &dt));
    assert(fabsf(dt - 0.02f) < 1e-6f);
    altered = result;
    altered.left_sequence++;
    altered.right_sequence++;
    altered.right_received_us++;
    assert(!input_pose_cnn_publish_stereo(&altered));
    now += 20000;
    result = pair(&link, 6, 10, now - 1000, now);
    assert(result.async_status == STEREO_POSE_OK);
    altered = result;
    altered.left_session_id++;
    assert(!input_pose_cnn_publish_stereo(&altered));
    assert(strcmp(input_pose_cnn_admission_reason(), "JUMP_HOLD") == 0);
    input_pose_cnn_set_async_test(1);
    assert(input_pose_cnn_publish_stereo(&result));
    assert(strcmp(input_pose_cnn_admission_reason(), "OK") == 0);
    input_pose_cnn_set_async_test(0);
    assert(!input_pose_ready() && !input_pose_cnn_publish_stereo(&result));
    assert(strcmp(input_pose_cnn_admission_reason(), "OFF") == 0);
    input_pose_cnn_set_async_test(1);
    now += 1000001U;
    assert(!input_pose_cnn_publish_stereo(&result));
    assert(strcmp(input_pose_cnn_admission_reason(), "STALE") == 0);
    stereo_link_set_async_test(&link, 1);
    input_pose_cnn_set_async_test(1);
    for (unsigned sequence = 7U; sequence <= 9U; ++sequence) {
        now += 20000U;
        result = pair(&link, sequence, 0, now - 1000U, now);
    }
    assert(result.async_test && result.async_status == STEREO_POSE_OK);
    assert(input_pose_cnn_publish_stereo(&result));
    assert(input_pose_cnn_take_stereo(&image, &measured, &dt));
    now += 20000;
    result = pair(&link, 10, 0, now - 1000, now);
    altered = result;
    altered.right_received_us = now + 1;
    assert(!input_pose_cnn_publish_stereo(&altered));
    altered = result;
    altered.async_measured_pose.wrist.z = NAN;
    assert(!input_pose_cnn_publish_stereo(&altered));
    altered = result;
    altered.control_status = STEREO_POSE_OK;
    assert(!input_pose_cnn_publish_stereo(&altered));
    {
        StereoCoordinateFrame left = frame(0, 11, 0), right = frame(1, 11, 0);
        left.joint[10].valid = 0;
        now += 20000;
        assert(stereo_link_push(&link, 0, &left, now));
        assert(stereo_link_push(&link, 1, &right, now));
        assert(stereo_link_take(&link, &result));
        assert(result.async_status == STEREO_POSE_MAJOR_INVALID);
        assert(!input_pose_cnn_publish_stereo(&result));
        left = frame(0, 12, 0); right = frame(1, 12, 0);
        left.markers[2] = 0;
        now += 20000;
        assert(stereo_link_push(&link, 0, &left, now));
        assert(stereo_link_push(&link, 1, &right, now));
        assert(stereo_link_take(&link, &result));
        assert(result.async_status == STEREO_POSE_OK && !result.async_diagnostics.hand_valid);
        assert(!result.async_measured_pose.finger1.valid && !result.async_measured_pose.finger2.valid);
        assert(input_pose_cnn_publish_stereo(&result));
        left = frame(0, 13, 0); right = frame(1, 13, 0);
        left.metadata = right.metadata = (StereoFrameMetadata){now + 20000, 7, 1, 1, 0};
        now += 20000;
        assert(stereo_link_push(&link, 0, &left, now));
        assert(stereo_link_push(&link, 1, &right, now));
        assert(stereo_link_take(&link, &result));
        assert(result.time_verified && !result.async_test && result.control_status == STEREO_POSE_UNVERIFIED);
        assert(!input_pose_cnn_publish_stereo(&result));
        for (unsigned sequence = 14U; sequence <= 16U; ++sequence) {
            now += 20000U;
            result = pair(&link, sequence, 0, now - 1000U, now);
        }
        left = frame(0, 17, 0); right = frame(1, 17, 0);
        left.joint[5].valid = left.joint[6].valid = 0;
        right.joint[5].valid = right.joint[6].valid = 0;
        now += 20000;
        assert(stereo_link_push(&link, 0, &left, now));
        assert(stereo_link_push(&link, 1, &right, now));
        assert(stereo_link_take(&link, &result));
        assert(result.async_status == STEREO_POSE_OK && result.image_pose.valid);
        assert(input_pose_cnn_publish_stereo(&result));
        assert(input_pose_cnn_take_stereo(&image, &measured, &dt));
        assert(!measured.shoulder_l.valid && !measured.shoulder_r.valid);
        forearm_mapping_init(&mapping);
        assert(forearm_mapping_update_stereo(&mapping, &image, &measured, POSE_ARM_RIGHT, dt, &target) == 1);
        assert(fabsf(mapping.pose.wrist_3d.z - 1000.0f) < 0.001f);
        left = frame(0, 18, 0); right = frame(1, 18, 0);
        right.joint[10].x += 20;
        now += 20000;
        assert(stereo_link_push(&link, 0, &left, now));
        assert(stereo_link_push(&link, 1, &right, now));
        assert(stereo_link_take(&link, &result));
        assert(input_pose_cnn_publish_stereo(&result));
        assert(input_pose_cnn_take_stereo(&image, &measured, &dt));
        assert(measured.wrist.z > 1000.0f && measured.wrist.z < 1120.0f);
    }
    stereo_link_set_async_test(&link, 0);
    input_pose_cnn_set_async_test(0);
    assert(!link.ready && !link.count[0] && !link.count[1] && !input_pose_ready());
    puts("test_stereo_async: PASS (latest pairs, strict timing, independent grip, 2D/3D reacquisition)");
    return 0;
}
