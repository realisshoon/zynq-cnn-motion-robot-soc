/*
 * keypoint_overlay.h
 *
 *  Created on: 2026. 9. 22.
 *      Author: kccistc
 */

#ifndef SRC_KEYPOINT_OVERLAY_KEYPOINT_OVERLAY_H_
#define SRC_KEYPOINT_OVERLAY_KEYPOINT_OVERLAY_H_

#include "xil_types.h"
#include "xstatus.h"

/* Marker color packing of this video pipeline:
 * [23:16] = R
 * [15:8]  = B
 * [7:0]   = G
 */
#define KPO_COLOR_RED      0x00FF0000u
#define KPO_COLOR_GREEN    0x000000FFu
#define KPO_COLOR_BLUE     0x0000FF00u
#define KPO_COLOR_WHITE    0x00FFFFFFu
#define KPO_COLOR_YELLOW   0x00FF00FFu
#define KPO_COLOR_CYAN     0x0000FFFFu

#define KPO_DEFAULT_RADIUS 3u

int  kpo_init(void);
void kpo_set_enable(int on);
void kpo_set_valid_flags(u32 flags);
void kpo_set_color(u32 color);
/* Group colors: shoulders/hips are body; elbows/wrists are arms. */
void kpo_set_body_arm_colors(u32 body_color, u32 arm_color);
void kpo_set_radius(u8 radius);
void kpo_set_source_frame_id(u32 frame_id);
void kpo_set_joint(int index, u16 x, u16 y, u8 score);
/* CNN marker word: [31] found, [20:11] y, [10:0] x. */
void kpo_set_color_results(u32 red_marker_word, u32 blue_marker_word,
                           u32 green_marker_word);
void kpo_commit(void);
int  kpo_commit_pending(void);
int  kpo_wait_commit(u32 timeout);
void kpo_clear(void);

/* Bring-up test */
void kpo_test_center(void);
void kpo_test_multi(void);
void kpo_test_motion(void);
void kpo_set_joint_color_mode(int on);
void kpo_test_fake_cnn_pose(void);
/* Diagnostics: register dump and a CNN-independent grouped-color pattern. */
void kpo_debug_dump(void);
void kpo_test_group_colors(void);

#endif /* SRC_KEYPOINT_OVERLAY_KEYPOINT_OVERLAY_H_ */
