#include "stereo_vision/stereo_link.h"

#include <string.h>
#include <math.h>

static uint64_t distance_us(uint64_t first, uint64_t second)
{
    return first > second ? first - second : second - first;
}

static Point2D joint_point(const StereoCoordinateFrame *frame, unsigned index)
{
    StereoCoordinateJoint joint = frame->joint[index];
    return (Point2D){(float)joint.x, (float)joint.y,
        joint.valid && joint.x < 1280 && joint.y < 720};
}

static Point2D marker_point(uint32_t word)
{
    unsigned x = word & 0x7FFU, y = (word >> 11) & 0x3FFU;
    return (Point2D){(float)x, (float)y, (word >> 31) && x < 1280 && y < 720};
}

HumanPose2D stereo_coordinate_pose(const StereoCoordinateFrame *frame)
{
    HumanPose2D pose;
    memset(&pose, 0, sizeof(pose));
    if (frame == NULL) return pose;
    pose.frame_id = frame->frame_id;
    pose.shoulder_l = joint_point(frame, 5);
    pose.shoulder_r = joint_point(frame, 6);
    pose.elbow = joint_point(frame, 8);
    pose.wrist = joint_point(frame, 10);
    pose.finger1 = marker_point(frame->markers[0]);
    pose.finger2 = marker_point(frame->markers[2]);
    pose.valid = pose.elbow.valid && pose.wrist.valid;
    return pose;
}

int stereo_link_init(StereoLink *link, const StereoCalibration *calibration)
{
    if (link == NULL || calibration == NULL) return 0;
    memset(link, 0, sizeof(*link));
    return stereo_geometry_init(&link->geometry, calibration, NULL) == STEREO_OK;
}

static void remove_entry(StereoLink *link, unsigned side, unsigned index)
{
    --link->count[side];
    memmove(&link->queue[side][index], &link->queue[side][index + 1],
        (link->count[side] - index) * sizeof(StereoLinkEntry));
}

static void expire_entries(StereoLink *link, uint64_t now)
{
    unsigned side, index;
    for (side = 0; side < 2; ++side) {
        index = 0;
        while (index < link->count[side]) {
            uint64_t received = link->queue[side][index].received_us;
            if ((!link->async_test_enabled ||
                 link->queue[side][index].frame.metadata.exposure_time_verified) &&
                now >= received && now - received > STEREO_LINK_MAX_AGE_US) {
                remove_entry(link, side, index);
                ++link->expired_frames;
            } else ++index;
        }
    }
}

static void filter_entry(StereoLink *link, unsigned side, StereoLinkEntry *entry)
{
    const StereoCoordinateFrame *frame = &entry->frame;
    uint8_t verified = frame->metadata.exposure_time_verified;
    uint64_t time = verified ? frame->metadata.exposure_time_us : entry->received_us;
    uint64_t elapsed = time > link->filter_time[side] ? time - link->filter_time[side] : 0;
    int reset = !link->have_filter_time[side] || verified != link->filter_verified[side] ||
        !elapsed || elapsed > STEREO_LINK_FILTER_RESET_US;
    float alpha = reset ? 1.0f : 1.0f - expf(-(float)elapsed / STEREO_LINK_PIXEL_TAU_US);
    unsigned index;
    for (index = 0; index < STEREO_LINK_POINTS; ++index) {
        Point2D point = index < STEREO_UART_JOINTS ? joint_point(frame, index)
            : marker_point(frame->markers[index - STEREO_UART_JOINTS]);
        Point2D previous = link->filtered[side][index];
        float delta_x = point.x - previous.x, delta_y = point.y - previous.y;
        if (point.valid && previous.valid && !reset &&
            delta_x * delta_x + delta_y * delta_y <=
                STEREO_LINK_PIXEL_RESET_PX * STEREO_LINK_PIXEL_RESET_PX) {
            point.x = previous.x + alpha * delta_x;
            point.y = previous.y + alpha * delta_y;
        }
        entry->filtered[index] = link->filtered[side][index] = point;
    }
    link->filter_time[side] = time;
    link->filter_verified[side] = verified;
    link->have_filter_time[side] = 1U;
}

static HumanPose2D filtered_pose(const StereoLinkEntry *entry)
{
    HumanPose2D pose = stereo_coordinate_pose(&entry->frame);
    pose.shoulder_l = entry->filtered[5];
    pose.shoulder_r = entry->filtered[6];
    pose.elbow = entry->filtered[8];
    pose.wrist = entry->filtered[10];
    pose.finger1 = entry->filtered[STEREO_UART_JOINTS];
    pose.finger2 = entry->filtered[STEREO_UART_JOINTS + 2];
    pose.valid = pose.elbow.valid && pose.wrist.valid;
    return pose;
}

static void reconstruct_pair(StereoLink *link, const StereoLinkEntry *left_entry,
                             const StereoLinkEntry *right_entry, int synchronized)
{
    const StereoCoordinateFrame *left = &left_entry->frame;
    const StereoCoordinateFrame *right = &right_entry->frame;
    StereoDepthResult *result = &link->latest;
    HumanPose2D right_pose;
    unsigned index;
    if (link->ready) ++link->overwritten_results;
    memset(result, 0, sizeof(*result));
    result->left_frame_id = left->frame_id;
    result->right_frame_id = right->frame_id;
    result->left_received_us = left_entry->received_us;
    result->right_received_us = right_entry->received_us;
    result->left_session_id = left->session_id;
    result->right_session_id = right->session_id;
    result->left_sequence = left->sequence;
    result->right_sequence = right->sequence;
    result->async_status = STEREO_POSE_UNVERIFIED;
    result->time_verified = synchronized ? 1U : 0U;
    result->left_metadata = left->metadata;
    result->right_metadata = right->metadata;
    for (index = 0; index < STEREO_LINK_POINTS; ++index) {
        Point2D left_point = left_entry->filtered[index];
        Point2D right_point = right_entry->filtered[index];
        result->point_status[index] = STEREO_INVALID_ARGUMENT;
        if (left_point.valid && right_point.valid)
            result->point_status[index] = stereo_reconstruct_point(&link->geometry,
                left_point.x, left_point.y, right_point.x, right_point.y, &result->point[index]);
    }
    result->image_pose = filtered_pose(left_entry);
    right_pose = filtered_pose(right_entry);
    result->control_status = stereo_pose_reconstruct(&link->geometry,
        &result->image_pose, &right_pose, &left->metadata, &right->metadata,
        STEREO_LINK_MAX_SKEW_US, &result->measured_pose, &result->diagnostics);
    if (!synchronized) {
        result->control_status = STEREO_POSE_UNVERIFIED;
        memset(&result->measured_pose, 0, sizeof(result->measured_pose));
        if (link->async_test_enabled) {
            result->async_test = 1U;
            result->async_status = stereo_pose_reconstruct_async_test(&link->geometry,
                &result->image_pose, &right_pose, &result->async_measured_pose,
                &result->async_diagnostics);
        }
        ++link->unsynchronized_pairs;
    }
    ++link->pairs;
    link->ready = 1;
}

static void match_pair(StereoLink *link)
{
    unsigned left_index, right_index, selected_left = 0, selected_right = 0;
    uint64_t best = UINT64_MAX;
    int found = 0, selected_synchronized = 0;
    for (left_index = 0; left_index < link->count[0]; ++left_index)
        for (right_index = 0; right_index < link->count[1]; ++right_index) {
            const StereoLinkEntry *left = &link->queue[0][left_index];
            const StereoLinkEntry *right = &link->queue[1][right_index];
            const StereoFrameMetadata *left_meta = &left->frame.metadata;
            const StereoFrameMetadata *right_meta = &right->frame.metadata;
            int synchronized = left_meta->exposure_time_verified && right_meta->exposure_time_verified;
            uint64_t gap;
            if (synchronized) {
                if (!left_meta->shared_clock_epoch || left_meta->shared_clock_epoch != right_meta->shared_clock_epoch)
                    continue;
                gap = distance_us(left_meta->exposure_time_us, right_meta->exposure_time_us);
                if (gap > STEREO_LINK_MAX_SKEW_US) continue;
            } else {
                gap = distance_us(left->received_us, right->received_us);
                if (!link->async_test_enabled && gap > STEREO_LINK_MAX_AGE_US) continue;
            }
            if (!found || (!link->async_test_enabled &&
                ((synchronized && !selected_synchronized) ||
                 (synchronized == selected_synchronized && gap < best)))) {
                found = 1;
                selected_left = left_index;
                selected_right = right_index;
                selected_synchronized = synchronized;
                best = gap;
            }
        }
    if (found) {
        reconstruct_pair(link, &link->queue[0][selected_left],
                         &link->queue[1][selected_right], selected_synchronized);
        remove_entry(link, 0, selected_left);
        remove_entry(link, 1, selected_right);
    }
}

int stereo_link_push(StereoLink *link, unsigned side,
                     const StereoCoordinateFrame *frame, uint64_t received_us)
{
    uint32_t advance;
    if (link == NULL || frame == NULL || side > 1 || !link->geometry.initialized || !frame->session_id) return 0;
    expire_entries(link, received_us);
    if (link->have_sequence[side] && link->session[side] == frame->session_id) {
        advance = frame->sequence - link->sequence[side];
        if (!advance || advance >= 0x80000000U) {
            ++link->rejected_frames;
            return 0;
        }
    } else {
        if (link->have_sequence[side]) {
            link->count[0] = link->count[1] = 0;
            link->ready = 0;
            memset(link->filtered, 0, sizeof(link->filtered));
            memset(link->have_filter_time, 0, sizeof(link->have_filter_time));
        }
        link->have_exposure[side] = 0;
    }
    if (frame->metadata.exposure_time_verified) {
        if (!frame->metadata.shared_clock_epoch || (link->have_exposure[side] &&
            (link->clock_epoch[side] != frame->metadata.shared_clock_epoch ||
             frame->metadata.exposure_time_us <= link->last_exposure[side]))) {
            ++link->rejected_frames;
            return 0;
        }
        link->last_exposure[side] = frame->metadata.exposure_time_us;
        link->clock_epoch[side] = frame->metadata.shared_clock_epoch;
        link->have_exposure[side] = 1;
    }
    link->have_sequence[side] = 1;
    link->session[side] = frame->session_id;
    link->sequence[side] = frame->sequence;
    if (link->count[side] == STEREO_LINK_QUEUE) {
        remove_entry(link, side, 0);
        if (link->async_test_enabled) ++link->queue_overflows;
        else ++link->expired_frames;
    }
    link->queue[side][link->count[side]].frame = *frame;
    link->queue[side][link->count[side]].received_us = received_us;
    filter_entry(link, side, &link->queue[side][link->count[side]]);
    ++link->count[side];
    if (!link->async_test_enabled) match_pair(link);
    return 1;
}

int stereo_link_take(StereoLink *link, StereoDepthResult *result)
{
    if (link == NULL || result == NULL) return 0;
    if (link->async_test_enabled && !link->ready) match_pair(link);
    if (!link->ready) return 0;
    *result = link->latest;
    link->ready = 0;
    return 1;
}

void stereo_link_set_async_test(StereoLink *link, int enabled)
{
    if (link == NULL) return;
    link->async_test_enabled = enabled ? 1U : 0U;
    link->count[0] = link->count[1] = 0;
    link->ready = 0U;
    memset(&link->latest, 0, sizeof(link->latest));
    memset(link->filtered, 0, sizeof(link->filtered));
    memset(link->have_filter_time, 0, sizeof(link->have_filter_time));
}
