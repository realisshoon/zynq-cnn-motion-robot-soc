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
