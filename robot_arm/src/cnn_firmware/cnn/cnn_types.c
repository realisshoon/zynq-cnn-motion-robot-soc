#include "cnn_types.h"

const char *cnn_error_string(cnn_error_t error)
{
    switch (error) {
    case CNN_OK: return "OK";
    case CNN_ERR_ARGUMENT: return "invalid argument";
    case CNN_ERR_NOT_INITIALIZED: return "not initialized";
    case CNN_ERR_BAD_VERSION: return "CNN version mismatch";
    case CNN_ERR_BAD_PACK_ID: return "CNN weight package mismatch";
    case CNN_ERR_SD_MOUNT: return "SD mount failed";
    case CNN_ERR_WEIGHT_OPEN: return "weight file open failed";
    case CNN_ERR_WEIGHT_SIZE: return "weight file size mismatch";
    case CNN_ERR_WEIGHT_READ: return "weight file read failed";
    case CNN_ERR_WEIGHT_SHA: return "weight SHA256 mismatch";
    case CNN_ERR_BAD_ALIGNMENT: return "address alignment error";
    case CNN_ERR_SG_DESCRIPTOR: return "image SG descriptor error";
    case CNN_ERR_BUSY: return "CNN busy or pending event";
    case CNN_ERR_HW_FAULT: return "CNN hardware fault";
    case CNN_ERR_TIMEOUT: return "software wait timeout";
    case CNN_ERR_RESULT_UNSTABLE: return "result sequence changed while reading";
    case CNN_ERR_DMA_RESET: return "DMA reset failed";
    case CNN_ERR_WEIGHTS_NOT_LOADED: return "weights not loaded; run w first";
    case CNN_ERR_INTERRUPT: return "CNN interrupt setup or delivery failed";
    default: return "unknown CNN error";
    }
}
