#include "stereo_vision/stereo_link.h"
#include "integration/input_pose.h"
#include "../../src/integration/input_pose_cnn.h"
#include "human_target_angle/forearm_mapping.h"
#include "xtime_l.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

void XTime_GetTime(XTime *time) { *time = 1000000; }

static StereoCalibration rail_calibration(void)
{
    StereoCalibration calibration;
    memset(&calibration, 0, sizeof(calibration));
    calibration.left.fx = calibration.left.fy = 1000;
    calibration.left.cx = 640;
    calibration.left.cy = 360;
    calibration.right = calibration.left;
    calibration.rotation[0] = calibration.rotation[4] = calibration.rotation[8] = 1;
    calibration.translation_mm[0] = -200;
    calibration.image_width = 1280;
    calibration.image_height = 720;
    return calibration;
}

static StereoCoordinateFrame make_frame(unsigned side, uint32_t sequence, uint64_t time)
{
    StereoCoordinateFrame frame;
    unsigned index;
    memset(&frame, 0, sizeof(frame));
    frame.session_id = side + 1;
    frame.sequence = sequence;
    frame.frame_id = side * 100 + sequence;
    frame.metadata = (StereoFrameMetadata){time, 7, 1, 1, 1};
    for (index = 0; index < STEREO_UART_JOINTS; ++index)
        frame.joint[index] = (StereoCoordinateJoint){(uint16_t)(640 - side * 200), 360, 50, 1};
    frame.joint[5].x = (uint16_t)(440 - side * 200);
    frame.joint[6].x = (uint16_t)(840 - side * 200);
    frame.joint[8].x = (uint16_t)(840 - side * 200);
    frame.joint[8].y = 560;
    frame.joint[10].x = (uint16_t)(1040 - side * 200);
    frame.joint[10].y = 560;
    for (index = 0; index < STEREO_UART_MARKERS; ++index)
        frame.markers[index] = 0x80000000U | (560U << 11) | (1060U + index * 10 - side * 200);
    return frame;
}

static void test_pixel_filter(void)
{
    StereoLink link;
    StereoCalibration calibration = rail_calibration();
    StereoCoordinateFrame left = make_frame(0, 1, 1000000);
    StereoCoordinateFrame right = make_frame(1, 1, 1000000);
    StereoDepthResult result;
    float expected = 560.0f + 12.0f * (1.0f - expf(-0.5f));
    assert(stereo_link_init(&link, &calibration));
    stereo_link_set_async_test(&link, 1);
    left.metadata.exposure_time_verified = right.metadata.exposure_time_verified = 0;
    assert(stereo_link_push(&link, 0, &left, 1000000));
    assert(stereo_link_push(&link, 1, &right, 1000000));
    assert(stereo_link_take(&link, &result));
    left.sequence = right.sequence = 2;
    left.joint[10].y = 572;
    assert(stereo_link_push(&link, 0, &left, 1050000));
    assert(link.queue[0][0].frame.joint[10].y == 572);
    assert(fabsf(link.queue[0][0].filtered[10].y - expected) < 0.001f);
    assert(!stereo_link_push(&link, 0, &left, 1060000));
    assert(stereo_link_push(&link, 1, &right, 1050000));
    assert(stereo_link_take(&link, &result));
    assert(fabsf(result.image_pose.wrist.y - expected) < 0.001f);
    assert(result.point_status[10] == STEREO_OK && result.async_status == STEREO_POSE_OK);
    assert(result.point[10].left_reprojection_error_px < 3.0);
    left.sequence = 3;
    left.joint[10].valid = 0;
    assert(stereo_link_push(&link, 0, &left, 1100000));
    assert(!link.queue[0][0].filtered[10].valid);
    left.sequence = 4;
    left.joint[10].valid = 1;
    left.joint[10].y = 578;
    assert(stereo_link_push(&link, 0, &left, 1150000));
    expected += (578.0f - expected) * (1.0f - expf(-1.0f));
    assert(fabsf(link.queue[0][1].filtered[10].y - expected) < 0.001f);
    left.sequence = 5;
    left.joint[10].y = 650;
    assert(stereo_link_push(&link, 0, &left, 1200000));
    assert(!link.queue[0][2].filtered[10].valid && link.jump_rejects == 1);
    assert(link.accepted_raw[0][10].y == 578);
    assert(fabsf(link.filtered[0][10].y - expected) < 0.001f);
    left.sequence = 6;
    left.joint[10].y = 640;
    assert(stereo_link_push(&link, 0, &left, 2000000));
    assert(link.count[0] == 1 && !link.queue[0][0].filtered[10].valid);
    assert(link.candidate[0][1].count == 1);
    left.sequence = 7;
    left.joint[10].y = 645;
    assert(stereo_link_push(&link, 0, &left, 2050000));
    assert(!link.queue[0][1].filtered[10].valid);
    left.sequence = 8;
    left.joint[10].y = 650;
    assert(stereo_link_push(&link, 0, &left, 2100000));
    assert(link.queue[0][2].filtered[10].valid && link.queue[0][2].filtered[10].y == 650);
    left.session_id = 98;
    left.metadata.exposure_time_verified = 1;
    left.metadata.shared_clock_epoch = 0;
    assert(!stereo_link_push(&link, 0, &left, 2120000));
    assert(link.count[0] == 3 && link.accepted_raw[0][10].y == 650);
    assert(link.filtered[0][10].y == 650);
    left.metadata.exposure_time_verified = 0;
    left.session_id = 99;
    left.sequence = 1;
    left.joint[10].y = 630;
    assert(stereo_link_push(&link, 0, &left, 2150000));
    assert(link.count[0] == 1 && link.count[1] == 0);
    assert(!link.queue[0][0].filtered[10].valid);
    left.sequence = 2;
    assert(stereo_link_push(&link, 0, &left, 2200000));
    left.sequence = 3;
    assert(stereo_link_push(&link, 0, &left, 2250000));
    assert(link.queue[0][2].filtered[10].valid && link.queue[0][2].filtered[10].y == 630);
    stereo_link_set_async_test(&link, 1);
    left.sequence = 4;
    left.joint[10].y = 620;
    assert(stereo_link_push(&link, 0, &left, 2300000));
    assert(!link.queue[0][0].filtered[10].valid);
    left.sequence = 5;
    assert(stereo_link_push(&link, 0, &left, 2350000));
    left.sequence = 6;
    assert(stereo_link_push(&link, 0, &left, 2400000));
    assert(link.queue[0][2].filtered[10].valid && link.queue[0][2].filtered[10].y == 620);
}

static StereoCoordinateFrame async_frame(unsigned side, uint32_t sequence)
{
    StereoCoordinateFrame frame = make_frame(side, sequence, 0);
    memset(&frame.metadata, 0, sizeof(frame.metadata));
    return frame;
}

static void test_receive_matching(void)
{
    StereoLink link;
    StereoCalibration calibration = rail_calibration();
    StereoDepthResult result;
    StereoCoordinateFrame frame;
    unsigned side, sequence;
    assert(stereo_link_init(&link, &calibration));
    stereo_link_set_async_test(&link, 1);
    for (side = 0; side < 2; ++side)
        for (sequence = 1; sequence <= 3; ++sequence) {
            frame = async_frame(side, sequence);
            assert(stereo_link_push(&link, side, &frame, 1000000 + sequence * 20000));
        }
    assert(stereo_link_take_at(&link, &result, 1070000));
    assert(result.left_sequence == 3 && result.right_sequence == 3);
    assert(result.left_frame_id != result.right_frame_id);
    assert(link.skipped_frames == 4 && !link.count[0] && !link.count[1]);
    assert(result.async_test && !result.time_verified && result.async_status == STEREO_POSE_OK);
    assert(!stereo_link_take_at(&link, &result, 1070000));

    assert(stereo_link_init(&link, &calibration));
    stereo_link_set_async_test(&link, 1);
    frame = async_frame(0, 1);
    assert(stereo_link_push(&link, 0, &frame, 1000000));
    frame = async_frame(0, 2);
    assert(stereo_link_push(&link, 0, &frame, 1060000));
    frame = async_frame(0, 3);
    assert(stereo_link_push(&link, 0, &frame, 1080000));
    frame = async_frame(1, 1);
    assert(stereo_link_push(&link, 1, &frame, 1070000));
    assert(stereo_link_take_at(&link, &result, 1080000));
    assert(result.left_sequence == 3 && result.right_sequence == 1);
    assert(link.skipped_frames == 2 && !link.count[0] && !link.count[1]);

    assert(stereo_link_init(&link, &calibration));
    stereo_link_set_async_test(&link, 1);
    frame = async_frame(0, 1);
    assert(stereo_link_push(&link, 0, &frame, 1000000));
    frame = async_frame(1, 1);
    assert(stereo_link_push(&link, 1, &frame, 1050000));
    frame = async_frame(1, 2);
    assert(stereo_link_push(&link, 1, &frame, 1200000));
    assert(stereo_link_take_at(&link, &result, 1200000));
    assert(result.right_sequence == 1 && link.receive_gap_rejects > 0);
    assert(link.count[1] == 1 && link.queue[1][0].frame.sequence == 2);
    assert(!stereo_link_take_at(&link, &result, 1200000));

    assert(stereo_link_init(&link, &calibration));
    stereo_link_set_async_test(&link, 1);
    frame = async_frame(0, 1);
    assert(stereo_link_push(&link, 0, &frame, 1000000));
    frame = async_frame(1, 1);
    assert(stereo_link_push(&link, 1, &frame, 1100000));
    assert(stereo_link_take_at(&link, &result, 1250000));
    assert(!link.stale_frames && result.left_received_us == 1000000);
    assert(stereo_link_init(&link, &calibration));
    stereo_link_set_async_test(&link, 1);
    frame = async_frame(0, 1);
    assert(stereo_link_push(&link, 0, &frame, 1000000));
    frame = async_frame(1, 1);
    assert(stereo_link_push(&link, 1, &frame, 1100001));
    assert(!stereo_link_take_at(&link, &result, 1100001));
    assert(link.receive_gap_rejects == 1);
    assert(!stereo_link_take_at(&link, &result, 1250001));
    assert(link.stale_frames == 1 && !link.count[0]);
    assert(!stereo_link_take_at(&link, &result, 1350002));
    assert(link.stale_frames == 2 && !link.count[1]);
    frame = async_frame(0, 2);
    assert(!stereo_link_push(&link, 0, &frame, 1000000));

    assert(stereo_link_init(&link, &calibration));
    frame = make_frame(0, 1, 1000000);
    assert(stereo_link_push(&link, 0, &frame, 1000000));
    frame = make_frame(1, 1, 1000000);
    assert(stereo_link_push(&link, 1, &frame, 1000000));
    assert(link.ready && !stereo_link_take_at(&link, &result, 1250001));
    assert(!link.ready && link.stale_frames == 1);
}

static void test_jump_cluster(void)
{
    StereoLink link;
    StereoCalibration calibration = rail_calibration();
    StereoCoordinateFrame frame = async_frame(0, 1);
    StereoDepthResult result;
    unsigned sequence;
    assert(stereo_link_init(&link, &calibration));
    stereo_link_set_async_test(&link, 1);
    assert(stereo_link_push(&link, 0, &frame, 1000000));
    for (sequence = 2; sequence <= 4; ++sequence) {
        frame.sequence = sequence;
        frame.joint[8].y = frame.joint[10].y = (uint16_t)(600 + (sequence - 2) * 20);
        assert(stereo_link_push(&link, 0, &frame, 1000000 + sequence * 20000));
        assert(!link.queue[0][sequence - 1].filtered[8].valid);
        assert(!link.queue[0][sequence - 1].filtered[10].valid);
    }
    assert(link.candidate[0][0].count == 1 && link.candidate[0][1].count == 1);
    frame.sequence = 5;
    assert(stereo_link_push(&link, 0, &frame, 1100000));
    assert(!stereo_link_push(&link, 0, &frame, 1100000));
    assert(link.candidate[0][1].count == 2);
    frame.sequence = 6;
    frame.joint[10].valid = frame.joint[8].valid = 0;
    assert(stereo_link_push(&link, 0, &frame, 1120000));
    assert(!link.candidate[0][1].count && link.accepted_raw[0][10].y == 560);
    frame.joint[10].valid = frame.joint[8].valid = 1;
    frame.sequence = 7;
    assert(stereo_link_push(&link, 0, &frame, 1140000));
    assert(!stereo_link_take_at(&link, &result, 1400001));
    assert(!link.candidate[0][1].count && link.reacquire[0][1]);
    for (sequence = 8; sequence <= 10; ++sequence) {
        frame.sequence = sequence;
        assert(stereo_link_push(&link, 0, &frame, 1400001 + (sequence - 8) * 20000));
        assert(link.queue[0][sequence - 8].filtered[10].valid == (sequence == 10));
    }
    assert(link.filtered[0][10].y == 640 && link.reacquired_points == 2);
    frame.sequence = 11;
    frame.joint[8].y = frame.joint[10].y = 560;
    assert(stereo_link_push(&link, 0, &frame, 1460001));
    assert(!link.queue[0][3].filtered[10].valid);
    frame.sequence = 12;
    frame.joint[8].y = frame.joint[10].y = 640;
    assert(stereo_link_push(&link, 0, &frame, 1480001));
    assert(link.queue[0][4].filtered[10].valid && !link.candidate[0][1].count);
    assert(stereo_link_init(&link, &calibration));
    stereo_link_set_async_test(&link, 1);
    frame = async_frame(0, 1);
    assert(stereo_link_push(&link, 0, &frame, 1000000));
    for (sequence = 2; sequence <= 4; ++sequence) {
        frame = async_frame(0, sequence);
        frame.joint[8].x += 170;
        frame.joint[10].x += 170;
        assert(stereo_link_push(&link, 0, &frame, 1000000 + (sequence - 1) * 100000));
        assert(link.queue[0][link.count[0] - 1].filtered[10].valid == (sequence == 4));
    }
    assert(link.reacquired_points == 2 && link.accepted_raw[0][10].x == 1210);
}

static void test_marker_quality(void)
{
    StereoLink link;
    StereoCalibration calibration = rail_calibration();
    StereoDepthResult result;
    StereoCoordinateFrame left, right;
    unsigned sequence;
    assert(stereo_link_init(&link, &calibration));
    stereo_link_set_async_test(&link, 1);
    for (sequence = 1; sequence <= 4; ++sequence) {
        left = async_frame(0, sequence);
        right = async_frame(1, sequence);
        if (sequence == 2) left.markers[0] = 0x80000000U | (5U << 11) | 5U;
        if (sequence == 4) right.markers[2] = 0U;
        assert(stereo_link_push(&link, 0, &left, 1000000 + sequence * 50000));
        assert(stereo_link_push(&link, 1, &right, 1000000 + sequence * 50000));
        assert(stereo_link_take(&link, &result));
        assert(result.async_status == STEREO_POSE_OK && result.image_pose.valid);
        assert(result.image_pose.gripper_2d.source == (sequence == 2 || sequence == 3 ? 2U : 1U));
        assert(result.image_pose.gripper_2d.finger1.valid && result.image_pose.gripper_2d.finger2.valid);
        if (sequence == 2) {
            assert(!result.image_pose.finger1.valid && !result.async_diagnostics.hand_valid);
            assert(link.accepted_raw[0][STEREO_UART_JOINTS].x == 1060);
            assert(link.filtered[0][STEREO_UART_JOINTS].x == 1060);
        }
    }
    assert(link.marker_rejects == 1);
    left = async_frame(0, 5);
    right = async_frame(1, 5);
    left.markers[0] = 0x80000000U | (560U << 11) | 1140U;
    left.markers[2] = 0x80000000U | (560U << 11) | 1180U;
    right.markers[0] = 0x80000000U | (560U << 11) | 940U;
    right.markers[2] = 0x80000000U | (560U << 11) | 980U;
    assert(stereo_link_push(&link, 0, &left, 1250000));
    assert(stereo_link_push(&link, 1, &right, 1250000));
    assert(stereo_link_take(&link, &result));
    assert(result.image_pose.finger1.valid && result.image_pose.finger1.x == 1140);
    assert(result.image_pose.finger2.valid && result.image_pose.finger2.x == 1180);
    assert(!link.jump_rejects && result.async_status == STEREO_POSE_OK);
    assert(stereo_link_init(&link, &calibration));
    left = async_frame(0, 1);
    left.joint[8].x = 1030;
    left.markers[0] = 0x80000000U | (560U << 11) | 1200U;
    left.markers[2] = 0x80000000U | (560U << 11) | 1201U;
    assert(stereo_link_push(&link, 0, &left, 1000000));
    assert(link.queue[0][0].filtered[STEREO_UART_JOINTS].valid);
    assert(!link.queue[0][0].filtered[STEREO_UART_JOINTS + 2].valid);
    assert(link.queue[0][0].filtered[8].valid && link.queue[0][0].filtered[10].valid);
    left.sequence = 2;
    left.markers[0] = 0x80000000U | (560U << 11) | 540U;
    left.markers[2] = 0x80000000U | (560U << 11) | 1200U;
    assert(stereo_link_push(&link, 0, &left, 1050000));
    assert(!link.queue[0][1].filtered[STEREO_UART_JOINTS].valid);
    assert(link.queue[0][1].filtered[STEREO_UART_JOINTS + 2].valid);
    assert(link.accepted_raw[0][STEREO_UART_JOINTS].x == 1200);
    assert(link.marker_rejects == 2);
}

static void test_reprojection_options(void)
{
    StereoCalibration calibration = rail_calibration();
    StereoGeometryContext geometry;
    StereoGeometryOptions options = stereo_geometry_default_options();
    StereoPoint3D point;
    assert(options.max_reprojection_error_px == 15.0);
    assert(stereo_geometry_init(&geometry, &calibration, NULL) == STEREO_OK);
    assert(stereo_reconstruct_point(&geometry, 840, 560, 640, 580, &point) == STEREO_OK);
    assert(stereo_reconstruct_point(&geometry, 840, 560, 640, 600, &point) == STEREO_LOW_QUALITY);
    options.max_reprojection_error_px = 2.0;
    assert(stereo_geometry_init(&geometry, &calibration, &options) == STEREO_OK);
    assert(stereo_reconstruct_point(&geometry, 840, 560, 640, 580, &point) == STEREO_LOW_QUALITY);
}

int main(void)
{
    StereoLink link;
    StereoCalibration calibration = rail_calibration();
    StereoCoordinateFrame left = make_frame(0, 1, 1000000), right = make_frame(1, 1, 1000500);
    StereoDepthResult depth;
    HumanPose2D image;
    HumanPose3D measured;
    ForearmMappingContext forearm;
    HumanForearmTarget target;
    cnn_result_t local;
    float dt;
    unsigned index;
    test_pixel_filter();
    test_receive_matching();
    test_jump_cluster();
    test_marker_quality();
    test_reprojection_options();
    assert(stereo_link_init(&link, &calibration));
    assert(stereo_link_push(&link, 0, &left, 1000000));
    assert(stereo_link_push(&link, 1, &right, 1001000));
    assert(stereo_link_take(&link, &depth));
    assert(depth.time_verified && depth.control_status == STEREO_POSE_OK);
    assert(depth.diagnostics.exposure_skew_us == 500);
    for (index = 0; index < STEREO_LINK_POINTS; ++index) {
        assert(depth.point_status[index] == STEREO_OK);
        assert(fabs(depth.point[index].z_mm - 1000) < 1e-6);
    }
    assert(depth.image_pose.finger2.x == 1080 && depth.measured_pose.finger2.valid);
    assert(fabsf(depth.measured_pose.wrist.y + 200) < 0.001f);
    assert(depth.measured_pose.frame_id == depth.image_pose.frame_id);
    input_pose_init();
    input_pose_cnn_set_stereo(1);
    memset(&local, 0, sizeof(local));
    assert(!input_pose_cnn_publish(&local));
    assert(input_pose_cnn_publish_stereo(&depth));
    assert(!input_pose_cnn_publish_stereo(&depth));
    assert(!input_pose_take(&image, &dt));
    assert(input_pose_cnn_take_stereo(&image, &measured, &dt));
    assert(fabsf(dt - 0.1f) < 1e-6f);
    forearm_mapping_init(&forearm);
    assert(forearm_mapping_update_stereo(&forearm, &image, &measured, POSE_ARM_RIGHT, dt, &target) == 1);
    assert(target.valid && forearm.stereo_input_active);
    assert(fabsf(forearm.pose.wrist_3d.z - 1000.0f) < 0.001f);
    assert(fabsf(forearm.pose.wrist_3d.y + 200.0f) < 0.001f);
    assert(!stereo_link_push(&link, 0, &left, 1001001));
    left = make_frame(0, 2, 1020000);
    right = make_frame(1, 2, 1020000);
    left.markers[2] = 0;
    assert(stereo_link_push(&link, 0, &left, 1021000));
    assert(stereo_link_push(&link, 1, &right, 1022000));
    assert(stereo_link_take(&link, &depth));
    assert(depth.control_status == STEREO_POSE_OK && !depth.diagnostics.hand_valid);
    assert(!depth.measured_pose.finger1.valid && !depth.measured_pose.finger2.valid);
    assert(input_pose_cnn_publish_stereo(&depth));
    assert(input_pose_cnn_take_stereo(&image, &measured, &dt));
    assert(fabsf(dt - 0.02f) < 1e-6f);
    assert(forearm_mapping_update_stereo(&forearm, &image, &measured, POSE_ARM_RIGHT, dt, &target) == 1);
    assert(!forearm.pose.finger_pose3d_valid);
    assert(stereo_link_init(&link, &calibration));
    left = make_frame(0, 3, 2000000);
    right = make_frame(1, 3, 2010000);
    assert(stereo_link_push(&link, 0, &left, 2000000));
    assert(stereo_link_push(&link, 1, &right, 2001000));
    assert(!stereo_link_take(&link, &depth));
    assert(stereo_link_init(&link, &calibration));
    left.metadata.exposure_time_verified = right.metadata.exposure_time_verified = 0;
    assert(stereo_link_push(&link, 0, &left, 3000000));
    assert(stereo_link_push(&link, 1, &right, 3001000));
    assert(stereo_link_take(&link, &depth));
    assert(depth.point_status[10] == STEREO_OK && !depth.time_verified);
    assert(depth.control_status == STEREO_POSE_UNVERIFIED && !depth.measured_pose.valid);
    assert(!input_pose_cnn_publish_stereo(&depth) && !input_pose_ready());
    assert(stereo_link_init(&link, &calibration));
    assert(stereo_link_push(&link, 0, &left, 4000000));
    assert(stereo_link_push(&link, 1, &right, 4300000));
    assert(!stereo_link_take(&link, &depth) && link.expired_frames == 1);
    assert(stereo_link_init(&link, &calibration));
    left = make_frame(0, UINT32_MAX, 5000000);
    right = make_frame(1, 1, 5000000);
    assert(stereo_link_push(&link, 0, &left, 5000000));
    assert(stereo_link_push(&link, 1, &right, 5001000));
    assert(stereo_link_take(&link, &depth));
    left.sequence = 0;
    left.frame_id = 2;
    left.metadata.exposure_time_us += 20000;
    assert(stereo_link_push(&link, 0, &left, 5020000));
    left.sequence = 1;
    left.metadata.shared_clock_epoch = 8;
    assert(!stereo_link_push(&link, 0, &left, 5021000));
    left.session_id = 123;
    assert(stereo_link_push(&link, 0, &left, 5022000));
    right = make_frame(1, 2, 5020000);
    assert(stereo_link_push(&link, 1, &right, 5023000));
    assert(!stereo_link_take(&link, &depth));
    assert(stereo_link_init(&link, &calibration));
    left = make_frame(0, 1, 6000000);
    right = make_frame(1, 1, 6000000);
    right.metadata.localization_quality_verified = 0;
    assert(stereo_link_push(&link, 0, &left, 6000000));
    assert(stereo_link_push(&link, 1, &right, 6001000));
    assert(stereo_link_take(&link, &depth));
    assert(depth.time_verified && depth.control_status == STEREO_POSE_UNVERIFIED);
    assert(depth.point_status[10] == STEREO_OK && !input_pose_cnn_publish_stereo(&depth));
    assert(stereo_link_init(&link, &calibration));
    left = make_frame(0, 2, 6020000);
    right = make_frame(1, 2, 6020000);
    left.metadata.fixed_geometry_verified = 0;
    assert(stereo_link_push(&link, 0, &left, 6020000));
    assert(stereo_link_push(&link, 1, &right, 6021000));
    assert(stereo_link_take(&link, &depth));
    assert(depth.control_status == STEREO_POSE_UNVERIFIED && !input_pose_cnn_publish_stereo(&depth));
    assert(stereo_link_init(&link, &calibration));
    left = make_frame(0, 3, 6040000);
    right = make_frame(1, 3, 6040000);
    left.joint[10].valid = 0;
    assert(stereo_link_push(&link, 0, &left, 6040000));
    assert(stereo_link_push(&link, 1, &right, 6041000));
    assert(stereo_link_take(&link, &depth));
    assert(depth.control_status == STEREO_POSE_MAJOR_INVALID && !depth.measured_pose.valid);
    assert(!input_pose_cnn_publish_stereo(&depth));
    input_pose_cnn_set_stereo(0);
    assert(input_pose_cnn_publish(&local));
    assert(input_pose_take(&image, &dt));
    puts("test_stereo_link: PASS");
    return 0;
}
