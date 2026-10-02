#ifndef INTEGRATION_INPUT_POSE_CNN_H
#define INTEGRATION_INPUT_POSE_CNN_H

#include "../cnn_firmware/cnn/cnn_types.h"
#include "stereo_vision/stereo_link.h"

/* Called once for each completed CNN frame, before the Agent frame path. */
int input_pose_cnn_publish(const cnn_result_t *result);
unsigned input_pose_cnn_overwritten(void);
void input_pose_cnn_set_stereo(int enabled);
void input_pose_cnn_set_async_test(int enabled);
int input_pose_cnn_publish_stereo(const StereoDepthResult *result);
int input_pose_cnn_take_stereo(HumanPose2D *image_pose, HumanPose3D *measured_pose, float *dt_sec);
void input_pose_cnn_discard_stereo_pending(void);

#endif
