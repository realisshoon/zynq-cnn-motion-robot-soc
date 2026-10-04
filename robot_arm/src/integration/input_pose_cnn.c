#include "integration/input_pose.h"
#include "input_pose_cnn.h"
#include "stereo_filter_config.h"

#if ROBOT_STEREO_ONE_EURO_ENABLE
#include "stereo_vision/stereo_pose_filter.h"
#endif

#include <string.h>
#include <math.h>
#include "xtime_l.h"

#define CNN_IMAGE_WIDTH 1280U
#define CNN_IMAGE_HEIGHT 720U
#define CNN_FIRST_DT_SEC 0.1f
#define CNN_REACQUIRE_FRAMES 3U
#define CNN_REACQUIRE_RADIUS_MM 50.0f
#define CNN_JUMP_LIMIT_MM 150.0f

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
static const char *s_admission_reason = "OFF";
static const char *s_tracking_state = "OFF";
static uint32_t s_filter_epoch;
static int s_have_seen_async, s_pending_async;
static uint32_t s_seen_left_session, s_seen_right_session;
static uint32_t s_seen_left_sequence, s_seen_right_sequence;
static uint64_t s_seen_received, s_async_left_received;
static HumanPose3D s_reacquire_anchor;
static unsigned s_reacquire_count;
static uint64_t s_reacquire_received;
static uint32_t s_reacquire_left_session, s_reacquire_right_session;

#if ROBOT_STEREO_ONE_EURO_ENABLE
static StereoPoseFilter s_stereo_filter;
static uint32_t s_stereo_filter_epoch;
#endif

static uint64_t current_time_us(void)
{
    XTime ticks;
    XTime_GetTime(&ticks);
    return (ticks / COUNTS_PER_SECOND) * 1000000U +
        (ticks % COUNTS_PER_SECOND) * 1000000U / COUNTS_PER_SECOND;
}

static void clear_reacquisition(void)
{
    s_reacquire_count = 0U;
    s_tracking_state = s_async_test ? (s_have_async ? "HOLD" : "WAIT") : "OFF";
}

static int reject_async(const char *reason)
{
    s_admission_reason = reason;
    clear_reacquisition();
    return 0;
}

static int points_within(Point3D first, Point3D second, float limit)
{
    float delta_x = first.x - second.x;
    float delta_y = first.y - second.y;
    float delta_z = first.z - second.z;
    float distance = sqrtf(delta_x * delta_x + delta_y * delta_y + delta_z * delta_z);
    return isfinite(distance) && distance <= limit;
}

static int poses_within(const HumanPose3D *first, const HumanPose3D *second, float limit)
{
    return points_within(first->elbow, second->elbow, limit) &&
        points_within(first->wrist, second->wrist, limit);
}

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
    s_admission_reason = "OFF";
    s_tracking_state = "OFF";
    s_have_seen_async = s_pending_async = 0;
    s_reacquire_count = 0U;
    ++s_filter_epoch;
#if ROBOT_STEREO_ONE_EURO_ENABLE
    stereo_pose_filter_init(&s_stereo_filter);
    s_stereo_filter_epoch = s_filter_epoch;
#endif
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
    s_have_seen_async = s_pending_async = 0;
    clear_reacquisition();
    ++s_filter_epoch;
}

static int publish_async_test(const StereoDepthResult *result)
{
    uint64_t now;
    uint32_t left_advance, right_advance;
    const Point3D *current[2];
    unsigned index;
    int same_session, reacquired = 0;
    s_admission_reason = "OFF";
    if (!s_async_test || !result->async_test) return reject_async("OFF");
    now = current_time_us();
    if (now < result->left_received_us || now < result->right_received_us)
        return reject_async("CLOCK");
    if (now - result->left_received_us > STEREO_LINK_MAX_AGE_US ||
        now - result->right_received_us > STEREO_LINK_MAX_AGE_US)
        return reject_async("STALE");
    if ((result->left_received_us > result->right_received_us
            ? result->left_received_us - result->right_received_us
            : result->right_received_us - result->left_received_us) > STEREO_LINK_MAX_RECEIVE_GAP_US)
        return reject_async("MATCH_GAP");
    if (result->time_verified ||
        result->control_status != STEREO_POSE_UNVERIFIED ||
        result->async_status != STEREO_POSE_OK || !result->image_pose.valid ||
        !result->async_measured_pose.valid || !result->left_session_id || !result->right_session_id ||
        result->image_pose.frame_id != result->async_measured_pose.frame_id)
        return reject_async("GEOMETRY");
    current[0] = &result->async_measured_pose.elbow;
    current[1] = &result->async_measured_pose.wrist;
    for (index = 0; index < 2; ++index)
        if (!current[index]->valid || !isfinite(current[index]->x) ||
            !isfinite(current[index]->y) || !isfinite(current[index]->z))
            return reject_async("INVALID");
    if (s_have_seen_async) {
        left_advance = result->left_sequence - s_seen_left_sequence;
        right_advance = result->right_sequence - s_seen_right_sequence;
        if ((result->left_session_id == s_seen_left_session &&
             (!left_advance || left_advance >= 0x80000000U)) ||
            (result->right_session_id == s_seen_right_session &&
             (!right_advance || right_advance >= 0x80000000U ||
              result->right_received_us <= s_seen_received))) {
            s_admission_reason = "SEQUENCE";
            return 0;
        }
    }
    s_seen_left_session = result->left_session_id;
    s_seen_right_session = result->right_session_id;
    s_seen_left_sequence = result->left_sequence;
    s_seen_right_sequence = result->right_sequence;
    s_seen_received = result->right_received_us;
    s_have_seen_async = 1;
    same_session = s_have_async && result->left_session_id == s_async_left_session &&
        result->right_session_id == s_async_right_session;
    if (s_have_async && (!same_session ||
        !poses_within(&result->async_measured_pose, &s_measured, CNN_JUMP_LIMIT_MM))) {
        if (!s_reacquire_count || result->left_session_id != s_reacquire_left_session ||
            result->right_session_id != s_reacquire_right_session ||
            result->right_received_us <= s_reacquire_received ||
            result->right_received_us - s_reacquire_received > STEREO_LINK_MAX_AGE_US ||
            !poses_within(&result->async_measured_pose, &s_reacquire_anchor,
                          CNN_REACQUIRE_RADIUS_MM)) {
            s_reacquire_anchor = result->async_measured_pose;
            s_reacquire_left_session = result->left_session_id;
            s_reacquire_right_session = result->right_session_id;
            s_reacquire_count = 1U;
        } else {
            ++s_reacquire_count;
        }
        s_reacquire_received = result->right_received_us;
        if (s_reacquire_count < CNN_REACQUIRE_FRAMES) {
            s_tracking_state = s_reacquire_count == 1U ? "HOLD" : "REACQUIRE";
            s_admission_reason = s_reacquire_count == 1U ? "JUMP_HOLD" : "REACQ_WAIT";
            return 0;
        }
        reacquired = 1;
    }
    s_dt_sec = same_session && !reacquired &&
        result->right_received_us - s_async_received <= STEREO_LINK_MAX_AGE_US
        ? (float)((double)(result->right_received_us - s_async_received) / 1000000.0)
        : CNN_FIRST_DT_SEC;
    if (!s_have_async || reacquired ||
        (same_session && result->right_received_us - s_async_received > STEREO_LINK_MAX_AGE_US))
        ++s_filter_epoch;
    s_async_received = result->right_received_us;
    s_async_left_session = result->left_session_id;
    s_async_right_session = result->right_session_id;
    s_async_left_sequence = result->left_sequence;
    s_async_right_sequence = result->right_sequence;
    s_async_left_received = result->left_received_us;
    s_have_async = 1;
    if (s_ready) ++s_overwritten;
    s_pose = result->image_pose;
    s_measured = result->async_measured_pose;
    s_ready = 1;
    s_pending_async = 1;
    s_tracking_state = "TRACK";
    s_admission_reason = reacquired ? "REACQ_ACCEPT" : "OK";
    s_reacquire_count = 0U;
    return 1;
}

int input_pose_cnn_publish_stereo(const StereoDepthResult *result)
{
    const StereoFrameMetadata *left, *right;
    uint64_t skew;
#if ROBOT_STEREO_ONE_EURO_ENABLE
    if (s_stereo && result != NULL)
        stereo_pose_filter_observe_invalid(&s_stereo_filter, result->time_verified
            ? &result->measured_pose : &result->async_measured_pose);
#endif
    if (s_stereo && result != NULL && !result->time_verified)
        return publish_async_test(result);
    if (s_async_test) clear_reacquisition();
    s_admission_reason = "UNVERIFIED";
    if (!s_stereo || result == NULL || !result->time_verified ||
        result->control_status != STEREO_POSE_OK || !result->image_pose.valid ||
        !result->measured_pose.valid || result->image_pose.frame_id != result->measured_pose.frame_id) return 0;
    left = &result->left_metadata;
    right = &result->right_metadata;
    s_admission_reason = "METADATA";
    if (!left->exposure_time_verified || !right->exposure_time_verified ||
        !left->fixed_geometry_verified || !right->fixed_geometry_verified ||
        !left->localization_quality_verified || !right->localization_quality_verified ||
        !left->shared_clock_epoch || left->shared_clock_epoch != right->shared_clock_epoch) return 0;
    skew = left->exposure_time_us > right->exposure_time_us
        ? left->exposure_time_us - right->exposure_time_us
        : right->exposure_time_us - left->exposure_time_us;
    s_admission_reason = "TIME_SEQUENCE";
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
    s_pending_async = 0;
    s_admission_reason = "OK";
    return 1;
}

const char *input_pose_cnn_admission_reason(void)
{
    return s_admission_reason;
}

const char *input_pose_cnn_tracking_state(void)
{
    return s_tracking_state;
}

unsigned input_pose_cnn_reacquire_count(void)
{
    return s_reacquire_count;
}

uint32_t input_pose_cnn_filter_epoch(void)
{
    return s_filter_epoch;
}

int input_pose_cnn_take_stereo(HumanPose2D *image_pose, HumanPose3D *measured_pose, float *dt_sec)
{
    if (!s_stereo || !s_ready || image_pose == NULL || measured_pose == NULL || dt_sec == NULL) return 0;
    if (s_pending_async) {
        uint64_t now = current_time_us();
        if (now < s_async_left_received || now < s_async_received ||
            now - s_async_left_received > STEREO_LINK_MAX_AGE_US ||
            now - s_async_received > STEREO_LINK_MAX_AGE_US) {
            s_ready = 0;
            return reject_async("STALE");
        }
    }
    *image_pose = s_pose;
    *measured_pose = s_measured;
    *dt_sec = s_dt_sec;
    s_ready = 0;
#if ROBOT_STEREO_ONE_EURO_ENABLE
    if (s_stereo_filter_epoch != s_filter_epoch) {
        stereo_pose_filter_init(&s_stereo_filter);
        s_stereo_filter_epoch = s_filter_epoch;
    }
    if (!stereo_pose_filter_apply(&s_stereo_filter, measured_pose, *dt_sec)) {
        s_admission_reason = "FILTER_GEOMETRY";
        return 0;
    }
#endif
    return 1;
}

void input_pose_cnn_discard_stereo_pending(void)
{
    if (s_stereo) s_ready = 0;
}
