#include "integration/input_pose.h"
#include "input_pose_cnn.h"

#include <string.h>
#include <math.h>
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
static int s_stereo;
static HumanPose3D s_measured;
static uint64_t s_last_exposure;
static uint32_t s_clock_epoch;
static int s_async_test, s_have_async;
static uint64_t s_async_received;
static uint32_t s_async_left_session, s_async_right_session;
static uint32_t s_async_left_sequence, s_async_right_sequence;

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
    s_stereo = 0;
    memset(&s_measured, 0, sizeof(s_measured));
    s_last_exposure = 0;
    s_clock_epoch = 0;
    s_async_test = s_have_async = 0;
    s_async_received = 0;
    s_async_left_session = s_async_right_session = 0;
    s_async_left_sequence = s_async_right_sequence = 0;
}

int input_pose_cnn_publish(const cnn_result_t *result)
{
    XTime now;
    HumanPose2D pose;

    if (result == 0 || s_stereo) return 0;
    if (s_have_last && result->frame_id == s_last_frame_id) return 0;

    memset(&pose, 0, sizeof(pose));
    pose.frame_id = result->frame_id;
    pose.shoulder_l = joint_point(result, CNN_JOINT_LEFT_SHOULDER);
    pose.shoulder_r = joint_point(result, CNN_JOINT_RIGHT_SHOULDER);
    pose.elbow = joint_point(result, CNN_JOINT_RIGHT_ELBOW);
    pose.wrist = joint_point(result, CNN_JOINT_RIGHT_WRIST);
    pose.finger1 = marker_point(result->red_marker);
    pose.finger2 = marker_point(result->green_marker); /* 실물 마커는 빨강+초록 (2026-09-30 확인) */
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
    if (s_stereo || !s_ready || pose == 0 || dt_sec == 0) return 0;
    *pose = s_pose;
    *dt_sec = s_dt_sec;
    s_ready = 0;
    return 1;
}

unsigned input_pose_cnn_overwritten(void)
{
    return s_overwritten;
}

void input_pose_cnn_set_stereo(int enabled)
{
    input_pose_init();
    s_stereo = enabled ? 1 : 0;
}

void input_pose_cnn_set_async_test(int enabled)
{
    s_async_test = s_stereo && enabled;
    s_ready = s_have_last = s_have_async = 0;
}

static int publish_async_test(const StereoDepthResult *result)
{
    XTime ticks;
    uint64_t now;
    uint32_t left_advance, right_advance;
    const Point3D *previous[2], *current[2];
    unsigned index;
    int same_session;
    if (!s_async_test || !result->async_test || result->time_verified ||
        result->control_status != STEREO_POSE_UNVERIFIED ||
        result->async_status != STEREO_POSE_OK || !result->image_pose.valid ||
        !result->async_measured_pose.valid || !result->left_session_id || !result->right_session_id ||
        result->image_pose.frame_id != result->async_measured_pose.frame_id) return 0;
    XTime_GetTime(&ticks);
    now = (ticks / COUNTS_PER_SECOND) * 1000000U +
        (ticks % COUNTS_PER_SECOND) * 1000000U / COUNTS_PER_SECOND;
    if (now < result->left_received_us || now < result->right_received_us) return 0;
    current[0] = &result->async_measured_pose.elbow;
    current[1] = &result->async_measured_pose.wrist;
    for (index = 0; index < 2; ++index)
        if (!current[index]->valid || !isfinite(current[index]->x) ||
            !isfinite(current[index]->y) || !isfinite(current[index]->z)) return 0;
    same_session = s_have_async && result->left_session_id == s_async_left_session &&
        result->right_session_id == s_async_right_session;
    if (same_session) {
        left_advance = result->left_sequence - s_async_left_sequence;
        right_advance = result->right_sequence - s_async_right_sequence;
        if (!left_advance || left_advance >= 0x80000000U || !right_advance ||
            right_advance >= 0x80000000U || result->right_received_us <= s_async_received) return 0;
    }
    if (s_have_async) {
        previous[0] = &s_measured.elbow; previous[1] = &s_measured.wrist;
        for (index = 0; index < 2; ++index) {
            float delta_x = current[index]->x - previous[index]->x;
            float delta_y = current[index]->y - previous[index]->y;
            float delta_z = current[index]->z - previous[index]->z;
            float distance = sqrtf(delta_x * delta_x + delta_y * delta_y + delta_z * delta_z);
            if (!isfinite(distance) || distance > 150.0f) return 0;
        }
    }
    s_dt_sec = same_session ? (float)((double)(result->right_received_us - s_async_received) / 1000000.0)
        : CNN_FIRST_DT_SEC;
    s_async_received = result->right_received_us;
    s_async_left_session = result->left_session_id;
    s_async_right_session = result->right_session_id;
    s_async_left_sequence = result->left_sequence;
    s_async_right_sequence = result->right_sequence;
    s_have_async = 1;
    if (s_ready) ++s_overwritten;
    s_pose = result->image_pose;
    s_measured = result->async_measured_pose;
    s_ready = 1;
    return 1;
}

int input_pose_cnn_publish_stereo(const StereoDepthResult *result)
{
    const StereoFrameMetadata *left, *right;
    uint64_t skew;
    if (s_stereo && result != NULL && !result->time_verified)
        return publish_async_test(result);
    if (!s_stereo || result == NULL || !result->time_verified ||
        result->control_status != STEREO_POSE_OK || !result->image_pose.valid ||
        !result->measured_pose.valid || result->image_pose.frame_id != result->measured_pose.frame_id) return 0;
    left = &result->left_metadata;
    right = &result->right_metadata;
    if (!left->exposure_time_verified || !right->exposure_time_verified ||
        !left->fixed_geometry_verified || !right->fixed_geometry_verified ||
        !left->localization_quality_verified || !right->localization_quality_verified ||
        !left->shared_clock_epoch || left->shared_clock_epoch != right->shared_clock_epoch) return 0;
    skew = left->exposure_time_us > right->exposure_time_us
        ? left->exposure_time_us - right->exposure_time_us
        : right->exposure_time_us - left->exposure_time_us;
    if (skew > STEREO_LINK_MAX_SKEW_US || (s_have_last &&
        (left->shared_clock_epoch != s_clock_epoch || left->exposure_time_us <= s_last_exposure ||
         result->image_pose.frame_id == s_last_frame_id))) return 0;
    s_dt_sec = s_have_last ? (float)((double)(left->exposure_time_us - s_last_exposure) / 1000000.0)
                          : CNN_FIRST_DT_SEC;
    s_last_exposure = left->exposure_time_us;
    s_clock_epoch = left->shared_clock_epoch;
    s_last_frame_id = result->image_pose.frame_id;
    s_have_last = 1;
    if (s_ready) ++s_overwritten;
    s_pose = result->image_pose;
    s_measured = result->measured_pose;
    s_ready = 1;
    return 1;
}

int input_pose_cnn_take_stereo(HumanPose2D *image_pose, HumanPose3D *measured_pose, float *dt_sec)
{
    if (!s_stereo || !s_ready || image_pose == NULL || measured_pose == NULL || dt_sec == NULL) return 0;
    *image_pose = s_pose;
    *measured_pose = s_measured;
    *dt_sec = s_dt_sec;
    s_ready = 0;
    return 1;
}

void input_pose_cnn_discard_stereo_pending(void)
{
    if (s_stereo) s_ready = 0;
}
