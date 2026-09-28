#ifndef CNN_TYPES_H
#define CNN_TYPES_H

#include "xil_types.h"

#define CNN_JOINT_COUNT 17U

/* COCO keypoint order used by the CNN result register bank. */
typedef enum {
    CNN_JOINT_NOSE = 0,
    CNN_JOINT_LEFT_EYE = 1,
    CNN_JOINT_RIGHT_EYE = 2,
    CNN_JOINT_LEFT_EAR = 3,
    CNN_JOINT_RIGHT_EAR = 4,
    CNN_JOINT_LEFT_SHOULDER = 5,
    CNN_JOINT_RIGHT_SHOULDER = 6,
    CNN_JOINT_LEFT_ELBOW = 7,
    CNN_JOINT_RIGHT_ELBOW = 8,
    CNN_JOINT_LEFT_WRIST = 9,
    CNN_JOINT_RIGHT_WRIST = 10,
    CNN_JOINT_LEFT_HIP = 11,
    CNN_JOINT_RIGHT_HIP = 12,
    CNN_JOINT_LEFT_KNEE = 13,
    CNN_JOINT_RIGHT_KNEE = 14,
    CNN_JOINT_LEFT_ANKLE = 15,
    CNN_JOINT_RIGHT_ANKLE = 16
} cnn_joint_index_t;

/* CNN joints consumed by input_pose_cnn for the right-arm robot path.
 * Finger1/2 are the independent red/blue color markers. This display mask
 * does not remove hip joints from CNN results used by camera tracking. */
#define CNN_ROBOT_INPUT_OVERLAY_MASK \
    ((1U << CNN_JOINT_LEFT_SHOULDER)  | \
     (1U << CNN_JOINT_RIGHT_SHOULDER) | \
     (1U << CNN_JOINT_RIGHT_ELBOW)    | \
     (1U << CNN_JOINT_RIGHT_WRIST))

/* Previous HDMI view, available through the UART display toggle. */
#define CNN_ROBOT_SKELETON_MASK \
    ((1U << CNN_JOINT_LEFT_SHOULDER)  | \
     (1U << CNN_JOINT_RIGHT_SHOULDER) | \
     (1U << CNN_JOINT_LEFT_ELBOW)     | \
     (1U << CNN_JOINT_RIGHT_ELBOW)    | \
     (1U << CNN_JOINT_LEFT_WRIST)     | \
     (1U << CNN_JOINT_RIGHT_WRIST)    | \
     (1U << CNN_JOINT_LEFT_HIP)       | \
     (1U << CNN_JOINT_RIGHT_HIP))

typedef enum {
    CNN_OK = 0,
    CNN_PENDING = 1,
    CNN_ERR_ARGUMENT = -1,
    CNN_ERR_NOT_INITIALIZED = -2,
    CNN_ERR_BAD_VERSION = -3,
    CNN_ERR_BAD_PACK_ID = -4,
    CNN_ERR_SD_MOUNT = -5,
    CNN_ERR_WEIGHT_OPEN = -6,
    CNN_ERR_WEIGHT_SIZE = -7,
    CNN_ERR_WEIGHT_READ = -8,
    CNN_ERR_WEIGHT_SHA = -9,
    CNN_ERR_BAD_ALIGNMENT = -10,
    CNN_ERR_SG_DESCRIPTOR = -11,
    CNN_ERR_BUSY = -12,
    CNN_ERR_HW_FAULT = -13,
    CNN_ERR_TIMEOUT = -14,
    CNN_ERR_RESULT_UNSTABLE = -15,
    CNN_ERR_DMA_RESET = -16,
    CNN_ERR_WEIGHTS_NOT_LOADED = -17,
    CNN_ERR_INTERRUPT = -18
} cnn_error_t;

typedef struct {
    u16 x;
    u16 y;
    s8 score;
    u8 valid;
} cnn_joint_t;

typedef struct {
    u32 frame_id;
    u32 result_seq;
    u32 cycle_count;
    u32 joint_flags;
    u32 red_marker;
    u32 blue_marker;
    u32 green_marker;
    cnn_joint_t joint[CNN_JOINT_COUNT];
} cnn_result_t;

typedef struct {
    u32 cnn_status;
    u32 cnn_error;
    u32 result_seq;
    u32 cycle_count;
    u32 image_dma_control;
    u32 image_dma_status;
    u32 weight_dma_control;
    u32 weight_dma_status;
    u32 feature_mm2s_control;
    u32 feature_mm2s_status;
    u32 feature_s2mm_control;
    u32 feature_s2mm_status;
} cnn_debug_snapshot_t;

const char *cnn_error_string(cnn_error_t error);

#endif
