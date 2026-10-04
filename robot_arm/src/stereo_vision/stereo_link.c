#include "stereo_vision/stereo_link.h"
#include "integration/trace.h"

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
            if (now < received || now - received > STEREO_LINK_MAX_AGE_US) {
                remove_entry(link, side, index);
                ++link->expired_frames;
                ++link->stale_frames;
            } else ++index;
        }
    }
}

static float point_distance_squared(Point2D first, Point2D second)
{
    float delta_x = first.x - second.x;
    float delta_y = first.y - second.y;
    return delta_x * delta_x + delta_y * delta_y;
}

static void reset_quality(StereoLink *link, int reacquire)
{
    memset(link->filtered, 0, sizeof(link->filtered));
    memset(link->accepted_raw, 0, sizeof(link->accepted_raw));
    memset(link->accepted_time, 0, sizeof(link->accepted_time));
    memset(link->accepted_received_us, 0, sizeof(link->accepted_received_us));
    memset(link->accepted_verified, 0, sizeof(link->accepted_verified));
    memset(link->candidate, 0, sizeof(link->candidate));
    memset(link->reacquire, reacquire ? 1 : 0, sizeof(link->reacquire));
    memset(link->have_filter_time, 0, sizeof(link->have_filter_time));
}

static void expire_quality(StereoLink *link, uint64_t now)
{
    unsigned side, major;
    for (side = 0; side < 2; ++side)
        for (major = 0; major < 2; ++major) {
            unsigned index = major ? 10U : 8U;
            StereoLinkCandidate *candidate = &link->candidate[side][major];
            uint64_t received = link->accepted_received_us[side][index];
            if (link->accepted_raw[side][index].valid &&
                (now < received || now - received > STEREO_LINK_MAX_AGE_US)) {
                link->reacquire[side][major] = 1U;
            }
            if (candidate->count && (now < candidate->received_us ||
                now - candidate->received_us > STEREO_LINK_MAX_AGE_US))
                memset(candidate, 0, sizeof(*candidate));
        }
}

static int accept_major_point(StereoLink *link, unsigned side, unsigned major,
                              Point2D point, const StereoLinkEntry *entry,
                              int *new_anchor)
{
    unsigned index = major ? 10U : 8U;
    StereoLinkCandidate *candidate = &link->candidate[side][major];
    Point2D previous = link->accepted_raw[side][index];
    uint64_t received = link->accepted_received_us[side][index];
    float cluster_squared = STEREO_LINK_REACQUIRE_CLUSTER_PX *
        STEREO_LINK_REACQUIRE_CLUSTER_PX;
    if (previous.valid && (entry->received_us < received ||
        entry->received_us - received > STEREO_LINK_MAX_AGE_US)) {
        link->reacquire[side][major] = 1U;
    }
    if (!point.valid) {
        memset(candidate, 0, sizeof(*candidate));
        return 0;
    }
    if (!link->reacquire[side][major] && (!previous.valid ||
        point_distance_squared(point, previous) <=
            STEREO_LINK_PIXEL_RESET_PX * STEREO_LINK_PIXEL_RESET_PX)) {
        memset(candidate, 0, sizeof(*candidate));
        return 1;
    }
    if (!candidate->count || entry->received_us < candidate->received_us ||
        entry->received_us - candidate->received_us > STEREO_LINK_MAX_AGE_US ||
        point_distance_squared(point, candidate->anchor) >= cluster_squared ||
        point_distance_squared(point, candidate->previous) >= cluster_squared) {
        candidate->anchor = point;
        candidate->count = 1U;
    } else if (entry->frame.sequence != candidate->sequence) {
        ++candidate->count;
    }
    candidate->previous = point;
    candidate->received_us = entry->received_us;
    candidate->sequence = entry->frame.sequence;
    if (candidate->count < STEREO_LINK_REACQUIRE_SAMPLES) {
        ++link->jump_rejects;
        return 0;
    }
    memset(candidate, 0, sizeof(*candidate));
    link->reacquire[side][major] = 0U;
    *new_anchor = 1;
    ++link->reacquired_points;
    return 1;
}

static void filter_entry(StereoLink *link, unsigned side, StereoLinkEntry *entry)
{
    const StereoCoordinateFrame *frame = &entry->frame;
    uint8_t verified = frame->metadata.exposure_time_verified;
    uint64_t time = verified ? frame->metadata.exposure_time_us : entry->received_us;
    Point2D raw[STEREO_LINK_POINTS];
    int clock_changed = link->have_filter_time[side] &&
        verified != link->filter_verified[side];
    unsigned index;
    for (index = 0; index < STEREO_LINK_POINTS; ++index)
        raw[index] = index < STEREO_UART_JOINTS ? joint_point(frame, index)
            : marker_point(frame->markers[index - STEREO_UART_JOINTS]);
    if (clock_changed) {
        memset(link->candidate[side], 0, sizeof(link->candidate[side]));
        memset(link->reacquire[side], 1, sizeof(link->reacquire[side]));
    }
    for (index = 0; index < STEREO_LINK_POINTS; ++index) {
        if (index == STEREO_UART_JOINTS || index == STEREO_UART_JOINTS + 2U) {
            float span = sqrtf(point_distance_squared(raw[8], raw[10]));
            float maximum = STEREO_LINK_MARKER_MAX_SPAN_RATIO *
                fmaxf(span, STEREO_LINK_MARKER_SPAN_FLOOR_PX);
            if (raw[index].valid && (!raw[8].valid || !raw[10].valid ||
                point_distance_squared(raw[index], raw[10]) > maximum * maximum)) {
                raw[index].valid = 0U;
                ++link->marker_rejects;
            }
        }
    }
    for (index = 0; index < STEREO_LINK_POINTS; ++index) {
        Point2D point = raw[index];
        Point2D previous = link->filtered[side][index];
        uint64_t elapsed = time >= link->accepted_time[side][index]
            ? time - link->accepted_time[side][index] : 0;
        int new_anchor = !previous.valid ||
            verified != link->accepted_verified[side][index] ||
            elapsed > STEREO_LINK_FILTER_RESET_US;
        if ((index == 8U || index == 10U) &&
            !accept_major_point(link, side, index == 10U, point, entry, &new_anchor))
            point.valid = 0U;
        if (point.valid) {
            link->accepted_raw[side][index] = point;
            if (!new_anchor && (index == 8U || index == 10U ||
                point_distance_squared(point, previous) <=
                    STEREO_LINK_PIXEL_RESET_PX * STEREO_LINK_PIXEL_RESET_PX)) {
                float alpha = 1.0f - expf(-(float)elapsed / STEREO_LINK_PIXEL_TAU_US);
                point.x = previous.x + alpha * (point.x - previous.x);
                point.y = previous.y + alpha * (point.y - previous.y);
            }
            link->filtered[side][index] = point;
            link->accepted_time[side][index] = time;
            link->accepted_received_us[side][index] = entry->received_us;
            link->accepted_verified[side][index] = verified;
        }
        entry->filtered[index] = point;
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

static int gripper_visible(const HumanPose2D *pose)
{
    return pose->wrist.valid && pose->finger1.valid && pose->finger2.valid;
}

static void select_gripper_view(StereoLink *link, HumanPose2D *left,
                                const HumanPose2D *right)
{
    const HumanPose2D *selected;
    int left_visible = gripper_visible(left), right_visible = gripper_visible(right);
    if (link->gripper_source == 2U && right_visible) selected = right;
    else if (left_visible) selected = left;
    else if (right_visible) selected = right;
    else selected = link->gripper_source == 2U ? right : left;
    link->gripper_source = selected == right ? 2U : 1U;
    left->gripper_2d = (HumanGripper2D){selected->wrist, selected->finger1,
        selected->finger2, link->gripper_source,
        hypotf(selected->wrist.x - selected->elbow.x,
               selected->wrist.y - selected->elbow.y)};
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
    select_gripper_view(link, &result->image_pose, &right_pose);
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
    TRACE_PAIR(link->pairs, result, left_entry->filtered, right_entry->filtered);
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
                if (gap > (link->async_test_enabled ? STEREO_LINK_MAX_RECEIVE_GAP_US
                    : STEREO_LINK_MAX_AGE_US)) {
                    if (link->async_test_enabled) ++link->receive_gap_rejects;
                    continue;
                }
            }
            if (!found || (synchronized && !selected_synchronized) ||
                (synchronized == selected_synchronized &&
                 ((!link->async_test_enabled || synchronized) ? gap < best :
                  right_index > selected_right || (right_index == selected_right &&
                   (gap < best || (gap == best && left_index > selected_left)))))) {
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
        if (link->async_test_enabled) {
            link->skipped_frames += selected_left + selected_right;
            link->count[0] -= selected_left + 1U;
            memmove(link->queue[0], &link->queue[0][selected_left + 1U],
                link->count[0] * sizeof(StereoLinkEntry));
            link->count[1] -= selected_right + 1U;
            memmove(link->queue[1], &link->queue[1][selected_right + 1U],
                link->count[1] * sizeof(StereoLinkEntry));
        } else {
            remove_entry(link, 0, selected_left);
            remove_entry(link, 1, selected_right);
        }
    }
}

int stereo_link_push(StereoLink *link, unsigned side,
                     const StereoCoordinateFrame *frame, uint64_t received_us)
{
    uint32_t advance;
    int new_session;
    if (link == NULL || frame == NULL || side > 1 || !link->geometry.initialized || !frame->session_id) return 0;
    if (received_us > link->last_observed_us) link->last_observed_us = received_us;
    expire_entries(link, link->last_observed_us);
    expire_quality(link, link->last_observed_us);
    if (link->last_observed_us - received_us > STEREO_LINK_MAX_AGE_US) {
        ++link->expired_frames;
        ++link->stale_frames;
        return 0;
    }
    new_session = !link->have_sequence[side] || link->session[side] != frame->session_id;
    if (!new_session) {
        advance = frame->sequence - link->sequence[side];
        if (!advance || advance >= 0x80000000U) {
            ++link->rejected_frames;
            return 0;
        }
    }
    if (frame->metadata.exposure_time_verified) {
        if (!frame->metadata.shared_clock_epoch || (!new_session && link->have_exposure[side] &&
            (link->clock_epoch[side] != frame->metadata.shared_clock_epoch ||
             frame->metadata.exposure_time_us <= link->last_exposure[side]))) {
            ++link->rejected_frames;
            return 0;
        }
    }
    if (new_session) {
        if (link->have_sequence[side]) {
            link->count[0] = link->count[1] = 0;
            link->ready = 0;
            reset_quality(link, 1);
            link->gripper_source = 0U;
        }
        link->have_exposure[side] = 0;
    }
    if (frame->metadata.exposure_time_verified) {
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
    return stereo_link_take_at(link, result, link != NULL ? link->last_observed_us : 0);
}

int stereo_link_take_at(StereoLink *link, StereoDepthResult *result, uint64_t now_us)
{
    if (link == NULL || result == NULL) return 0;
    if (now_us > link->last_observed_us) link->last_observed_us = now_us;
    expire_entries(link, now_us);
    expire_quality(link, now_us);
    if (link->ready && (now_us < link->latest.left_received_us ||
        now_us < link->latest.right_received_us ||
        now_us - link->latest.left_received_us > STEREO_LINK_MAX_AGE_US ||
        now_us - link->latest.right_received_us > STEREO_LINK_MAX_AGE_US)) {
        link->ready = 0U;
        ++link->stale_frames;
        ++link->expired_frames;
    }
    if (link->async_test_enabled && !link->ready) match_pair(link);
    if (!link->ready) return 0;
    *result = link->latest;
    link->ready = 0;
    return 1;
}

void stereo_link_set_async_test(StereoLink *link, int enabled)
{
    int reacquire;
    if (link == NULL) return;
    reacquire = link->have_sequence[0] || link->have_sequence[1];
    link->async_test_enabled = enabled ? 1U : 0U;
    link->count[0] = link->count[1] = 0;
    link->ready = 0U;
    link->gripper_source = 0U;
    memset(&link->latest, 0, sizeof(link->latest));
    reset_quality(link, reacquire);
}
