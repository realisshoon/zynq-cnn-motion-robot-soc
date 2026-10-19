#ifndef ROBOT_STEREO_TRANSPORT_H
#define ROBOT_STEREO_TRANSPORT_H

#include "app/robot/vision/stereo/stereo_link.h"
#include "../../../src/app/cnn/cnn_types.h"

#if defined(ROBOT_STEREO_LEFT) && defined(ROBOT_STEREO_RIGHT)
#error "Select exactly one stereo board role"
#endif

typedef struct {
    uint32_t tx_frames, tx_dropped, rx_frames, rx_overflows, rx_hw_errors;
    uint32_t crc_errors, format_errors, range_errors;
    uint32_t rejected_frames, expired_frames, pairs, unsynchronized_pairs;
    uint32_t async_accepted, async_rejected;
} StereoTransportStats;

int stereo_transport_init(void);
/* Changes input admission only. It never grants physical output or Real Follow. */
int stereo_transport_set_virtual_compute(int enabled);
int stereo_transport_virtual_compute_enabled(void);
int stereo_transport_set_async_test(int enabled);
int stereo_transport_async_test_enabled(void);
int stereo_transport_set_remote_follow(int enabled);
int stereo_transport_remote_follow_enabled(void);
int stereo_transport_remote_requested(void);
int stereo_transport_send_target(const HumanForearmTarget *target, int stationary,
                             int wrist_fresh, uint32_t epoch);
int stereo_transport_take_target(HumanForearmTarget *target, int *stationary,
                             int *wrist_fresh, uint32_t *epoch);
int stereo_transport_send_gripper(const HumanPose2D *pose);
int stereo_transport_take_gripper(HumanPose2D *pose, int *new_session);
int stereo_transport_set_pixel_tau_us(uint32_t tau_us);
uint32_t stereo_transport_pixel_tau_us(void);
void stereo_transport_service(void);
int stereo_transport_on_result(const cnn_result_t *result, const StereoFrameMetadata *metadata);
int stereo_transport_take_depth(StereoDepthResult *result);
void stereo_transport_stats(StereoTransportStats *stats);

#endif
