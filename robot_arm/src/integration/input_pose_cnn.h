#ifndef INTEGRATION_INPUT_POSE_CNN_H
#define INTEGRATION_INPUT_POSE_CNN_H

#include "../cnn_firmware/cnn/cnn_types.h"

/* Called once for each completed CNN frame, before the Agent frame path. */
int input_pose_cnn_publish(const cnn_result_t *result);
unsigned input_pose_cnn_overwritten(void);

#endif
