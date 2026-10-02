#ifndef INTEGRATION_STEREO_BOARD_H
#define INTEGRATION_STEREO_BOARD_H

#include "stereo_vision/stereo_link.h"
#include "cnn/cnn_types.h"

#if defined(ROBOT_STEREO_LEFT) && defined(ROBOT_STEREO_RIGHT)
#error "Select exactly one stereo board role"
#endif

typedef struct {
    uint32_t tx_frames, tx_dropped, rx_frames, rx_overflows, rx_hw_errors;
    uint32_t crc_errors, format_errors, range_errors;
    uint32_t rejected_frames, expired_frames, pairs, unsynchronized_pairs;
    uint32_t async_accepted, async_rejected;
} StereoBoardStats;

int stereo_board_init(void);
int stereo_board_set_async_test(int enabled);
int stereo_board_async_test_enabled(void);
void stereo_board_service(void);
int stereo_board_on_result(const cnn_result_t *result, const StereoFrameMetadata *metadata);
int stereo_board_take_depth(StereoDepthResult *result);
void stereo_board_stats(StereoBoardStats *stats);

#endif
