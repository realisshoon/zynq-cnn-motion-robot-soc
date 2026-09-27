#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "xtime_l.h"

#include "integration/input_pose.h"
#include "../../src/integration/input_pose_cnn.h"

static XTime fake_time;
void XTime_GetTime(XTime *time) { *time = fake_time; }

static u32 marker(unsigned x, unsigned y, int valid)
{
    return (valid ? 0x80000000U : 0U) | (y << 11) | x;
}

int main(void)
{
    cnn_result_t result;
    HumanPose2D pose;
    float dt;
    memset(&result, 0, sizeof(result));
    input_pose_init();
    assert(!input_pose_ready());
    result.frame_id = 12U;
    result.joint[CNN_JOINT_LEFT_SHOULDER] = (cnn_joint_t){100U, 200U, 50, 1U};
    result.joint[CNN_JOINT_RIGHT_SHOULDER] = (cnn_joint_t){300U, 201U, 50, 1U};
    result.joint[CNN_JOINT_RIGHT_ELBOW] = (cnn_joint_t){400U, 350U, 50, 1U};
    result.joint[CNN_JOINT_RIGHT_WRIST] = (cnn_joint_t){500U, 450U, 50, 1U};
    result.red_marker = marker(520U, 470U, 1);
    result.blue_marker = marker(550U, 480U, 1);
    fake_time = 1000000U;
    assert(input_pose_cnn_publish(&result));
    assert(!input_pose_cnn_publish(&result));
    assert(input_pose_cnn_overwritten() == 0U);
    assert(input_pose_take(&pose, &dt));
    assert(pose.valid && pose.frame_id == 12U);
    assert(pose.shoulder_l.x == 100.0f && pose.shoulder_r.x == 300.0f);
    assert(pose.elbow.y == 350.0f && pose.wrist.y == 450.0f);
    assert(pose.finger1.valid && pose.finger1.x == 520.0f);
    assert(pose.finger2.valid && pose.finger2.y == 480.0f);
    assert(dt > 0.0f);
    assert(!input_pose_take(&pose, &dt));

    result.frame_id = 13U;
    result.joint[CNN_JOINT_RIGHT_WRIST].x = 1280U;
    result.blue_marker = marker(550U, 720U, 1);
    fake_time = 1020000U;
    assert(input_pose_cnn_publish(&result));
    result.frame_id = 14U;
    fake_time = 1050000U;
    assert(input_pose_cnn_publish(&result));
    assert(input_pose_cnn_overwritten() == 1U);
    assert(input_pose_take(&pose, &dt));
    assert(pose.frame_id == 14U && !pose.valid && !pose.wrist.valid);
    assert(!pose.finger2.valid);
    assert(fabsf(dt - 0.03f) < 0.00001f);
    puts("test_input_pose_cnn: PASS");
    return 0;
}
