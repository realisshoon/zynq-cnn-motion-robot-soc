#include "integration/input_pose.h"
#include "input_pose_cnn.h"

#include <string.h>
#include "xtime_l.h"

#define CNN_IMAGE_WIDTH 1280U
#define CNN_IMAGE_HEIGHT 720U
#define CNN_FIRST_DT_SEC 0.1f

static HumanPose2D s_pose;
static float s_dt_sec;
static XTime s_last_time;
static u32 s_last_frame_id;
static unsigned s_overwritten;
static int s_have_last;
static int s_ready;

static Point2D joint_point(const cnn_result_t *result, unsigned index)
{
    const cnn_joint_t *joint = &result->joint[index];
    Point2D point;
    point.x = (float)joint->x;
    point.y = (float)joint->y;
    point.valid = joint->valid && joint->x < CNN_IMAGE_WIDTH &&
                  joint->y < CNN_IMAGE_HEIGHT;
    return point;
}

static Point2D marker_point(u32 word)
{
    Point2D point;
    point.x = (float)(word & 0x7FFU);
    point.y = (float)((word >> 11) & 0x3FFU);
    point.valid = ((word >> 31) & 1U) && point.x < CNN_IMAGE_WIDTH &&
                  point.y < CNN_IMAGE_HEIGHT;
    return point;
}

void input_pose_init(void)
{
    memset(&s_pose, 0, sizeof(s_pose));
    s_dt_sec = CNN_FIRST_DT_SEC;
    s_last_time = 0U;
    s_last_frame_id = 0U;
    s_overwritten = 0U;
    s_have_last = 0;
    s_ready = 0;
}

int input_pose_cnn_publish(const cnn_result_t *result)
{
    XTime now;
    HumanPose2D pose;

    if (result == 0) return 0;
    if (s_have_last && result->frame_id == s_last_frame_id) return 0;

    memset(&pose, 0, sizeof(pose));
    pose.frame_id = result->frame_id;
    pose.shoulder_l = joint_point(result, CNN_JOINT_LEFT_SHOULDER);
    pose.shoulder_r = joint_point(result, CNN_JOINT_RIGHT_SHOULDER);
    pose.elbow = joint_point(result, CNN_JOINT_RIGHT_ELBOW);
    pose.wrist = joint_point(result, CNN_JOINT_RIGHT_WRIST);
    pose.finger1 = marker_point(result->red_marker);
    pose.finger2 = marker_point(result->blue_marker);
    pose.valid = pose.shoulder_l.valid && pose.shoulder_r.valid &&
                 pose.elbow.valid && pose.wrist.valid;

    XTime_GetTime(&now);
    if (s_have_last)
        s_dt_sec = (float)((double)(now - s_last_time) /
                           (double)COUNTS_PER_SECOND);
    else
        s_dt_sec = CNN_FIRST_DT_SEC;
    s_last_time = now;
    s_last_frame_id = result->frame_id;
    s_have_last = 1;
    if (s_ready) ++s_overwritten;
    s_pose = pose;
    s_ready = 1;
    return 1;
}

int input_pose_ready(void)
{
    return s_ready;
}

int input_pose_take(HumanPose2D *pose, float *dt_sec)
{
    if (!s_ready || pose == 0 || dt_sec == 0) return 0;
    *pose = s_pose;
    *dt_sec = s_dt_sec;
    s_ready = 0;
    return 1;
}

unsigned input_pose_cnn_overwritten(void)
{
    return s_overwritten;
}
